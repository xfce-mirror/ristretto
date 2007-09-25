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

#include "navigator.h"
#include "picture_viewer.h"
#include "thumbnail_viewer.h"
#include "main_window.h"

static ThunarVfsMimeDatabase *mime_dbase = NULL;
static GtkIconTheme *icon_theme = NULL;

static GtkRecentManager *recent_manager;
static XfceRc *xfce_rc;

int main(int argc, char **argv)
{


    #ifdef ENABLE_NLS
    bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);
    bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
    textdomain (GETTEXT_PACKAGE);
    #endif

    gtk_init(&argc, &argv);

    thunar_vfs_init();

    mime_dbase = thunar_vfs_mime_database_get_default();
    icon_theme = gtk_icon_theme_get_default();

    gtk_window_set_default_icon_name("ristretto");
    recent_manager = gtk_recent_manager_get_default();
    xfce_rc = xfce_rc_config_open(XFCE_RESOURCE_CONFIG, "ristretto/ristrettorc", FALSE);
    
    GtkWidget *window = rstto_main_window_new();


    g_signal_connect(G_OBJECT(window), "destroy", G_CALLBACK(gtk_main_quit), NULL);


    gtk_widget_show_all(window);
    rstto_main_window_set_thumbnail_viewer_visibility(RSTTO_MAIN_WINDOW(window), TRUE);

    gtk_main();
    xfce_rc_flush(xfce_rc);
    xfce_rc_close(xfce_rc);
    return 0;
}
