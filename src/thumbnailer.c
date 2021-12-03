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
cb_rstto_thumbnailer_thumbnail_error (TumblerThumbnailer1 *proxy,
                                      guint handle,
                                      const gchar *const *uri,
                                      gint error_code,
                                      const gchar *error_message,
                                      gpointer data);
static gboolean
rstto_thumbnailer_queue_request_timer (gpointer user_data);



struct _RsttoThumbnailerPrivate
{
    GDBusConnection     *connection;
    TumblerThumbnailer1 *proxy;
    RsttoSettings       *settings;

    GSList              *queue, *in_process_queue;
    guint                handle, request_timer_id;

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

        g_signal_connect (thumbnailer->priv->proxy, "ready",
                          G_CALLBACK (cb_rstto_thumbnailer_thumbnail_ready), thumbnailer);
        g_signal_connect (thumbnailer->priv->proxy, "error",
                          G_CALLBACK (cb_rstto_thumbnailer_thumbnail_error), thumbnailer);
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

    tumbler_thumbnailer1_call_dequeue_sync (thumbnailer->priv->proxy,
                                            thumbnailer->priv->handle, NULL, NULL);
    g_slist_free_full (thumbnailer->priv->queue, g_object_unref);
    g_slist_free_full (thumbnailer->priv->in_process_queue, g_object_unref);

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
    GSList *last_visible;
    gint n_items;

    g_return_if_fail (RSTTO_IS_THUMBNAILER (thumbnailer));
    g_return_if_fail (RSTTO_IS_FILE (file));

    if (thumbnailer->priv->request_timer_id != 0)
        REMOVE_SOURCE (thumbnailer->priv->request_timer_id);

    /* relative to the size of the icon bar, which is what matters, this is a O(1) cost
     * operation, so we can keep the use of a GList for convenience */
    n_items = rstto_icon_bar_get_n_visible_items (rstto_main_window_get_app_icon_bar ());
    last_visible = g_slist_nth (thumbnailer->priv->queue, n_items - 1);
    if (last_visible != NULL)
    {
        rstto_file_set_thumbnail_state (last_visible->data, RSTTO_THUMBNAIL_STATE_UNPROCESSED);
        g_object_unref (last_visible->data);
        thumbnailer->priv->queue = g_slist_delete_link (thumbnailer->priv->queue, last_visible);
    }

    thumbnailer->priv->queue = g_slist_prepend (thumbnailer->priv->queue, g_object_ref (file));
    thumbnailer->priv->request_timer_id =
        g_timeout_add_full (G_PRIORITY_LOW, 300, rstto_thumbnailer_queue_request_timer,
                            rstto_util_source_autoremove (thumbnailer), NULL);
}

