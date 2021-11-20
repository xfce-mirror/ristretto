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

#include "util.h"
#include "thumbnailer.h"
#include "tumbler.h"
#include "main_window.h"

#include <glib/gi18n.h>



enum
{
    RSTTO_THUMBNAILER_SIGNAL_READY = 0,
    RSTTO_THUMBNAILER_SIGNAL_COUNT
};

static gint rstto_thumbnailer_signals[RSTTO_THUMBNAILER_SIGNAL_COUNT];

static RsttoThumbnailer *thumbnailer_object;



static void
rstto_thumbnailer_finalize (GObject *object);

static void
cb_rstto_thumbnailer_thumbnail_ready (TumblerThumbnailer1 *proxy,
                                      guint handle,
                                      const gchar *const *uri,
                                      gpointer data);
static void
rstto_thumbnailer_queue_reset_in_process (RsttoThumbnailer *thumbnailer);
static gboolean
rstto_thumbnailer_queue_request_timer (gpointer user_data);



struct _RsttoThumbnailerPrivate
{
    GDBusConnection     *connection;
    TumblerThumbnailer1 *proxy;
    RsttoSettings       *settings;
    RsttoImageList      *image_list;

    GSList              *queue, *in_process_queue;
    guint                handle, request_timer_id;
    gint                 n_visible_items;

    gboolean             show_missing_thumbnailer_error;
};



G_DEFINE_TYPE_WITH_PRIVATE (RsttoThumbnailer, rstto_thumbnailer, G_TYPE_OBJECT)



static void
rstto_thumbnailer_init (RsttoThumbnailer *thumbnailer)
{
    thumbnailer->priv = rstto_thumbnailer_get_instance_private (thumbnailer);
    thumbnailer->priv->connection = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, NULL);
    thumbnailer->priv->settings = rstto_settings_new ();
    thumbnailer->priv->queue = NULL;
    thumbnailer->priv->in_process_queue = NULL;

    thumbnailer->priv->show_missing_thumbnailer_error =
            rstto_settings_get_boolean_property (
                    thumbnailer->priv->settings,
                    "show-error-missing-thumbnailer");

    if (thumbnailer->priv->connection)
    {
        thumbnailer->priv->proxy = tumbler_thumbnailer1_proxy_new_sync (
                thumbnailer->priv->connection,
                G_DBUS_PROXY_FLAGS_NONE,
                "org.freedesktop.thumbnails.Thumbnailer1",
                "/org/freedesktop/thumbnails/Thumbnailer1",
                NULL,
                NULL);

        g_signal_connect (thumbnailer->priv->proxy,
                         "ready",
                         G_CALLBACK (cb_rstto_thumbnailer_thumbnail_ready),
                         thumbnailer);
    }
}


