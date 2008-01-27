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

#if GTK_CHECK_VERSION(2,12,0)
#define RSTTO_COLOR_PARSE gdk_color_parse
#define RSTTO_COLOR_TO_STRING gdk_color_to_string
#else
gboolean
rstto_color_parse(const gchar *string, GdkColor *color)
{
    gint i;
    guint16 red = 0;
    guint16 green = 0;
    guint16 blue = 0;
    g_return_val_if_fail(string != NULL, FALSE);
    /* Check is the string is long enough tp contain #rrrrggggbbbb
     */
    if(strlen(string) != 13)
        return FALSE;
    if(string[0] != '#')
        return FALSE;

    /* red */
    for(i = 1; i < 5; ++i)
    {
        if(string[i] >= '0' && string[i] <= '9')
        {
            red |= (string[i] ^ 0x30);
        }
        else
        {
            if(string[i] >= 'a' && string[i] <= 'f')
            {
                red |= ((string[i] ^ 0x60)+9);
            }
            else
            {
                return FALSE;
            }
        }
        if (i < 4)
            red = red << 4;
    }

    /* green */
    for(i = 5; i < 9; ++i)
    {
        if(string[i] >= '0' && string[i] <= '9')
        {
            green |= (string[i] ^ 0x30);
        }
        else
        {
            if(string[i] >= 'a' && string[i] <= 'f')
            {
                green |= ((string[i] ^ 0x60)+9);
            }
            else
            {
                return FALSE;
            }
        }

        if (i < 8)
            green = green << 4;

    }

    /* blue */
    for(i = 9; i < 13; ++i)
    {
        if(string[i] >= '0' && string[i] <= '9')
        {
            blue |= (string[i] ^ 0x30);
        }
        else
        {
            if(string[i] >= 'a' && string[i] <= 'f')
            {
                blue |= ((string[i] ^ 0x60)+9);
            }
            else
            {
                return FALSE;
            }
        }
        if (i < 12)
            blue = blue << 4;
    }

    color->red = red;
    color->green = green;
    color->blue = blue;
    return TRUE;
}

gchar *
rstto_color_to_string(const GdkColor *color)
{
    gint i;
    gchar *color_string = g_new0(gchar, 14);

    color_string[0] = '#';

    for(i = 0; i < 4; ++i)
    {
        if(((color->red >> (4*i))&0x000f) < 0xA)
        {
           color_string[1+3-i] = (((color->red >> (4*i))&0x000f)|0x0030);
        }
        else
        {
           color_string[1+3-i] = ((((color->red >> (4*i))&0x000f)|0x0060)-9);
        }
    }

    for(i = 0; i < 4; ++i)
    {
        if(((color->green >> (4*i))&0x000f) < 0xA)
        {
           color_string[5+3-i] = (((color->green >> (4*i))&0x000f)|0x0030);
        }
        else
        {
           color_string[5+3-i] = ((((color->green >> (4*i))&0x000f)|0x0060)-9);
        }
    }

    for(i = 0; i < 4; ++i)
    {
        if(((color->blue >> (4*i))&0x000f) < 0xA)
        {
           color_string[9+3-i] = (((color->blue >> (4*i))&0x000f)|0x0030);
        }
        else
        {
           color_string[9+3-i] = ((((color->blue >> (4*i))&0x000f)|0x0060)-9);
        }
    }

    return color_string;
}
#define RSTTO_COLOR_PARSE(string, color) rstto_color_parse(string, color)
#define RSTTO_COLOR_TO_STRING(color) rstto_color_to_string(color)
#endif