static gboolean
rstto_thumbnailer_queue_request_timer (gpointer user_data)
{
    RsttoThumbnailer *thumbnailer = user_data;
    RsttoImageList *image_list;
    GtkWidget *error_dialog, *vbox, *do_not_show_checkbox;
    GSList *iter, *temp = NULL;
    GError *error = NULL;
    const gchar **uris, **mimetypes;
    gint n_items, i;

    g_return_val_if_fail (RSTTO_IS_THUMBNAILER (thumbnailer), FALSE);

    image_list = rstto_main_window_get_app_image_list ();
    n_items = rstto_icon_bar_get_n_visible_items (rstto_main_window_get_app_icon_bar ());
    uris = g_new0 (const gchar *, n_items + 1);
    mimetypes = g_new0 (const gchar *, n_items + 1);

    /* dequeue files in process: they will eventually be re-queued after those
     * of the current request */
    tumbler_thumbnailer1_call_dequeue_sync (thumbnailer->priv->proxy,
                                            thumbnailer->priv->handle, NULL, NULL);

    /* handle current request, whose size doesn't exceed the number of visible items */
    for (iter = thumbnailer->priv->queue, i = 0; iter != NULL; iter = iter->next)
    {
        /* directories are loaded without this costly filtering, so it is done
         * here only when required */
        if (! rstto_file_is_valid (iter->data))
        {
            rstto_image_list_remove_file (image_list, iter->data);
            g_object_unref (iter->data);

            continue;
        }

        uris[i] = rstto_file_get_uri (iter->data);
        mimetypes[i] = rstto_file_get_content_type (iter->data);
        temp = g_slist_prepend (temp, iter->data);
        i++;
    }

    g_slist_free (thumbnailer->priv->queue);
    thumbnailer->priv->queue = NULL;

    /* handle previously queued files, up to the number of visible items */
    for (iter = thumbnailer->priv->in_process_queue; iter != NULL && i < n_items; iter = iter->next)
    {
        uris[i] = rstto_file_get_uri (iter->data);
        mimetypes[i] = rstto_file_get_content_type (iter->data);
        temp = g_slist_prepend (temp, iter->data);
        i++;
    }

    /* clean up the rest of the list and reset the status of unqueued files */
    for (; iter != NULL; iter = iter->next)
    {
        rstto_file_set_thumbnail_state (iter->data, RSTTO_THUMBNAIL_STATE_UNPROCESSED);
        g_object_unref (iter->data);
    }

    g_slist_free (thumbnailer->priv->in_process_queue);

    /* update the list of queued files */
    thumbnailer->priv->in_process_queue = g_slist_reverse (temp);

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
cb_rstto_thumbnailer_thumbnail_ready (TumblerThumbnailer1 *proxy,
                                      guint handle,
                                      const gchar *const *uris,
                                      gpointer data)
{
    RsttoThumbnailer *thumbnailer = data;
    GSList *iter = thumbnailer->priv->in_process_queue;
    gint n = 0;

    g_return_if_fail (RSTTO_IS_THUMBNAILER (thumbnailer));

    while (iter != NULL && uris[n] != NULL)
    {
        if (g_strcmp0 (uris[n], rstto_file_get_uri (iter->data)) == 0)
        {
            rstto_file_set_thumbnail_state (iter->data, RSTTO_THUMBNAIL_STATE_PROCESSED);
            g_signal_emit (thumbnailer,
                           rstto_thumbnailer_signals[RSTTO_THUMBNAILER_SIGNAL_READY],
                           0, iter->data, NULL);

            g_object_unref (iter->data);
            thumbnailer->priv->in_process_queue =
                g_slist_remove (thumbnailer->priv->in_process_queue, iter->data);

            iter = thumbnailer->priv->in_process_queue;
            n++;
        }
        else
            iter = iter->next;
    }
}

static void
cb_rstto_thumbnailer_thumbnail_error (TumblerThumbnailer1 *proxy,
                                      guint handle,
                                      const gchar *const *uris,
                                      gint error_code,
                                      const gchar *error_message,
                                      gpointer data)
{
    RsttoThumbnailer *thumbnailer = data;
    GSList *iter = thumbnailer->priv->in_process_queue;
    gint n = 0;

    g_return_if_fail (RSTTO_IS_THUMBNAILER (thumbnailer));

    while (iter != NULL && uris[n] != NULL)
    {
        if (g_strcmp0 (uris[n], rstto_file_get_uri (iter->data)) == 0)
        {
            /* it was probably cancelled when we unqueued files above, and since it is in the
             * queue here, we wanted it to be processed: back to square one, state unchanged */
            if (error_code == G_IO_ERROR_CANCELLED)
                rstto_thumbnailer_queue_file (thumbnailer, iter->data);
            else
                rstto_file_set_thumbnail_state (iter->data, RSTTO_THUMBNAIL_STATE_ERROR);

            g_object_unref (iter->data);
            thumbnailer->priv->in_process_queue =
                g_slist_remove (thumbnailer->priv->in_process_queue, iter->data);

            iter = thumbnailer->priv->in_process_queue;
            n++;
        }
        else
            iter = iter->next;
    }
}
