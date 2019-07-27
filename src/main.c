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

#include <xfconf/xfconf.h>
#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>
#include <string.h>

#include <libxfce4ui/libxfce4ui.h>
#include <libxfce4util/libxfce4util.h>
#include <libexif/exif-data.h>

#include "util.h"
#include "file.h"
#include "image_list.h"
#include "settings.h"
#include "main_window.h"
#include "preferences_dialog.h"


gboolean version = FALSE;
gboolean start_fullscreen = FALSE;
gboolean start_slideshow = FALSE;
gboolean show_settings = FALSE;

typedef struct {
    RsttoImageList *image_list;
    gint argc;
    gchar **argv;
    gint iter;
    GtkWidget *window;
} RsttoOpenFiles;

static gboolean
cb_rstto_open_files (RsttoOpenFiles *rof);

static GOptionEntry entries[] =
{
    {    "version", 'V', G_OPTION_FLAG_IN_MAIN, G_OPTION_ARG_NONE, &version,
        N_("Version information"),
        NULL
    },
    {    "fullscreen", 'f', G_OPTION_FLAG_IN_MAIN, G_OPTION_ARG_NONE, &start_fullscreen,
        N_("Start in fullscreen mode"),
        NULL
    },
    {    "slideshow", 's', G_OPTION_FLAG_IN_MAIN, G_OPTION_ARG_NONE, &start_slideshow,
        N_("Start a slideshow"),
        NULL
    },
    { "settings",
            'S',
            G_OPTION_FLAG_IN_MAIN,
            G_OPTION_ARG_NONE,
            &show_settings,
            N_("Show settings dialog"),
        NULL
    },
    { NULL, ' ', 0, 0, NULL, NULL, NULL }
};

int
main(int argc, char **argv)
{
    GError *cli_error = NULL;
    RsttoSettings *settings;
    RsttoImageList *image_list;
    GtkWidget *window;

    #ifdef ENABLE_NLS
    bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);
    bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
    textdomain (GETTEXT_PACKAGE);
    #endif

    #if !GLIB_CHECK_VERSION (2, 32, 0)
    g_thread_init(NULL);
    #endif
    gdk_threads_init();

    if(!gtk_init_with_args(&argc, &argv, "", entries, PACKAGE, &cli_error))
    {
        if (cli_error != NULL)
        {
            g_print (
                    _("%s: %s\n\n"
                      "Try %s --help to see a full list of\n"
                      "available command line options.\n"),
                    PACKAGE,
                    cli_error->message,
                    PACKAGE_NAME);

            g_error_free (cli_error);
            return 1;
        }
    }

    if(version)
    {
        g_print("%s\n", PACKAGE_STRING);
        return 0;
    }

    xfconf_init(NULL);

    gtk_window_set_default_icon_name("ristretto");
    settings = rstto_settings_new();

    if (FALSE == show_settings)
    {
        image_list = rstto_image_list_new ();
        window = rstto_main_window_new (image_list, FALSE);

        if (argc > 1)
        {
            RsttoOpenFiles rof;

            rof.image_list = image_list;
            rof.argc = argc;
            rof.argv = argv;
            rof.iter = 1;
            rof.window = window;

            gdk_threads_add_idle ((GSourceFunc )cb_rstto_open_files, &rof);

            if (TRUE == rstto_settings_get_boolean_property (
                        settings,
                        "maximize-on-startup"))
            {
                gtk_window_maximize (GTK_WINDOW(window));
            }
        }

        /* Start fullscreen */
        if (TRUE == start_fullscreen)
        {
           gtk_window_fullscreen (GTK_WINDOW (window));
        }

        g_signal_connect(G_OBJECT(window), "destroy", G_CALLBACK(gtk_main_quit), NULL);
        gtk_widget_show_all (window);

        GDK_THREADS_ENTER();
        gtk_main();
        GDK_THREADS_LEAVE();

        g_object_unref (image_list);
    }
    else
    {
        window = rstto_preferences_dialog_new (NULL);
        while (gtk_dialog_run (GTK_DIALOG(window)) == GTK_RESPONSE_HELP)
        {
            xfce_dialog_show_help (
                    GTK_WINDOW (window),
                    "ristretto",
                    "preferences",
                    NULL);
        }
        gtk_widget_destroy (window);
    }

    g_object_unref (settings);

    xfconf_shutdown();

    return 0;
}

static gboolean
cb_rstto_open_files (RsttoOpenFiles *rof)
{
    GFileType file_type;
    GFile *file, *p_file;
    GFileInfo *file_info;
    RsttoFile *r_file = NULL;
    RsttoImageListIter *iter = NULL;
    const gchar *content_type;

    if (rof->argc > 2)
    {
        if (rof->iter < rof->argc)
        {
            file = g_file_new_for_commandline_arg (rof->argv[rof->iter]);
            if (file)
            {
                file_info = g_file_query_info (file, "standard::content-type", 0, NULL, NULL);

                if (file_info)
                {
                    content_type = g_file_info_get_attribute_string (file_info, "standard::content-type");

                    if (strncmp (content_type, "image/", 6) == 0)
                    {
                        r_file = rstto_file_new (file);
                        if (rstto_image_list_add_file (rof->image_list, r_file, NULL) == TRUE)
                        {
                            rstto_main_window_add_file_to_recent_files (file);
                        }
                        g_object_unref (r_file);
                        r_file = NULL;
                    }

                }
            }
            rof->iter++;
            return TRUE;
        }
    }
    else
    {
        file = g_file_new_for_commandline_arg (rof->argv[rof->iter]);
        if (file)
        {
            file_info = g_file_query_info (file, "standard::content-type,standard::type", 0, NULL, NULL);
            if (file_info)
            {
                file_type = g_file_info_get_file_type (file_info);
                content_type = g_file_info_get_attribute_string (file_info, "standard::content-type");

                if (strncmp (content_type, "image/", 6) == 0)
                {
                    r_file = rstto_file_new (file);
                }
                else
                {
                    /* TODO: show error dialog */
                }
            }
        }

        /* Get the iterator used by the main-window, it should be
         * set to point to the right file later.
         */
        iter = rstto_main_window_get_iter (RSTTO_MAIN_WINDOW (rof->window));

        if (r_file != NULL && file_type != G_FILE_TYPE_DIRECTORY)
        {
            /* Get the file's parent directory */
            p_file = g_file_get_parent (file);

            /* Open the directory */
            rstto_image_list_set_directory (rof->image_list, p_file, NULL);

            /* This call adds the contents of the
             * directory asynchronously.
             */
            rstto_image_list_add_file (
                    rof->image_list,
                    r_file,
                    NULL);

            /* Point the iterator to the correct image */
            rstto_image_list_iter_find_file (iter, r_file);

            g_object_unref (r_file);
            r_file = NULL;
        }
        else
        {
            /* Open the directory */
            rstto_image_list_set_directory (rof->image_list, file, NULL);

            /* Point the iterator to the correct image */
            rstto_image_list_iter_set_position (iter, 0);
        }

        if (TRUE == start_slideshow)
        {
            rstto_main_window_play_slideshow (RSTTO_MAIN_WINDOW(rof->window));
        }

    }
    return FALSE;
}
