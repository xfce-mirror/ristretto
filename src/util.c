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
#include "main_window.h"

#include <fcntl.h>
#include <gio/gunixfdlist.h>
#include <glib/gi18n.h>
#include <unistd.h>
#ifdef GDK_WINDOWING_X11
#include <gdk/gdkx.h>
#endif
#ifdef GDK_WINDOWING_WAYLAND
#include <gdk/gdkwayland.h>
#endif
#include <glib/gstdio.h>


#define RSTTO_THUMBNAIL_FLAVOR_NORMAL_N_PIXELS 128
#define RSTTO_THUMBNAIL_FLAVOR_LARGE_N_PIXELS 256
#define RSTTO_THUMBNAIL_FLAVOR_X_LARGE_N_PIXELS 512
#define RSTTO_THUMBNAIL_FLAVOR_XX_LARGE_N_PIXELS 1024

static const gchar *rstto_thumbnail_flavor_names[] = { "normal", "large", "x-large", "xx-large" };
static const guint rstto_thumbnail_n_pixels_unscaled[] = { 32, 48, 64, 96, 128, 192, 256 };
static guint rstto_thumbnail_n_pixels[RSTTO_THUMBNAIL_SIZE_COUNT];



static void
rstto_util_source_remove_all (gpointer data,
                              GObject *object)
{
    while (g_source_remove_by_user_data (object))
        ;
}



/* an often sufficient way to automate memory management of sources, without having
 * to store a source id and use an ad hoc handler */
gpointer
rstto_util_source_autoremove (gpointer object)
{
    g_return_val_if_fail (G_IS_OBJECT (object), object);

    if (!rstto_object_get_data (object, "source-autoremove"))
    {
        g_object_weak_ref (object, rstto_util_source_remove_all, NULL);
        rstto_object_set_data (object, "source-autoremove", GINT_TO_POINTER (TRUE));
    }

    return object;
}



/*
 * Workaround to avoid using Cairo Xlib backend which has some memory issues:
 * https://gitlab.freedesktop.org/cairo/cairo/-/issues/500
 * https://gitlab.freedesktop.org/cairo/cairo/-/issues/510
 */
cairo_pattern_t *
rstto_util_set_source_pixbuf (cairo_t *ctx,
                              const GdkPixbuf *pixbuf,
                              gdouble pixbuf_x,
                              gdouble pixbuf_y)
{
    cairo_t *cr;
    cairo_surface_t *surface;
    cairo_pattern_t *pattern;
    cairo_format_t format;

    /* for non-Xlib backends, this is just a wrapper */
    if (ctx != NULL && cairo_surface_get_type (cairo_get_target (ctx)) != CAIRO_SURFACE_TYPE_XLIB)
    {
        gdk_cairo_set_source_pixbuf (ctx, pixbuf, pixbuf_x, pixbuf_y);
        return NULL;
    }

    /* copied from gdk_cairo_set_source_pixbuf() */
    if (gdk_pixbuf_get_n_channels (pixbuf) == 3)
        format = CAIRO_FORMAT_RGB24;
    else
        format = CAIRO_FORMAT_ARGB32;

    /* create a generic image surface on which to apply gdk_cairo_set_source_pixbuf() */
    surface = cairo_image_surface_create (format,
                                          gdk_pixbuf_get_width (pixbuf),
                                          gdk_pixbuf_get_height (pixbuf));
    cr = cairo_create (surface);
    cairo_surface_destroy (surface);

    /* apply it and get the resulting source */
    gdk_cairo_set_source_pixbuf (cr, pixbuf, pixbuf_x, pixbuf_y);
    pattern = cairo_pattern_reference (cairo_get_source (cr));
    cairo_destroy (cr);

    /* put the source in the original context, if any  */
    if (ctx != NULL)
    {
        cairo_set_source (ctx, pattern);
        g_clear_pointer (&pattern, cairo_pattern_destroy);
    }

    return pattern;
}



