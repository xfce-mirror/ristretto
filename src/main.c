/*
 *  Copyright (c) 2006 Stephan Arts <stephan@xfce.org>
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
#include <gtk/gtk.h>

#include "picture_viewer.h"

static void
cb_rstto_zoom_fit(GtkToolItem *item, RsttoPictureViewer *viewer);
static void
cb_rstto_zoom_100(GtkToolItem *item, RsttoPictureViewer *viewer);
static void
cb_rstto_zoom_in(GtkToolItem *item, RsttoPictureViewer *viewer);
static void
cb_rstto_zoom_out(GtkToolItem *item, RsttoPictureViewer *viewer);

int main(int argc, char **argv)
{
	GdkPixbuf *pixbuf;
	gtk_init(&argc, &argv);

	GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);

	if(argc == 2)
		pixbuf = gdk_pixbuf_new_from_file(argv[1], NULL);
	else
		pixbuf = NULL;


	g_signal_connect(G_OBJECT(window), "destroy", G_CALLBACK(gtk_main_quit), NULL);

	GtkWidget *viewer = rstto_picture_viewer_new();
	GtkWidget *s_window = gtk_scrolled_window_new(NULL,NULL);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(s_window), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	GtkWidget *main_vbox = gtk_vbox_new(0, FALSE);
	GtkWidget *tool_bar = gtk_toolbar_new();

	GtkToolItem *zoom_fit= gtk_tool_button_new_from_stock(GTK_STOCK_ZOOM_FIT);
	GtkToolItem *zoom_100= gtk_tool_button_new_from_stock(GTK_STOCK_ZOOM_100);
	GtkToolItem *zoom_out= gtk_tool_button_new_from_stock(GTK_STOCK_ZOOM_OUT);
	GtkToolItem *zoom_in= gtk_tool_button_new_from_stock(GTK_STOCK_ZOOM_IN);

	rstto_picture_viewer_set_pixbuf(RSTTO_PICTURE_VIEWER(viewer), pixbuf);

	gtk_widget_set_size_request(window, 300, 200);


	gtk_container_add(GTK_CONTAINER(s_window), viewer);

	gtk_box_pack_start(GTK_BOX(main_vbox), s_window, TRUE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(main_vbox), tool_bar, FALSE, TRUE, 0);

	rstto_picture_viewer_set_scale(RSTTO_PICTURE_VIEWER(viewer), 1);

	gtk_toolbar_insert(GTK_TOOLBAR(tool_bar), zoom_fit, 0);
	gtk_toolbar_insert(GTK_TOOLBAR(tool_bar), zoom_100, 0);
	gtk_toolbar_insert(GTK_TOOLBAR(tool_bar), zoom_out, 0);
	gtk_toolbar_insert(GTK_TOOLBAR(tool_bar), zoom_in, 0);

	g_signal_connect(G_OBJECT(zoom_fit), "clicked", G_CALLBACK(cb_rstto_zoom_fit), viewer);
	g_signal_connect(G_OBJECT(zoom_100), "clicked", G_CALLBACK(cb_rstto_zoom_100), viewer);
	g_signal_connect(G_OBJECT(zoom_in), "clicked", G_CALLBACK(cb_rstto_zoom_in), viewer);
	g_signal_connect(G_OBJECT(zoom_out), "clicked", G_CALLBACK(cb_rstto_zoom_out), viewer);

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
