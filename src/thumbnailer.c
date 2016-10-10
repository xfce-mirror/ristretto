/*
 *  Copyright (c) Stephan Arts 2006-2012 <stephan@xfce.org>
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version 2
 *  of the License, or (at your option) any later version.
 * 
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 * 
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 *  02110-1301, USA.
 */

#include <config.h>

#include <string.h>

#include <glib.h>
#include <gio/gio.h>
#include <dbus/dbus-glib.h>

#include <libexif/exif-data.h>

#include <libxfce4util/libxfce4util.h>

#include "util.h"
#include "file.h"
#include "settings.h"
#include "thumbnailer.h"
#include "marshal.h"

static void
rstto_thumbnailer_init (GObject *);
static void
rstto_thumbnailer_class_init (GObjectClass *);

static void
rstto_thumbnailer_dispose (GObject *object);
static void
rstto_thumbnailer_finalize (GObject *object);

static void
rstto_thumbnailer_set_property (
        GObject      *object,
        guint         property_id,
        const GValue *value,
        GParamSpec   *pspec);
static void
rstto_thumbnailer_get_property (
        GObject    *object,
        guint       property_id,
        GValue     *value,
        GParamSpec *pspec);

static void
cb_rstto_thumbnailer_request_finished (
        DBusGProxy *proxy,
        gint handle,
        gpointer data);
static void
cb_rstto_thumbnailer_thumbnail_ready (
        DBusGProxy *proxy,
        gint handle,
        const gchar **uri,
        gpointer data);

static gboolean
rstto_thumbnailer_queue_request_timer (RsttoThumbnailer *thumbnailer);

static GObjectClass *parent_class = NULL;

static RsttoThumbnailer *thumbnailer_object;

enum
{
    RSTTO_THUMBNAILER_SIGNAL_READY = 0,
    RSTTO_THUMBNAILER_SIGNAL_COUNT
};

static gint rstto_thumbnailer_signals[RSTTO_THUMBNAILER_SIGNAL_COUNT];

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

        rstto_thumbnailer_type = g_type_register_static (
                G_TYPE_OBJECT,
                "RsttoThumbnailer",
                &rstto_thumbnailer_info,
                0);
    }
    return rstto_thumbnailer_type;
}

struct _RsttoThumbnailerPriv
{
    DBusGConnection   *connection;
    DBusGProxy        *proxy;
    RsttoSettings     *settings;

    GSList            *queue;

    GSList            *in_process_queue;
    gint               handle;

    gboolean           show_missing_thumbnailer_error;

    gint request_timer_id;
};

static void
rstto_thumbnailer_init (GObject *object)
{
    RsttoThumbnailer *thumbnailer = RSTTO_THUMBNAILER (object);

    thumbnailer->priv = g_new0 (RsttoThumbnailerPriv, 1);
    thumbnailer->priv->connection = dbus_g_bus_get(DBUS_BUS_SESSION, NULL);
    thumbnailer->priv->settings = rstto_settings_new();

    thumbnailer->priv->show_missing_thumbnailer_error =
            rstto_settings_get_boolean_property (
                    thumbnailer->priv->settings,
                    "show-error-missing-thumbnailer");

    if (thumbnailer->priv->connection)
    {
    
        thumbnailer->priv->proxy = dbus_g_proxy_new_for_name (
                thumbnailer->priv->connection,
                "org.freedesktop.thumbnails.Thumbnailer1",
                "/org/freedesktop/thumbnails/Thumbnailer1",
                "org.freedesktop.thumbnails.Thumbnailer1");

        dbus_g_object_register_marshaller (
                (GClosureMarshal) _rstto_marshal_VOID__UINT_BOXED,
                G_TYPE_NONE,
                G_TYPE_UINT,
                G_TYPE_STRV,
                G_TYPE_INVALID);

        dbus_g_proxy_add_signal (
                thumbnailer->priv->proxy,
                "Finished",
                G_TYPE_UINT,
                G_TYPE_INVALID);
        dbus_g_proxy_add_signal (
                thumbnailer->priv->proxy,
                "Ready",
                G_TYPE_UINT,
                G_TYPE_STRV,
                G_TYPE_INVALID);

        dbus_g_proxy_connect_signal (
                thumbnailer->priv->proxy,
                "Finished",
                G_CALLBACK (cb_rstto_thumbnailer_request_finished),
                thumbnailer,
                NULL);
        dbus_g_proxy_connect_signal (
                thumbnailer->priv->proxy,
                "Ready",
                G_CALLBACK(cb_rstto_thumbnailer_thumbnail_ready),
                thumbnailer,
                NULL);
    }
}


