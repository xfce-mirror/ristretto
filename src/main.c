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
#include <string.h>

#include <thunar-vfs/thunar-vfs.h>

#include "picture_viewer.h"

static ThunarVfsMimeDatabase *mime_dbase = NULL;

static gboolean playing = FALSE;

static void
cb_rstto_zoom_fit(GtkToolItem *item, RsttoPictureViewer *viewer);
static void
cb_rstto_zoom_100(GtkToolItem *item, RsttoPictureViewer *viewer);
static void
cb_rstto_zoom_in(GtkToolItem *item, RsttoPictureViewer *viewer);
static void
cb_rstto_zoom_out(GtkToolItem *item, RsttoPictureViewer *viewer);

static void
cb_rstto_previous(GtkToolItem *item, RsttoPictureViewer *viewer);
static void
cb_rstto_forward(GtkToolItem *item, RsttoPictureViewer *viewer);
static void
cb_rstto_play(GtkToolItem *item, RsttoPictureViewer *viewer);

static gboolean
play_forward(RsttoPictureViewer *viewer);

static void
cb_rstto_open(GtkToolItem *item, RsttoPictureViewer *viewer);

int main(int argc, char **argv)
{
    ThunarVfsPath *path = NULL;

	#ifdef ENABLE_NLS
	bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);
 	bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
 	textdomain (GETTEXT_PACKAGE);
	#endif

	gtk_init(&argc, &argv);

	thunar_vfs_init();

	mime_dbase = thunar_vfs_mime_database_get_default();

	GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_window_set_title(GTK_WINDOW(window), PACKAGE_STRING);

	if(argc == 2)
        path = thunar_vfs_path_new(argv[1], NULL);


	g_signal_connect(G_OBJECT(window), "destroy", G_CALLBACK(gtk_main_quit), NULL);

	GtkWidget *viewer = rstto_picture_viewer_new();
	GtkWidget *s_window = gtk_scrolled_window_new(NULL,NULL);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(s_window), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	GtkWidget *main_vbox = gtk_vbox_new(0, FALSE);
	GtkWidget *main_hbox = gtk_hbox_new(0, FALSE);
    GtkWidget *menu_bar = gtk_menu_bar_new();
	GtkWidget *image_tool_bar = gtk_toolbar_new();
	GtkWidget *app_tool_bar = gtk_toolbar_new();
    GtkWidget *status_bar = gtk_statusbar_new();

    GtkWidget *menu_item_file = gtk_menu_item_new_with_mnemonic(_("_File"));
    gtk_menu_shell_append(GTK_MENU_SHELL(menu_bar), menu_item_file);

	GtkToolItem *zoom_fit= gtk_tool_button_new_from_stock(GTK_STOCK_ZOOM_FIT);
	GtkToolItem *zoom_100= gtk_tool_button_new_from_stock(GTK_STOCK_ZOOM_100);
	GtkToolItem *zoom_out= gtk_tool_button_new_from_stock(GTK_STOCK_ZOOM_OUT);
	GtkToolItem *zoom_in = gtk_tool_button_new_from_stock(GTK_STOCK_ZOOM_IN);
	GtkToolItem *forward = gtk_tool_button_new_from_stock(GTK_STOCK_MEDIA_FORWARD);
	GtkToolItem *play = gtk_tool_button_new_from_stock(GTK_STOCK_MEDIA_PLAY);
	GtkToolItem *previous = gtk_tool_button_new_from_stock(GTK_STOCK_MEDIA_REWIND);
	GtkToolItem *open = gtk_tool_button_new_from_stock(GTK_STOCK_OPEN);
	GtkToolItem *spacer = gtk_tool_item_new();
	GtkToolItem *separator = gtk_separator_tool_item_new();


	gtk_tool_item_set_expand(spacer, TRUE);
	gtk_tool_item_set_homogeneous(spacer, FALSE);

	rstto_picture_viewer_set_path(RSTTO_PICTURE_VIEWER(viewer), path);

	gtk_widget_set_size_request(window, 300, 200);


	gtk_container_add(GTK_CONTAINER(s_window), viewer);
    gtk_toolbar_set_orientation(GTK_TOOLBAR(image_tool_bar), GTK_ORIENTATION_VERTICAL);
	gtk_box_pack_start(GTK_BOX(main_hbox), image_tool_bar, FALSE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(main_hbox), s_window, TRUE, TRUE, 0);

	gtk_box_pack_start(GTK_BOX(main_vbox), menu_bar, FALSE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(main_vbox), app_tool_bar, FALSE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(main_vbox), main_hbox, TRUE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(main_vbox), status_bar, FALSE, TRUE, 0);

	rstto_picture_viewer_set_scale(RSTTO_PICTURE_VIEWER(viewer), 1);

	gtk_toolbar_insert(GTK_TOOLBAR(image_tool_bar), zoom_fit, 0);
	gtk_toolbar_insert(GTK_TOOLBAR(image_tool_bar), zoom_100, 0);
	gtk_toolbar_insert(GTK_TOOLBAR(image_tool_bar), zoom_out, 0);
	gtk_toolbar_insert(GTK_TOOLBAR(image_tool_bar), zoom_in, 0);
	//gtk_toolbar_insert(GTK_TOOLBAR(image_tool_bar), spacer, 0);
	gtk_toolbar_insert(GTK_TOOLBAR(app_tool_bar), forward, 0);
	gtk_toolbar_insert(GTK_TOOLBAR(app_tool_bar), play, 0);
	gtk_toolbar_insert(GTK_TOOLBAR(app_tool_bar), previous, 0);
	gtk_toolbar_insert(GTK_TOOLBAR(app_tool_bar), separator, 0);
	gtk_toolbar_insert(GTK_TOOLBAR(app_tool_bar), open, 0);

	g_signal_connect(G_OBJECT(zoom_fit), "clicked", G_CALLBACK(cb_rstto_zoom_fit), viewer);
	g_signal_connect(G_OBJECT(zoom_100), "clicked", G_CALLBACK(cb_rstto_zoom_100), viewer);
	g_signal_connect(G_OBJECT(zoom_in), "clicked", G_CALLBACK(cb_rstto_zoom_in), viewer);
	g_signal_connect(G_OBJECT(zoom_out), "clicked", G_CALLBACK(cb_rstto_zoom_out), viewer);
	g_signal_connect(G_OBJECT(forward), "clicked", G_CALLBACK(cb_rstto_forward), viewer);
	g_signal_connect(G_OBJECT(play), "clicked", G_CALLBACK(cb_rstto_play), viewer);
	g_signal_connect(G_OBJECT(previous), "clicked", G_CALLBACK(cb_rstto_previous), viewer);
	g_signal_connect(G_OBJECT(open), "clicked", G_CALLBACK(cb_rstto_open), viewer);

	gtk_container_add(GTK_CONTAINER(window), main_vbox);

	gtk_widget_show_all(window);
	gtk_widget_show(viewer);


	gtk_main();
	return 0;
}