void
rstto_util_paint_background_color (GtkWidget *widget,
                                   RsttoSettings *settings,
                                   cairo_t *ctx)
{
    GdkWindow *window;
    GdkRGBA *bgcolor = NULL;
    gboolean bgcolor_override = FALSE;

    /* see if we have a non-default background color */
    window = gdk_window_get_toplevel (gtk_widget_get_window (widget));
    if (gdk_window_get_state (window) & GDK_WINDOW_STATE_FULLSCREEN)
        g_object_get (settings, "bgcolor-fullscreen", &bgcolor, NULL);
    else
    {
        g_object_get (settings, "bgcolor-override", &bgcolor_override, NULL);
        if (bgcolor_override)
            g_object_get (settings, "bgcolor", &bgcolor, NULL);
    }

    /* override default background color if needed */
    if (bgcolor != NULL)
    {
        cairo_save (ctx);
        gdk_cairo_set_source_rgba (ctx, bgcolor);
        cairo_paint (ctx);
        cairo_restore (ctx);
        gdk_rgba_free (bgcolor);
    }
}



void
rstto_util_dialog_error (const gchar *message,
                         GError *error)
{
    GtkWidget *dialog;
    GtkWindow *window;

    window = GTK_WINDOW (rstto_main_window_get_app_window ());
    if (message != NULL && error != NULL)
        dialog = gtk_message_dialog_new (window, GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                         GTK_MESSAGE_ERROR, GTK_BUTTONS_OK,
                                         "%s: %s", message, error->message);
    else if (message != NULL)
        dialog = gtk_message_dialog_new (window, GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                         GTK_MESSAGE_ERROR, GTK_BUTTONS_OK, "%s", message);
    else if (error != NULL)
        dialog = gtk_message_dialog_new (window, GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                         GTK_MESSAGE_ERROR, GTK_BUTTONS_OK, "%s", error->message);
    else
    {
        g_warn_if_reached ();
        return;
    }

    gtk_dialog_run (GTK_DIALOG (dialog));
    gtk_widget_destroy (dialog);
}



void
rstto_util_set_scale_factor (gint scale_factor)
{
    for (gint n = 0; n < RSTTO_THUMBNAIL_SIZE_COUNT; n++)
        rstto_thumbnail_n_pixels[n] = rstto_thumbnail_n_pixels_unscaled[n] * scale_factor;
}



RsttoThumbnailFlavor
rstto_util_get_thumbnail_flavor (RsttoThumbnailSize size)
{
    if (rstto_thumbnail_n_pixels[size] <= RSTTO_THUMBNAIL_FLAVOR_NORMAL_N_PIXELS)
        return RSTTO_THUMBNAIL_FLAVOR_NORMAL;
    else if (rstto_thumbnail_n_pixels[size] <= RSTTO_THUMBNAIL_FLAVOR_LARGE_N_PIXELS)
        return RSTTO_THUMBNAIL_FLAVOR_LARGE;
    else if (rstto_thumbnail_n_pixels[size] <= RSTTO_THUMBNAIL_FLAVOR_X_LARGE_N_PIXELS)
        return RSTTO_THUMBNAIL_FLAVOR_X_LARGE;
    else
        return RSTTO_THUMBNAIL_FLAVOR_XX_LARGE;
}



const gchar *
rstto_util_get_thumbnail_flavor_name (RsttoThumbnailFlavor flavor)
{
    return rstto_thumbnail_flavor_names[flavor];
}



guint
rstto_util_get_thumbnail_n_pixels (RsttoThumbnailSize size)
{
    return rstto_thumbnail_n_pixels[size];
}



gboolean
rstto_util_sendfile (GOutputStream *out,
                     GInputStream *in,
                     GError **error)
{
    gsize buff_size = 1 * 1024 * 1024;
    gchar *buff = g_malloc (buff_size);
    gboolean status = FALSE;

    while (TRUE)
    {
        gssize n_in = g_input_stream_read (in, buff, buff_size, NULL, error);
        if (n_in == -1)
        {
            break;
        }
        else if (n_in == 0)
        {
            status = TRUE;
            break;
        }

        gssize n_out = g_output_stream_write (out, buff, n_in, NULL, error);
        if (n_out == -1)
        {
            break;
        }
        else if (n_out != n_in)
        {
            g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED, _("Cannot write all bytes"));
            break;
        }
    }

    g_free (buff);

    return status;
}


gboolean
rstto_util_is_running_in_flatpak (void)
{
    return g_file_test ("/.flatpak-info", G_FILE_TEST_EXISTS);
}

typedef struct
{
    GDBusConnection *connection;
    guint sub_id;
} RsttoPortalSub;

