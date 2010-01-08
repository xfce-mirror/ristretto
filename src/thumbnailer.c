/*
 *  Copyright (c) 2009 Stephan Arts <stephan@xfce.org>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Library General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include <config.h>

#include <glib.h>
#include <gtk/gtk.h>
#include <gio/gio.h>
#include <dbus/dbus-glib.h>

#include "image.h"
#include "thumbnail.h"
#include "thumbnailer.h"

static void
rstto_thumbnailer_init (GObject *);
static void
rstto_thumbnailer_class_init (GObjectClass *);

static void
rstto_thumbnailer_dispose (GObject *object);
static void
rstto_thumbnailer_finalize (GObject *object);

static void
rstto_thumbnailer_set_property    (GObject      *object,
                                guint         property_id,
                                const GValue *value,
                                GParamSpec   *pspec);
static void
rstto_thumbnailer_get_property    (GObject    *object,
                                guint       property_id,
                                GValue     *value,
                                GParamSpec *pspec);

static void
cb_rstto_thumbnailer_request_finished (DBusGProxy *proxy, gint handle, gpointer data);
static void
cb_rstto_thumbnailer_thumbnail_ready (DBusGProxy *proxy, gint handle, const gchar **uri, gpointer data);

static gboolean
rstto_thumbnailer_queue_request_timer (RsttoThumbnailer *thumbnailer);

static GObjectClass *parent_class = NULL;

static RsttoThumbnailer *thumbnailer_object;

enum
{
    PROP_0,
};

GType
rstto_thumbnailer_get_type (void)
{
    static GType rstto_thumbnailer_type = 0;

    if (!rstto_thumbnailer_type)
    {
        static const GTypeInfo rstto_thumbnailer_info = 
        {
            sizeof (RsttoThumbnailerClass),
            (GBaseInitFunc) NULL,
            (GBaseFinalizeFunc) NULL,
            (GClassInitFunc) rstto_thumbnailer_class_init,
            (GClassFinalizeFunc) NULL,
            NULL,
            sizeof (RsttoThumbnailer),
            0,
            (GInstanceInitFunc) rstto_thumbnailer_init,
            NULL
        };

        rstto_thumbnailer_type = g_type_register_static (G_TYPE_OBJECT, "RsttoThumbnailer", &rstto_thumbnailer_info, 0);
    }
    return rstto_thumbnailer_type;
}

struct _RsttoThumbnailerPriv
{
    DBusGConnection   *connection;
    DBusGProxy        *proxy;
    GSList *queue;
    gint handle;

    gint request_timer_id;
};

static void
rstto_thumbnailer_init (GObject *object)
{
    RsttoThumbnailer *thumbnailer = RSTTO_THUMBNAILER (object);

    thumbnailer->priv = g_new0 (RsttoThumbnailerPriv, 1);
    thumbnailer->priv->connection = dbus_g_bus_get(DBUS_BUS_SESSION, NULL);
    if (thumbnailer->priv->connection)
    {
    
        thumbnailer->priv->proxy = dbus_g_proxy_new_for_name (thumbnailer->priv->connection,
                                                                "org.freedesktop.thumbnails.Thumbnailer1",
                                                                "/org/freedesktop/thumbnails/Thumbnailer1",
                                                                "org.freedesktop.thumbnails.Thumbnailer1");
        dbus_g_proxy_add_signal (thumbnailer->priv->proxy, "Finished", G_TYPE_UINT, G_TYPE_INVALID);

        dbus_g_proxy_connect_signal (thumbnailer->priv->proxy, "Finished", cb_rstto_thumbnailer_request_finished, thumbnailer, NULL);
    }
}


static void
rstto_thumbnailer_class_init (GObjectClass *object_class)
{
    GParamSpec *pspec;

    RsttoThumbnailerClass *thumbnailer_class = RSTTO_THUMBNAILER_CLASS (object_class);

    parent_class = g_type_class_peek_parent (thumbnailer_class);

    object_class->dispose = rstto_thumbnailer_dispose;
    object_class->finalize = rstto_thumbnailer_finalize;

    object_class->set_property = rstto_thumbnailer_set_property;
    object_class->get_property = rstto_thumbnailer_get_property;

}

/**
 * rstto_thumbnailer_dispose:
 * @object:
 *
 */
static void
rstto_thumbnailer_dispose (GObject *object)
{
    RsttoThumbnailer *thumbnailer = RSTTO_THUMBNAILER (object);

    if (thumbnailer->priv)
    {
        g_free (thumbnailer->priv);
        thumbnailer->priv = NULL;
    }
}