static void
rstto_thumbnailer_class_init (RsttoThumbnailerClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = rstto_thumbnailer_finalize;

    rstto_thumbnailer_signals[RSTTO_THUMBNAILER_SIGNAL_READY] = g_signal_new ("ready",
            G_TYPE_FROM_CLASS (klass),
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
 * rstto_thumbnailer_finalize:
 * @object:
 *
 */
static void
rstto_thumbnailer_finalize (GObject *object)
{
    RsttoThumbnailer *thumbnailer = RSTTO_THUMBNAILER (object);

    g_slist_free_full (thumbnailer->priv->queue, g_object_unref);
    rstto_thumbnailer_queue_reset_in_process (thumbnailer);

    g_clear_object (&thumbnailer->priv->settings);
    g_clear_object (&thumbnailer->priv->proxy);
    g_clear_object (&thumbnailer->priv->connection);

    G_OBJECT_CLASS (rstto_thumbnailer_parent_class)->finalize (object);
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

void
rstto_thumbnailer_queue_file (
        RsttoThumbnailer *thumbnailer,
        RsttoFile *file)
{
    g_return_if_fail (RSTTO_IS_THUMBNAILER (thumbnailer));
    g_return_if_fail (RSTTO_IS_FILE (file));

    if (thumbnailer->priv->request_timer_id != 0)
        REMOVE_SOURCE (thumbnailer->priv->request_timer_id);

    thumbnailer->priv->queue = g_slist_prepend (thumbnailer->priv->queue, g_object_ref (file));
    thumbnailer->priv->request_timer_id =
        g_timeout_add_full (G_PRIORITY_LOW, 300, rstto_thumbnailer_queue_request_timer,
                            rstto_util_source_autoremove (thumbnailer), NULL);
}

void
rstto_thumbnailer_set_image_list (RsttoThumbnailer *thumbnailer,
                                  RsttoImageList   *image_list)
{
    /* we don't take a ref here, this would uselessly put us into a ref counting cycle */
    thumbnailer->priv->image_list = image_list;
}

void
rstto_thumbnailer_set_n_visible_items (RsttoThumbnailer *thumbnailer,
                                       gint n_items)
{
    thumbnailer->priv->n_visible_items = n_items;
}

static void
rstto_thumbnailer_queue_reset_in_process (RsttoThumbnailer *thumbnailer)
{
    tumbler_thumbnailer1_call_dequeue_sync (thumbnailer->priv->proxy,
                                            thumbnailer->priv->handle, NULL, NULL);
    g_slist_free_full (thumbnailer->priv->in_process_queue, g_object_unref);
}

static gboolean
rstto_thumbnailer_queue_request_timer (gpointer user_data)
{
    RsttoThumbnailer *thumbnailer = user_data;
    GtkWidget *error_dialog, *vbox, *do_not_show_checkbox;
    GSList *iter;
    GError *error = NULL;
    const gchar **uris, **mimetypes;
    gint i;

    g_return_val_if_fail (RSTTO_IS_THUMBNAILER (thumbnailer), FALSE);

    rstto_thumbnailer_queue_reset_in_process (thumbnailer);
    thumbnailer->priv->in_process_queue = thumbnailer->priv->queue;
    thumbnailer->priv->queue = NULL;

    uris = g_new0 (const gchar *, thumbnailer->priv->n_visible_items + 1);
    mimetypes = g_new0 (const gchar *, thumbnailer->priv->n_visible_items + 1);

    for (iter = thumbnailer->priv->in_process_queue, i = 0;
         iter != NULL && i < thumbnailer->priv->n_visible_items;
         iter = iter->next)
    {
        /* directories are loaded without this costly filtering, so it is done
         * here only when required */
        if (! rstto_file_is_valid (iter->data))
        {
            rstto_image_list_remove_file (thumbnailer->priv->image_list, iter->data);
            continue;
        }

        uris[i] = rstto_file_get_uri (iter->data);
        mimetypes[i] = rstto_file_get_content_type (iter->data);
        i++;
    }

    if (! tumbler_thumbnailer1_call_queue_sync (thumbnailer->priv->proxy,
                                                (const gchar * const*) uris,
                                                (const gchar * const*) mimetypes,
                                                "normal", "default", 0,
                                                &thumbnailer->priv->handle,
                                                NULL, &error))
    {
        if (NULL != error)
        {
            g_warning ("DBUS-call failed: %s", error->message);
            if (error->domain == G_DBUS_ERROR
                && error->code == G_DBUS_ERROR_SERVICE_UNKNOWN
                && thumbnailer->priv->show_missing_thumbnailer_error)
            {
                error_dialog = gtk_message_dialog_new_with_markup (
                        GTK_WINDOW (rstto_main_window_get_app_window ()),
                        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                        GTK_MESSAGE_WARNING,
                        GTK_BUTTONS_OK,
                        _("The thumbnailer-service can not be reached,\n"
                        "for this reason, the thumbnails can not be\n"
                        "created.\n\n"
                        "Install <b>Tumbler</b> or another <i>thumbnailing daemon</i>\n"
                        "to resolve this issue."));
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
                gtk_dialog_run (GTK_DIALOG (error_dialog));

                thumbnailer->priv->show_missing_thumbnailer_error = FALSE;
                if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (do_not_show_checkbox)))
                {
                    rstto_settings_set_boolean_property (
                        thumbnailer->priv->settings,
                        "show-error-missing-thumbnailer",
                         FALSE);
                }
                gtk_widget_destroy (error_dialog);
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
cb_rstto_thumbnailer_thumbnail_ready (
        TumblerThumbnailer1 *proxy,
        guint handle,
        const gchar *const *uri,
        gpointer data)
{
    RsttoThumbnailer *thumbnailer = data;
    GSList *iter = thumbnailer->priv->in_process_queue;
    gint n = 0;

    g_return_if_fail (RSTTO_IS_THUMBNAILER (thumbnailer));

    while (iter != NULL && uri[n] != NULL)
    {
        if (g_strcmp0 (uri[n], rstto_file_get_uri (iter->data)) == 0)
        {
            g_signal_emit (thumbnailer,
                           rstto_thumbnailer_signals[RSTTO_THUMBNAILER_SIGNAL_READY],
                           0, iter->data, NULL);
            iter = thumbnailer->priv->in_process_queue;
            n++;
        }
        else
            iter = iter->next;
    }
}