static void
cb_rstto_zoom_fit(GtkToolItem *item, RsttoPictureViewer *viewer)
{
	rstto_picture_viewer_fit_scale(viewer);
}

static void
cb_rstto_zoom_100(GtkToolItem *item, RsttoPictureViewer *viewer)
{
	rstto_picture_viewer_set_scale(viewer, 1);
}

static void
cb_rstto_zoom_in(GtkToolItem *item, RsttoPictureViewer *viewer)
{
	gdouble scale = rstto_picture_viewer_get_scale(viewer);
	rstto_picture_viewer_set_scale(viewer, scale*1.2);
}

static void
cb_rstto_zoom_out(GtkToolItem *item, RsttoPictureViewer *viewer)
{
	gdouble scale = rstto_picture_viewer_get_scale(viewer);
	rstto_picture_viewer_set_scale(viewer, scale/1.2);
}

static void
cb_rstto_open(GtkToolItem *item, RsttoPictureViewer *viewer)
{
	GtkWidget *window = gtk_widget_get_toplevel(GTK_WIDGET(item));

	GtkWidget *dialog = gtk_file_chooser_dialog_new(_("Open image"),
	                                                GTK_WINDOW(window),
	                                                GTK_FILE_CHOOSER_ACTION_OPEN,
	                                                GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
	                                                GTK_STOCK_OPEN, GTK_RESPONSE_OK,
	                                                NULL);

	gint response = gtk_dialog_run(GTK_DIALOG(dialog));
	if(response == GTK_RESPONSE_OK)
	{
		const gchar *filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));

		ThunarVfsPath *path = thunar_vfs_path_new(filename, NULL);

		rstto_picture_viewer_set_path(RSTTO_PICTURE_VIEWER(viewer), path);

	}

	gtk_widget_destroy(dialog);
}

static void
cb_rstto_forward(GtkToolItem *item, RsttoPictureViewer *viewer)
{
    rstto_picture_viewer_forward(viewer);
}

static void
cb_rstto_previous(GtkToolItem *item, RsttoPictureViewer *viewer)
{
    rstto_picture_viewer_reverse(viewer);
}

static void
cb_rstto_play(GtkToolItem *item, RsttoPictureViewer *viewer)
{
	if(playing == TRUE)
	{
		gtk_tool_button_set_stock_id(GTK_TOOL_BUTTON(item), GTK_STOCK_MEDIA_PLAY);
		playing = FALSE;
	}
	else
	{
		gtk_tool_button_set_stock_id(GTK_TOOL_BUTTON(item), GTK_STOCK_MEDIA_PAUSE);
		g_timeout_add(5000, (GSourceFunc)play_forward, viewer);
		playing = TRUE;
	}
}

static gboolean
play_forward(RsttoPictureViewer *viewer)
{
	if(playing == TRUE)
		cb_rstto_forward(NULL, viewer);
	return playing;
}