/**
 * rstto_thumbnailer_finalize:
 * @object:
 *
 */
static void
rstto_thumbnailer_finalize (GObject *object)
{
}



/**
 * rstto_thumbnailer_new:
 *
 *
 * Singleton
 */
RsttoThumbnailer *
rstto_thumbnailer_new (void)
{
    if (thumbnailer_object == NULL)
    {
        thumbnailer_object = g_object_new (RSTTO_TYPE_THUMBNAILER, NULL);
    }
    else
    {
        g_object_ref (thumbnailer_object);
    }

    return thumbnailer_object;
}


static void
rstto_thumbnailer_set_property    (GObject      *object,
                                guint         property_id,
                                const GValue *value,
                                GParamSpec   *pspec)
{
    RsttoThumbnailer *thumbnailer = RSTTO_THUMBNAILER (object);

    switch (property_id)
    {
        default:
            break;
    }

}

static void
rstto_thumbnailer_get_property    (GObject    *object,
                                guint       property_id,
                                GValue     *value,
                                GParamSpec *pspec)
{
    RsttoThumbnailer *thumbnailer = RSTTO_THUMBNAILER (object);

    switch (property_id)
    {
        default:
            break;
    }
}

void
rstto_thumbnailer_queue_image (RsttoThumbnailer *thumbnailer, RsttoImage *image)
{
    if (thumbnailer->priv->request_timer_id)
        g_source_remove (thumbnailer->priv->request_timer_id);

    thumbnailer->priv->queue = g_slist_prepend (thumbnailer->priv->queue, image);

    thumbnailer->priv->request_timer_id = g_timeout_add_full (G_PRIORITY_LOW, 100, (GSourceFunc)rstto_thumbnailer_queue_request_timer, thumbnailer, NULL);
}

void
rstto_thumbnailer_dequeue_image (RsttoThumbnailer *thumbnailer, RsttoImage *image)
{
    if (thumbnailer->priv->request_timer_id)
        g_source_remove (thumbnailer->priv->request_timer_id);

    thumbnailer->priv->queue = g_slist_remove_all (thumbnailer->priv->queue, image);

    thumbnailer->priv->request_timer_id = g_timeout_add_full (G_PRIORITY_LOW, 100, (GSourceFunc)rstto_thumbnailer_queue_request_timer, thumbnailer, NULL);
}

static gboolean
rstto_thumbnailer_queue_request_timer (RsttoThumbnailer *thumbnailer)
{
    gchar **uris;
    gchar **mimetypes;
    GSList *iter;
    gint i = 0;
    GFile *file;
    RsttoImage *image;
    GError *error = NULL;

    uris = g_new0 (gchar *, g_slist_length(thumbnailer->priv->queue)+1);
    mimetypes = g_new0 (gchar *, g_slist_length(thumbnailer->priv->queue)+1);

    iter = thumbnailer->priv->queue;
    while (iter)
    {
        image = rstto_thumbnail_get_image (RSTTO_THUMBNAIL(iter->data));
        file = rstto_image_get_file (image);
        uris[i] = g_file_get_uri (file);
        mimetypes[i] = "image/jpeg";
        iter = g_slist_next(iter);
        i++;
    }

    if(dbus_g_proxy_call(thumbnailer->priv->proxy,
                 "Queue",
                 &error,
                 G_TYPE_STRV, uris,
                 G_TYPE_STRV, mimetypes,
                 G_TYPE_STRING, "normal",
                 G_TYPE_STRING, "default",
                 G_TYPE_UINT, 0,
                 G_TYPE_INVALID,
                 G_TYPE_UINT, &thumbnailer->priv->handle,
                 G_TYPE_INVALID) == FALSE)
    {
        g_debug("call faile:%s", error->message);
        /* TOOO: Nice cleanup */
    }
    
    thumbnailer->priv->request_timer_id = 0;
    return FALSE;
}

static void
cb_rstto_thumbnailer_request_finished (DBusGProxy *proxy, gint handle, gpointer data)
{
    RsttoThumbnailer *thumbnailer = RSTTO_THUMBNAILER (data);
    GSList *iter = thumbnailer->priv->queue;
    GSList *prev;
    while (iter)
    {
        rstto_thumbnail_update (iter->data);
        prev = iter;
        iter = g_slist_next(iter);
        thumbnailer->priv->queue = g_slist_remove (thumbnailer->priv->queue, prev);
    } 
}

static void
cb_rstto_thumbnailer_thumbnail_ready (DBusGProxy *proxy, gint handle, const gchar **uri, gpointer data)
{
    g_debug("Ready");   
}

/*

                    
*/