int main(int argc, char **argv)
{
    GdkColor *bg_color = NULL;
    GError *cli_error = NULL;
    gchar *path_dir = NULL;
    gint n;

    #ifdef ENABLE_NLS
    bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);
    bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
    textdomain (GETTEXT_PACKAGE);
    #endif

    if(!gtk_init_with_args(&argc, &argv, _(""), entries, PACKAGE, &cli_error))
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
    gint max_cache = xfce_rc_read_int_entry(xfce_rc, "MaxImagesCacheSize", 64);
    gboolean preload_during_slideshow = xfce_rc_read_bool_entry (xfce_rc, "PreloadDuringSlideShow", FALSE);
    gboolean override_bg_color = xfce_rc_read_bool_entry (xfce_rc, "OverrideBgColor", FALSE);

    if (override_bg_color)
    {
        const gchar *color = xfce_rc_read_entry(xfce_rc, "BgColor", "#000000000000");
        bg_color = g_new0(GdkColor, 1);
        if(!RSTTO_COLOR_PARSE(color, bg_color))
        {
            g_debug("parse failed");
        }
    }
    
    GtkWidget *window = rstto_main_window_new();
    gtk_widget_ref(window);

    RsttoNavigator *navigator = rstto_main_window_get_navigator(RSTTO_MAIN_WINDOW(window));

    navigator->preload = preload_during_slideshow;

    rstto_main_window_set_max_cache_size(RSTTO_MAIN_WINDOW(window), max_cache);
    rstto_main_window_set_slideshow_timeout(RSTTO_MAIN_WINDOW(window), (gdouble)slideshow_timeout);

    GtkRecentManager *recent_manager = rstto_main_window_get_recent_manager(RSTTO_MAIN_WINDOW(window));
    rstto_navigator_set_timeout(navigator, slideshow_timeout);

    /* When more then one file is provided over the CLI,
     * just open those files and don't index the folder
     */
    if (argc > 2)
    {
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
                        gchar *file_media = thunar_vfs_mime_info_get_media(info->mime_info);
                        if(!strcmp(file_media, "image"))
                        {
                            RsttoNavigatorEntry *entry = rstto_navigator_entry_new(navigator, info);
                            rstto_navigator_add (navigator, entry, TRUE);
                        }
                    }
                    else
                    {
                        GDir *dir = g_dir_open(argv[n], 0, NULL);
                        const gchar *filename = g_dir_read_name(dir);
                        rstto_navigator_set_monitor_handle_for_dir(navigator, info->path);

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
                                    rstto_navigator_add (navigator, entry, FALSE);
                                }
                                g_free(file_media);
                                thunar_vfs_path_unref(file_path);
                            }
                            g_free(path_name);
                            filename = g_dir_read_name(dir);
                        }
                        g_dir_close(dir);
                    }

                    gchar *uri = thunar_vfs_path_dup_uri(info->path);
                    gtk_recent_manager_add_item(recent_manager, uri);
                    g_free(uri);
                }
            }
        }
        rstto_navigator_jump_first(navigator);
    }
    else
    {
        if (argc == 2)
        {
            ThunarVfsPath *path;
            if (g_path_is_absolute(argv[1]))
            {
                path = thunar_vfs_path_new(argv[1], NULL);
                path_dir = g_strdup(argv[1]);
            }
            else
            {
                gchar *base_dir = g_get_current_dir();

                path_dir = g_build_path("/", base_dir, argv[1], NULL);
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
                        gchar* media = thunar_vfs_mime_info_get_media(info->mime_info);
                        if(!strcmp(media, "image"))
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
                                        gint i = rstto_navigator_add (navigator, entry, FALSE);
                                        if (path_dir == NULL)
                                        {
                                            if (!strcmp(path_name, argv[1]))
                                            {
                                                rstto_navigator_set_file_nr(navigator, i);
                                            }
                                        }
                                        else
                                        {
                                            if (!strcmp(path_name, path_dir))
                                            {
                                                rstto_navigator_set_file_nr(navigator, i);
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
                        g_free(media);
                    }
                    else
                    {
                        GDir *dir = g_dir_open(argv[1], 0, NULL);
                        const gchar *filename = g_dir_read_name(dir);

                        rstto_navigator_set_monitor_handle_for_dir(navigator, info->path);
                        while (filename)
                        {
                            gchar *path_name = g_strconcat(path_dir,  "/", filename, NULL);
                            ThunarVfsPath *file_path = thunar_vfs_path_new(path_name, NULL);
                            if (file_path)
                            {
                                ThunarVfsInfo *file_info = thunar_vfs_info_new_for_path(file_path, NULL);
                                gchar *file_media = thunar_vfs_mime_info_get_media(file_info->mime_info);
                                if(!strcmp(file_media, "image"))
                                {
                                    RsttoNavigatorEntry *entry = rstto_navigator_entry_new(navigator, file_info);
                                    rstto_navigator_add (navigator, entry, FALSE);
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

    if (bg_color)
    {
        rstto_main_window_set_pv_bg_color(RSTTO_MAIN_WINDOW(window), bg_color);
        g_free(bg_color);
    }


    gtk_window_set_default_size(GTK_WINDOW(window), window_width, window_height);
    gtk_widget_show_all(window);

    rstto_main_window_set_show_thumbnail_viewer(RSTTO_MAIN_WINDOW(window), show_thumbnail_viewer);
    rstto_main_window_set_show_toolbar(RSTTO_MAIN_WINDOW(window), show_toolbar);

    gtk_main();

    bg_color = (GdkColor *)rstto_main_window_get_pv_bg_color(RSTTO_MAIN_WINDOW(window));

    xfce_rc_write_bool_entry(xfce_rc, "ShowToolBar", rstto_main_window_get_show_toolbar(RSTTO_MAIN_WINDOW(window)));
    xfce_rc_write_bool_entry(xfce_rc, "PreloadDuringSlideShow", navigator->preload);
    xfce_rc_write_bool_entry(xfce_rc, "ShowThumbnailViewer", rstto_main_window_get_show_thumbnail_viewer(RSTTO_MAIN_WINDOW(window)));
    if (bg_color)
    {
        xfce_rc_write_bool_entry(xfce_rc, "OverrideBgColor", TRUE);
        xfce_rc_write_entry(xfce_rc, "BgColor", RSTTO_COLOR_TO_STRING(bg_color));
    }
    else
    {
        xfce_rc_write_bool_entry(xfce_rc, "OverrideBgColor", FALSE);
    }
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
