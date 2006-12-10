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

int main(int argc, char **argv)
{
	gtk_init(&argc, &argv);

	GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);

	g_signal_connect(G_OBJECT(window), "destroy", G_CALLBACK(gtk_main_quit), NULL);

	GtkWidget *viewer = rstto_picture_viewer_new();

	gtk_container_add(GTK_CONTAINER(window), viewer);

	gtk_widget_set_size_request(window, 400,300);
	gtk_widget_show_all(window);

	gtk_main();
	return 0;
}