typedef struct
{
    GFile *file;
    GWeakRef parent;
    gboolean ask;
    GDBusProxy *proxy;
    GUnixFDList *fd_list;
    gint fd_idx;
} RsttoPortalOp;

static void
rstto_portal_op_free (RsttoPortalOp *op)
{
    g_clear_object (&op->file);
    g_clear_object (&op->proxy);
    g_clear_object (&op->fd_list);
    g_weak_ref_clear (&op->parent);
    g_free (op);
}

static void
rstto_portal_sub_free (RsttoPortalSub *sub)
{
    if (sub->connection != NULL && sub->sub_id != 0)
        g_dbus_connection_signal_unsubscribe (sub->connection, sub->sub_id);
    g_clear_object (&sub->connection);
    g_free (sub);
}

static void
rstto_portal_open_file_done (GObject *source,
                             GAsyncResult *res,
                             gpointer user_data);

static void
rstto_portal_response_cb (GDBusConnection *connection,
                          const gchar *sender_name,
                          const gchar *object_path,
                          const gchar *interface_name,
                          const gchar *signal_name,
                          GVariant *parameters,
                          gpointer user_data)
{
    RsttoPortalSub *sub = user_data;
    guint response = 0;
    g_autoptr (GVariant) results = NULL;

    g_variant_get (parameters, "(u@a{sv})", &response, &results);
    if (response != 0)
        g_debug ("OpenURI portal dismissed (response=%u)", response);

    rstto_portal_sub_free (sub);
}

static void
rstto_portal_call_open_file (RsttoPortalOp *op,
                             const gchar *parent_window)
{
    GVariantBuilder opts_builder;
    GVariant *options;
    g_autofree gchar *token = NULL;

    token = g_strdup_printf ("ristretto_%u", g_random_int ());
    g_variant_builder_init (&opts_builder, G_VARIANT_TYPE ("a{sv}"));
    g_variant_builder_add (&opts_builder, "{sv}", "handle_token", g_variant_new_string (token));
    g_variant_builder_add (&opts_builder, "{sv}", "ask", g_variant_new_boolean (op->ask ? TRUE : FALSE));
    options = g_variant_builder_end (&opts_builder);

    g_dbus_proxy_call_with_unix_fd_list (op->proxy,
                                         "OpenFile",
                                         g_variant_new ("(sh@a{sv})", parent_window, op->fd_idx, options),
                                         G_DBUS_CALL_FLAGS_NONE,
                                         -1,
                                         op->fd_list,
                                         NULL,
                                         rstto_portal_open_file_done,
                                         NULL);
}

#ifdef GDK_WINDOWING_WAYLAND
static void
rstto_portal_wayland_exported (GdkWindow *window,
                               const char *handle,
                               gpointer user_data)
{
    RsttoPortalOp *op = user_data;
    g_autofree gchar *parent_window = NULL;

    if (handle != NULL)
        parent_window = g_strdup_printf ("wayland:%s", handle);
    else
        parent_window = g_strdup ("");

    rstto_portal_call_open_file (op, parent_window);
    rstto_portal_op_free (op);
}
#endif

static void
rstto_portal_open_file_done (GObject *source,
                             GAsyncResult *res,
                             gpointer user_data)
{
    GDBusProxy *proxy = G_DBUS_PROXY (source);
    g_autoptr (GError) error = NULL;
    g_autoptr (GVariant) ret = NULL;
    g_autoptr (GUnixFDList) out_fd_list = NULL;
    g_autofree gchar *request_path = NULL;
    GDBusConnection *connection;
    RsttoPortalSub *sub;

    ret = g_dbus_proxy_call_with_unix_fd_list_finish (proxy, &out_fd_list, res, &error);
    if (ret == NULL)
    {
        g_warning ("OpenURI portal call failed: %s", error ? error->message : "unknown error");
        return;
    }

    g_variant_get (ret, "(o)", &request_path);
    connection = g_dbus_proxy_get_connection (proxy);
    if (connection == NULL)
        return;

    sub = g_new0 (RsttoPortalSub, 1);
    sub->connection = g_object_ref (connection);
    sub->sub_id = g_dbus_connection_signal_subscribe (connection,
                                                      "org.freedesktop.portal.Desktop",
                                                      "org.freedesktop.portal.Request",
                                                      "Response",
                                                      request_path,
                                                      NULL,
                                                      G_DBUS_SIGNAL_FLAGS_NO_MATCH_RULE,
                                                      rstto_portal_response_cb,
                                                      sub,
                                                      NULL);
}

