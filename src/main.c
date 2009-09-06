/*
 *  Copyright (c) 2006-2009 Stephan Arts <stephan@xfce.org>
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
#include <string.h>

#include <glib.h>
#include <gio/gio.h>

#include <xfconf/xfconf.h>
#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>
#include <string.h>

#include <libxfce4util/libxfce4util.h>
#include <libexif/exif-data.h>

#include "image.h"
#include "image_list.h"
#include "settings.h"
#include "picture_viewer.h"
#include "main_window.h"


gboolean version = FALSE;
gboolean start_fullscreen = FALSE;
gboolean start_slideshow = FALSE;

typedef struct {
    RsttoImageList *image_list;
    gint argc;
    gchar **argv;
    gint iter;
    RsttoMainWindow *window;
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

    if(!gtk_init_with_args(&argc, &argv, "", entries, PACKAGE, &cli_error))
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

    xfconf_init(NULL);

    gtk_window_set_default_icon_name("ristretto");
    settings = rstto_settings_new();

    image_list = rstto_image_list_new ();
    window = rstto_main_window_new (image_list, FALSE);

    if (argc > 1)
    {
        RsttoOpenFiles rof;

        rof.image_list = image_list;
        rof.argc = argc;
        rof.argv = argv;
    	rof.iter = 1;
        rof.window = RSTTO_MAIN_WINDOW (window);

        g_idle_add ((GSourceFunc )cb_rstto_open_files, &rof);

    }

    g_signal_connect(G_OBJECT(window), "destroy", G_CALLBACK(gtk_main_quit), NULL);
    gtk_widget_show_all (window);

    gtk_main();

    g_object_unref (settings);

    xfconf_shutdown();

    return 0;
}

static gboolean
cb_rstto_open_files (RsttoOpenFiles *rof)
{
    GFile *file;
    GFileInfo *file_info;
    const gchar *content_type;
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
                    rstto_image_list_add_file (rof->image_list, file, NULL);
                }
            }
        }
        rof->iter++;
        return TRUE;
    }
    return FALSE;
}
