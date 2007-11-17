/*
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
#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>
#include <string.h>

#include <thunar-vfs/thunar-vfs.h>
#include <libexif/exif-data.h>

#include "navigator.h"
#include "picture_viewer.h"
#include "thumbnail_viewer.h"
#include "main_window.h"

static ThunarVfsMimeDatabase *mime_dbase = NULL;

static XfceRc *xfce_rc;
static gint window_save_geometry_timer_id = 0;

static gboolean
rstto_window_save_geometry_timer (gpointer user_data);
static void
rstto_window_save_geometry_timer_destroy(gpointer user_data);
static gboolean
cb_rstto_main_window_configure_event (GtkWidget *widget, GdkEventConfigure *event);

gboolean version = FALSE;

static GOptionEntry entries[] =
{
    {    "version", 'v', G_OPTION_FLAG_IN_MAIN, G_OPTION_ARG_NONE, &version,
        N_("Version information"),
        NULL
    },
    { NULL }
};

int main(int argc, char **argv)
{

    GError *cli_error = NULL;
    gchar *path_dir = NULL;
    gint n;

    #ifdef ENABLE_NLS
    bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);
    bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
    textdomain (GETTEXT_PACKAGE);
    #endif

    if(!gtk_init_with_args(&argc, &argv, _(""), entries, PACKAGE, NULL))
    {
        if (cli_error != NULL)
        {
            g_print (_("%s: %s\nTry %s --help to see a full list of available command line options.\n"), PACKAGE, cli_error->message, PACKAGE_NAME);
            g_error_free (cli_error);
            return 1;
        }
    }

    if(version)
    {
        g_print("%s\n", PACKAGE_STRING);
        return 0;
    }

    thunar_vfs_init();

    mime_dbase = thunar_vfs_mime_database_get_default();


    gtk_window_set_default_icon_name("ristretto");
    xfce_rc = xfce_rc_config_open(XFCE_RESOURCE_CONFIG, "ristretto/ristrettorc", FALSE);

    const gchar *thumbnail_viewer_orientation = xfce_rc_read_entry(xfce_rc, "ThumbnailViewerOrientation", "horizontal");
    gboolean show_thumbnail_viewer = xfce_rc_read_bool_entry(xfce_rc, "ShowThumbnailViewer", TRUE);
    gboolean show_toolbar = xfce_rc_read_bool_entry(xfce_rc, "ShowToolBar", TRUE);
    gint window_width = xfce_rc_read_int_entry(xfce_rc, "LastWindowWidth", 400);
    gint window_height = xfce_rc_read_int_entry(xfce_rc, "LastWindowHeight", 300);
    gint slideshow_timeout = xfce_rc_read_int_entry(xfce_rc, "SlideShowTimeout", 5000);
    gint max_cache = xfce_rc_read_int_entry(xfce_rc, "MaxImagesCacheSize", 1);
    gboolean preload_during_slideshow = xfce_rc_read_bool_entry (xfce_rc, "PreloadDuringSlideShow", FALSE);
    
    GtkWidget *window = rstto_main_window_new();
    gtk_widget_ref(window);

    RsttoNavigator *navigator = rstto_main_window_get_navigator(RSTTO_MAIN_WINDOW(window));

    navigator->preload = preload_during_slideshow;

    rstto_main_window_set_max_cache_size(RSTTO_MAIN_WINDOW(window), max_cache);
    rstto_main_window_set_slideshow_timeout(RSTTO_MAIN_WINDOW(window), (gdouble)slideshow_timeout);

    GtkRecentManager *recent_manager = rstto_main_window_get_recent_manager(RSTTO_MAIN_WINDOW(window));
    rstto_navigator_set_timeout(navigator, slideshow_timeout);

    for (n = 1; n < argc; ++n)
    {
        ThunarVfsPath *path;
        if (g_path_is_absolute(argv[n]))
            path = thunar_vfs_path_new(argv[n], NULL);
        else
        {
            gchar *base_dir = g_get_current_dir();

            path_dir = g_build_path("/", base_dir, argv[n], NULL);
            path = thunar_vfs_path_new(path_dir, NULL);

            g_free(base_dir);
        }

        if (path)
        {

            ThunarVfsInfo *info = thunar_vfs_info_new_for_path(path, NULL);
            if(info)
            {
                if(strcmp(thunar_vfs_mime_info_get_name(info->mime_info), "inode/directory"))
                {
                    ThunarVfsPath *_path = thunar_vfs_path_get_parent(path);
                    thunar_vfs_path_unref(path);
                    path = _path;

                    gchar *path_string = thunar_vfs_path_dup_string(path);
                    
                    GDir *dir = g_dir_open(path_string, 0, NULL);
                    const gchar *filename = g_dir_read_name(dir);
                    while (filename)
                    {
                        gchar *path_name = g_strconcat(path_string,  "/", filename, NULL);
                        ThunarVfsPath *file_path = thunar_vfs_path_new(path_name, NULL);
                        if (file_path)
                        {
                            ThunarVfsInfo *file_info = thunar_vfs_info_new_for_path(file_path, NULL);
                            gchar *file_media = thunar_vfs_mime_info_get_media(file_info->mime_info);
                            if(!strcmp(file_media, "image"))
                            {
                                RsttoNavigatorEntry *entry = rstto_navigator_entry_new(navigator, file_info);
                                gint i = rstto_navigator_add (navigator, entry);
                                if (path_dir == NULL)
                                {
                                    if (!strcmp(path_name, argv[n]))
                                    {
                                        rstto_navigator_set_file(navigator, i);
                                    }
                                }
                                else
                                {
                                    if (!strcmp(path_name, path_dir))
                                    {
                                        rstto_navigator_set_file(navigator, i);
                                    }

                                }
                            }
                            g_free(file_media);
                            thunar_vfs_path_unref(file_path);
                        }
                        g_free(path_name);
                        filename = g_dir_read_name(dir);
                    }
                    g_dir_close(dir);
                    g_free(path_string);
                }
                else
                {
                    GDir *dir = g_dir_open(argv[n], 0, NULL);
                    const gchar *filename = g_dir_read_name(dir);
                    while (filename)
                    {
                        gchar *path_name = g_strconcat(argv[n],  "/", filename, NULL);
                        ThunarVfsPath *file_path = thunar_vfs_path_new(path_name, NULL);
                        if (file_path)
                        {
                            ThunarVfsInfo *file_info = thunar_vfs_info_new_for_path(file_path, NULL);
                            gchar *file_media = thunar_vfs_mime_info_get_media(file_info->mime_info);
                            if(!strcmp(file_media, "image"))
                            {
                                RsttoNavigatorEntry *entry = rstto_navigator_entry_new(navigator, file_info);
                                rstto_navigator_add (navigator, entry);
                            }
                            g_free(file_media);
                            thunar_vfs_path_unref(file_path);
                        }
                        g_free(path_name);
                        filename = g_dir_read_name(dir);
                    }
                    rstto_navigator_jump_first(navigator);
                    g_dir_close(dir);
                }
                gchar *uri = thunar_vfs_path_dup_uri(info->path);
                gtk_recent_manager_add_item(recent_manager, uri);
                g_free(uri);
            }
            thunar_vfs_path_unref(path);
        }
    }


    g_signal_connect(G_OBJECT(window), "destroy", G_CALLBACK(gtk_main_quit), NULL);
    g_signal_connect(G_OBJECT(window), "configure-event", G_CALLBACK(cb_rstto_main_window_configure_event), NULL);

    if (!strcmp(thumbnail_viewer_orientation, "vertical"))
    {
        rstto_main_window_set_thumbnail_viewer_orientation(RSTTO_MAIN_WINDOW(window), GTK_ORIENTATION_VERTICAL);
    }
    else
    {
        rstto_main_window_set_thumbnail_viewer_orientation(RSTTO_MAIN_WINDOW(window), GTK_ORIENTATION_HORIZONTAL);
    }


    gtk_window_set_default_size(GTK_WINDOW(window), window_width, window_height);
    gtk_widget_show_all(window);

    rstto_main_window_set_show_thumbnail_viewer(RSTTO_MAIN_WINDOW(window), show_thumbnail_viewer);
    rstto_main_window_set_show_toolbar(RSTTO_MAIN_WINDOW(window), show_toolbar);

    gtk_main();
    xfce_rc_write_bool_entry(xfce_rc, "ShowToolBar", rstto_main_window_get_show_toolbar(RSTTO_MAIN_WINDOW(window)));
    xfce_rc_write_bool_entry(xfce_rc, "PreloadDuringSlideShow", navigator->preload);
    xfce_rc_write_bool_entry(xfce_rc, "ShowThumbnailViewer", rstto_main_window_get_show_thumbnail_viewer(RSTTO_MAIN_WINDOW(window)));
    switch (rstto_main_window_get_thumbnail_viewer_orientation(RSTTO_MAIN_WINDOW(window)))
    {
        case GTK_ORIENTATION_VERTICAL:
            xfce_rc_write_entry(xfce_rc, "ThumbnailViewerOrientation", "vertical");
            break;
        case GTK_ORIENTATION_HORIZONTAL:
            xfce_rc_write_entry(xfce_rc, "ThumbnailViewerOrientation", "horizontal");
            break;
    }
    xfce_rc_write_int_entry(xfce_rc, "MaxImagesCacheSize", rstto_main_window_get_max_cache_size(RSTTO_MAIN_WINDOW(window)));
    xfce_rc_write_int_entry(xfce_rc, "SlideShowTimeout", (gint)rstto_main_window_get_slideshow_timeout(RSTTO_MAIN_WINDOW(window)));
    xfce_rc_flush(xfce_rc);
    xfce_rc_close(xfce_rc);
    gtk_widget_unref(window);
    return 0;
}

static gboolean
rstto_window_save_geometry_timer (gpointer user_data)
{
    GtkWindow *window = GTK_WINDOW(user_data);
    gint width = 0;
    gint height = 0;
    /* check if the window is still visible */
    if (GTK_WIDGET_VISIBLE (window))
    {
        /* determine the current state of the window */
        gint state = gdk_window_get_state (GTK_WIDGET (window)->window);

        /* don't save geometry for maximized or fullscreen windows */
        if ((state & (GDK_WINDOW_STATE_MAXIMIZED | GDK_WINDOW_STATE_FULLSCREEN)) == 0)
        {
            /* determine the current width/height of the window... */
            gtk_window_get_size (GTK_WINDOW (window), &width, &height);

            /* ...and remember them as default for new windows */
            xfce_rc_write_int_entry (xfce_rc, "LastWindowWidth", width);
            xfce_rc_write_int_entry (xfce_rc, "LastWindowHeight", height);
        }
    }
    return FALSE;
}

static void
rstto_window_save_geometry_timer_destroy(gpointer user_data)
{
    window_save_geometry_timer_id = 0;
}

static gboolean
cb_rstto_main_window_configure_event (GtkWidget *widget, GdkEventConfigure *event)
{
    /* shamelessly copied from thunar, written by benny */
    /* check if we have a new dimension here */
    if (widget->allocation.width != event->width || widget->allocation.height != event->height)
    {
        /* drop any previous timer source */
        if (window_save_geometry_timer_id > 0)
            g_source_remove (window_save_geometry_timer_id);

        /* check if we should schedule another save timer */
        if (GTK_WIDGET_VISIBLE (widget))
        {
            /* save the geometry one second after the last configure event */
            window_save_geometry_timer_id = g_timeout_add_full (G_PRIORITY_LOW, 1000, rstto_window_save_geometry_timer,
                widget, rstto_window_save_geometry_timer_destroy);
        }
    }

    /* let Gtk+ handle the configure event */
    return FALSE;
}