static void
rstto_portal_proxy_ready (GObject *source,
                          GAsyncResult *res,
                          gpointer user_data)
{
    g_autoptr (GDBusProxy) proxy = NULL;
    g_autoptr (GError) error = NULL;
    g_autoptr (GUnixFDList) fd_list = NULL;
    g_autofree gchar *path = NULL;
    g_autofree gchar *uri = NULL;
    gint fd = -1;
    gint fd_idx = -1;
    RsttoPortalOp *op = user_data;

    proxy = g_dbus_proxy_new_for_bus_finish (res, &error);
    if (proxy == NULL)
    {
        g_warning ("Cannot connect to portal: %s", error ? error->message : "unknown error");
        rstto_portal_op_free (op);
        return;
    }

    if (op->file == NULL || !G_IS_FILE (op->file))
    {
        rstto_portal_op_free (op);
        return;
    }

    path = g_file_get_path (op->file);
    if (path == NULL)
    {
        g_autoptr (GtkWindow) parent_win = g_weak_ref_get (&op->parent);
        uri = g_file_get_uri (op->file);
        gtk_show_uri_on_window (parent_win, uri, gtk_get_current_event_time (), NULL);
        rstto_portal_op_free (op);
        return;
    }

    fd = g_open (path, O_RDONLY | O_CLOEXEC);
    if (fd == -1)
    {
        g_warning ("Cannot open '%s' for portal: %m", path);
        rstto_portal_op_free (op);
        return;
    }

    fd_list = g_unix_fd_list_new ();
    fd_idx = g_unix_fd_list_append (fd_list, fd, &error);
    close (fd);
    if (fd_idx == -1)
    {
        g_warning ("Cannot export fd to portal: %s", error ? error->message : "unknown error");
        rstto_portal_op_free (op);
        return;
    }

    op->proxy = g_steal_pointer (&proxy);
    op->fd_list = g_steal_pointer (&fd_list);
    op->fd_idx = fd_idx;

    {
        g_autoptr (GtkWindow) parent_win = g_weak_ref_get (&op->parent);
        if (parent_win != NULL && gtk_widget_get_realized (GTK_WIDGET (parent_win)))
        {
            GdkWindow *gdk_win = gtk_widget_get_window (GTK_WIDGET (parent_win));
#ifdef GDK_WINDOWING_WAYLAND
            if (GDK_IS_WAYLAND_WINDOW (gdk_win))
            {
                gdk_wayland_window_export_handle (gdk_win,
                                                  rstto_portal_wayland_exported,
                                                  op,
                                                  NULL);
                return;
            }
#endif
#ifdef GDK_WINDOWING_X11
            if (GDK_IS_X11_WINDOW (gdk_win))
            {
                g_autofree gchar *parent_window = g_strdup_printf ("x11:%lx",
                                                                   (gulong) gdk_x11_window_get_xid (gdk_win));
                rstto_portal_call_open_file (op, parent_window);
                rstto_portal_op_free (op);
                return;
            }
#endif
        }
    }

    rstto_portal_call_open_file (op, "");
    rstto_portal_op_free (op);
}

void
rstto_util_open_file_with_portal (GFile *file,
                                  GtkWindow *parent,
                                  RsttoPortalOpenMode mode)
{
    RsttoPortalOp *op;

    g_return_if_fail (G_IS_FILE (file));

    op = g_new0 (RsttoPortalOp, 1);
    op->file = g_object_ref (file);
    g_weak_ref_init (&op->parent, parent);
    op->ask = (mode == RSTTO_PORTAL_OPEN_ASK);

    g_dbus_proxy_new_for_bus (G_BUS_TYPE_SESSION,
                              G_DBUS_PROXY_FLAGS_NONE,
                              NULL,
                              "org.freedesktop.portal.Desktop",
                              "/org/freedesktop/portal/desktop",
                              "org.freedesktop.portal.OpenURI",
                              NULL,
                              rstto_portal_proxy_ready,
                              op);
}