static void
rstto_thumbnailer_class_init (GObjectClass *object_class)
{
    RsttoThumbnailerClass *thumbnailer_class = RSTTO_THUMBNAILER_CLASS (
            object_class);

    parent_class = g_type_class_peek_parent (thumbnailer_class);

    object_class->dispose = rstto_thumbnailer_dispose;
    object_class->finalize = rstto_thumbnailer_finalize;

    object_class->set_property = rstto_thumbnailer_set_property;
    object_class->get_property = rstto_thumbnailer_get_property;


    rstto_thumbnailer_signals[RSTTO_THUMBNAILER_SIGNAL_READY] = g_signal_new("ready",
            G_TYPE_FROM_CLASS(thumbnailer_class),
            G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
            0,
            NULL,
            NULL,
            g_cclosure_marshal_VOID__OBJECT,
            G_TYPE_NONE,
            1,
            G_TYPE_OBJECT,
            NULL);
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
        if (thumbnailer->priv->settings)
        {
            g_object_unref (thumbnailer->priv->settings);
            thumbnailer->priv->settings = NULL;
        }

        if (thumbnailer->priv->proxy)
        {
            g_object_unref (thumbnailer->priv->proxy);
            thumbnailer->priv->proxy = NULL;
        }

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
rstto_thumbnailer_set_property (
        GObject      *object,
        guint         property_id,
        const GValue *value,
        GParamSpec   *pspec)
{
    switch (property_id)
    {
        default:
            break;
    }
}

static void
rstto_thumbnailer_get_property (
        GObject    *object,
        guint       property_id,
        GValue     *value,
        GParamSpec *pspec)
{
    switch (property_id)
    {
        default:
            break;
    }
}

void
rstto_thumbnailer_queue_file (
        RsttoThumbnailer *thumbnailer,
        RsttoFile *file )
{
    g_return_if_fail ( RSTTO_IS_THUMBNAILER (thumbnailer) );

    g_return_if_fail ( RSTTO_IS_FILE (file) );

    if (thumbnailer->priv->request_timer_id)
    {
        g_source_remove (thumbnailer->priv->request_timer_id);
        if (thumbnailer->priv->handle)
        {
            if(dbus_g_proxy_call(thumbnailer->priv->proxy,
                    "Dequeue",
                    NULL,
                    G_TYPE_UINT, thumbnailer->priv->handle,
                    G_TYPE_INVALID) == FALSE)
            {
            }
            thumbnailer->priv->handle = 0;
        }
    }

    if (g_slist_find (thumbnailer->priv->queue, file) == NULL)
    {
        g_object_ref (file);
        thumbnailer->priv->queue = g_slist_prepend (
                thumbnailer->priv->queue,
                file);
    }

    thumbnailer->priv->request_timer_id = g_timeout_add_full (
            G_PRIORITY_LOW,
            300,
            (GSourceFunc)rstto_thumbnailer_queue_request_timer,
            thumbnailer,
            NULL);
}

void
rstto_thumbnailer_dequeue_file (
        RsttoThumbnailer *thumbnailer,
        RsttoFile *file)
{
    g_return_if_fail ( RSTTO_IS_THUMBNAILER (thumbnailer) );

    g_return_if_fail ( RSTTO_IS_FILE (file) );
    
    if (thumbnailer->priv->request_timer_id)
    {
        g_source_remove (thumbnailer->priv->request_timer_id);
    }

    if (thumbnailer->priv->handle)
    {
        if(dbus_g_proxy_call(thumbnailer->priv->proxy,
                "Dequeue",
                NULL,
                G_TYPE_UINT, thumbnailer->priv->handle,
                G_TYPE_INVALID) == FALSE)
        {
        }
        thumbnailer->priv->handle = 0;
        g_slist_foreach (thumbnailer->priv->in_process_queue, (GFunc)g_object_unref, NULL);
        g_slist_free (thumbnailer->priv->in_process_queue);
        thumbnailer->priv->in_process_queue = NULL;
    }

    if (g_slist_find (thumbnailer->priv->queue, file) != NULL)
    {
        thumbnailer->priv->queue = g_slist_remove_all (
                thumbnailer->priv->queue,
                file);
        g_object_unref (file);
    }

    thumbnailer->priv->request_timer_id = g_timeout_add_full (
            G_PRIORITY_LOW,
            300,
            (GSourceFunc)rstto_thumbnailer_queue_request_timer,
            thumbnailer,
            NULL);
}

static gboolean
rstto_thumbnailer_queue_request_timer (
        RsttoThumbnailer *thumbnailer)
{
    const gchar **uris;
    const gchar **mimetypes;
    GSList *iter;
    gint i = 0;
    RsttoFile *file;
    GError *error = NULL;
    GtkWidget *error_dialog = NULL;
    GtkWidget *vbox, *do_not_show_checkbox;

    g_return_val_if_fail ( RSTTO_IS_THUMBNAILER (thumbnailer), FALSE);

    thumbnailer->priv->in_process_queue = thumbnailer->priv->queue;
    thumbnailer->priv->queue = NULL;

    uris = g_new0 (
            const gchar *,
            g_slist_length(thumbnailer->priv->in_process_queue) + 1);
    mimetypes = g_new0 (
            const gchar *,
            g_slist_length (thumbnailer->priv->in_process_queue) + 1);

    iter = thumbnailer->priv->in_process_queue;
    while (iter)
    {
        if (iter->data)
        {
            file = RSTTO_FILE(iter->data);
            uris[i] = rstto_file_get_uri (file);
            mimetypes[i] = rstto_file_get_content_type (file);
        }
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
        if (NULL != error)
        {
            g_warning("DBUS-call failed:%s", error->message);
            if ((error->domain == DBUS_GERROR) &&
                (error->code == DBUS_GERROR_SERVICE_UNKNOWN) &&
                thumbnailer->priv->show_missing_thumbnailer_error == TRUE)
            {
                GDK_THREADS_ENTER();

                error_dialog = gtk_message_dialog_new_with_markup (
                        NULL,
                        0,
                        GTK_MESSAGE_WARNING,
                        GTK_BUTTONS_OK,
                        _("The thumbnailer-service can not be reached,\n"
                        "for this reason, the thumbnails can not be\n"
                        "created.\n\n"
                        "Install <b>Tumbler</b> or another <i>thumbnailing daemon</i>\n"
                        "to resolve this issue.")
                        );
                vbox = gtk_dialog_get_content_area (
                        GTK_DIALOG (error_dialog));

                do_not_show_checkbox = gtk_check_button_new_with_mnemonic (
                        _("Do _not show this message again"));
                gtk_box_pack_end (
                        GTK_BOX (vbox),
                        do_not_show_checkbox,
                        TRUE,
                        FALSE,
                        0);
                gtk_widget_show (do_not_show_checkbox);
                gtk_dialog_run (GTK_DIALOG(error_dialog));

                thumbnailer->priv->show_missing_thumbnailer_error = FALSE;
                if (TRUE == gtk_toggle_button_get_active (
                        GTK_TOGGLE_BUTTON (do_not_show_checkbox)))
                {
                    rstto_settings_set_boolean_property (
                        thumbnailer->priv->settings,
                        "show-error-missing-thumbnailer",
                         FALSE);
                }
                gtk_widget_destroy (error_dialog);
                GDK_THREADS_LEAVE();
            }
        }
        /* TOOO: Nice cleanup */
    }
    
    g_free (uris);
    g_free (mimetypes);

    thumbnailer->priv->request_timer_id = 0;
    return FALSE;
}

static void
cb_rstto_thumbnailer_request_finished (
        DBusGProxy *proxy,
        gint handle,
        gpointer data)
{
    RsttoThumbnailer *thumbnailer = RSTTO_THUMBNAILER (data);

    g_return_if_fail ( RSTTO_IS_THUMBNAILER (thumbnailer) );

    if (thumbnailer->priv->in_process_queue)
    {
        g_slist_foreach (thumbnailer->priv->in_process_queue, (GFunc)g_object_unref, NULL);
        g_slist_free (thumbnailer->priv->in_process_queue);
        thumbnailer->priv->in_process_queue = NULL;
    }
}

static void
cb_rstto_thumbnailer_thumbnail_ready (
        DBusGProxy *proxy,
        gint handle,
        const gchar **uri,
        gpointer data)
{
    RsttoThumbnailer *thumbnailer = RSTTO_THUMBNAILER (data);
    RsttoFile *file;
    GSList *iter = thumbnailer->priv->in_process_queue;
    gint x = 0;
    const gchar *f_uri;

    g_return_if_fail ( RSTTO_IS_THUMBNAILER (thumbnailer) );

    while (iter)
    {
        if ((uri[x] == NULL) || (iter->data == NULL))
        {
            break;
        }

        file = RSTTO_FILE (iter->data);
        f_uri = rstto_file_get_uri (file);
        if (strcmp (uri[x], f_uri) == 0)
        {
            g_signal_emit (
                    G_OBJECT (thumbnailer),
                    rstto_thumbnailer_signals[RSTTO_THUMBNAILER_SIGNAL_READY],
                    0,
                    file,
                    NULL);
            thumbnailer->priv->in_process_queue = g_slist_remove (
                    thumbnailer->priv->in_process_queue,
                    file);
            g_object_unref (file);

            iter = thumbnailer->priv->in_process_queue;
            x++;
        }
        else
        {
            iter = g_slist_next(iter);
        }
    } 
}
