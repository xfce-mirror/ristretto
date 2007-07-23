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
static ThunarVfsPath *working_dir = NULL;
static GList *file_list = NULL;
static GList *file_iter = NULL;

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
	GdkPixbuf *pixbuf = NULL;
    GtkWidget *dialog = NULL;
    GError *error = NULL;
    gchar *dir_name = NULL;


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
	{
        ThunarVfsPath *path = thunar_vfs_path_new(argv[1], NULL);
        ThunarVfsInfo *info = thunar_vfs_info_new_for_path(path, NULL);

        if(info)
        {
            g_debug("%s\n", thunar_vfs_mime_info_get_name(info->mime_info));
            g_debug("%s\n", thunar_vfs_mime_info_get_comment(info->mime_info));
            if(strcmp(thunar_vfs_mime_info_get_name(info->mime_info), "inode/directory"))
            {
                working_dir = thunar_vfs_path_get_parent(path);
            }
            else
            {
                working_dir = path;
            }
        }
        else
        {
            working_dir = path;
        }
        thunar_vfs_path_ref(working_dir);

        dir_name = thunar_vfs_path_dup_string(working_dir);

        GDir *dir = g_dir_open(dir_name, 0, NULL);
        const gchar *filename = g_dir_read_name(dir);
        while(filename)
        {
            ThunarVfsMimeInfo *mime_info = thunar_vfs_mime_database_get_info_for_name(mime_dbase, filename);
            if(!strcmp(thunar_vfs_mime_info_get_media(mime_info), "image"))
            {
                file_list = g_list_prepend(file_list, thunar_vfs_path_relative(working_dir, filename));
                if(thunar_vfs_path_equal(path, file_list->data))
                {
                    pixbuf = gdk_pixbuf_new_from_file(argv[1], &error);
                    file_iter = file_list;
                }
            }
            thunar_vfs_mime_info_unref(mime_info);
            filename = g_dir_read_name(dir);
        }
        g_free(dir_name);
        if(error)
        {
            dialog = gtk_message_dialog_new_with_markup(GTK_WINDOW(window),
                                            GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                            GTK_MESSAGE_ERROR,
                                            GTK_BUTTONS_OK,
                                            "<b>Error:</b>\n%s",
                                            error->message);
        }
        if(!file_iter)
            file_list = file_list;
	}
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
	GtkToolItem *zoom_in = gtk_tool_button_new_from_stock(GTK_STOCK_ZOOM_IN);
	GtkToolItem *separator = gtk_separator_tool_item_new();
	GtkToolItem *forward = gtk_tool_button_new_from_stock(GTK_STOCK_MEDIA_FORWARD);
	GtkToolItem *play = gtk_tool_button_new_from_stock(GTK_STOCK_MEDIA_PLAY);
	GtkToolItem *previous = gtk_tool_button_new_from_stock(GTK_STOCK_MEDIA_REWIND);
	GtkToolItem *open = gtk_tool_button_new_from_stock(GTK_STOCK_OPEN);
	GtkToolItem *spacer = gtk_tool_item_new();


	gtk_tool_item_set_expand(spacer, TRUE);
	gtk_tool_item_set_homogeneous(spacer, FALSE);

	rstto_picture_viewer_set_pixbuf(RSTTO_PICTURE_VIEWER(viewer), pixbuf);

	if(pixbuf)
		g_object_unref(pixbuf);

	gtk_widget_set_size_request(window, 300, 200);


	gtk_container_add(GTK_CONTAINER(s_window), viewer);

	gtk_box_pack_start(GTK_BOX(main_vbox), s_window, TRUE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(main_vbox), tool_bar, FALSE, TRUE, 0);

	rstto_picture_viewer_set_scale(RSTTO_PICTURE_VIEWER(viewer), 1);

	gtk_toolbar_insert(GTK_TOOLBAR(tool_bar), zoom_fit, 0);
	gtk_toolbar_insert(GTK_TOOLBAR(tool_bar), zoom_100, 0);
	gtk_toolbar_insert(GTK_TOOLBAR(tool_bar), zoom_out, 0);
	gtk_toolbar_insert(GTK_TOOLBAR(tool_bar), zoom_in, 0);
	gtk_toolbar_insert(GTK_TOOLBAR(tool_bar), separator, 0);
	gtk_toolbar_insert(GTK_TOOLBAR(tool_bar), forward, 0);
	gtk_toolbar_insert(GTK_TOOLBAR(tool_bar), play, 0);
	gtk_toolbar_insert(GTK_TOOLBAR(tool_bar), previous, 0);
	gtk_toolbar_insert(GTK_TOOLBAR(tool_bar), spacer, 0);
	gtk_toolbar_insert(GTK_TOOLBAR(tool_bar), open, 0);

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

    if(dialog)
    {
        gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);
    }

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
	GdkPixbuf *pixbuf = NULL;
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
		if(working_dir)
			thunar_vfs_path_unref(working_dir);
		working_dir = thunar_vfs_path_get_parent(path);
		thunar_vfs_path_ref(working_dir);
		thunar_vfs_path_unref(path);

		g_list_foreach(file_list, (GFunc)thunar_vfs_path_unref, NULL);
		g_list_free(file_list);
		file_list = file_iter = NULL;

		gchar *dir_name = thunar_vfs_path_dup_string(working_dir);

		GDir *dir = g_dir_open(dir_name, 0, NULL);
		const gchar *_filename = g_dir_read_name(dir);
		while(_filename)
		{
			ThunarVfsMimeInfo *mime_info = thunar_vfs_mime_database_get_info_for_name(mime_dbase, _filename);
			if(!strcmp(thunar_vfs_mime_info_get_media(mime_info), "image"))
			{
				file_list = g_list_prepend(file_list, thunar_vfs_path_relative(working_dir, _filename));
				if(!strcmp(thunar_vfs_path_get_name(THUNAR_VFS_PATH(file_list->data)), _filename))
					file_iter = file_list;
			}
			thunar_vfs_mime_info_unref(mime_info);
			_filename = g_dir_read_name(dir);
		}
		g_free(dir_name);

		pixbuf = gdk_pixbuf_new_from_file(filename , NULL);

		rstto_picture_viewer_set_pixbuf(RSTTO_PICTURE_VIEWER(viewer), pixbuf);

		g_object_unref(pixbuf);
	}

	gtk_widget_destroy(dialog);
}

static void
cb_rstto_forward(GtkToolItem *item, RsttoPictureViewer *viewer)
{
	GdkPixbuf *pixbuf = NULL;
    GError *error = NULL;

	if(file_iter)
		file_iter = g_list_next(file_iter);
	if(!file_iter)
		file_iter = file_list;

	while(!pixbuf && file_iter)
	{
		gchar *filename = thunar_vfs_path_dup_string(file_iter->data);

		pixbuf = gdk_pixbuf_new_from_file(filename , &error);
        if(!pixbuf)
        {
            g_debug("%s\n", error->message);
            GList *_file_iter = g_list_next(file_iter);

            thunar_vfs_path_unref(file_iter->data);
            file_list = g_list_remove(file_list, file_iter->data);
            if(_file_iter)
                file_iter = _file_iter;
            else
		        file_iter = file_list;
        }

		rstto_picture_viewer_set_pixbuf(RSTTO_PICTURE_VIEWER(viewer), pixbuf);

		if(pixbuf)
			g_object_unref(pixbuf);
		g_free(filename);
	}
}

static void
cb_rstto_previous(GtkToolItem *item, RsttoPictureViewer *viewer)
{
	GdkPixbuf *pixbuf;
	if(file_iter)
		file_iter = g_list_previous(file_iter);
	if(!file_iter)
		file_iter = g_list_last(file_list);

	if(file_iter)
	{
		gchar *filename = thunar_vfs_path_dup_string(file_iter->data);

		pixbuf = gdk_pixbuf_new_from_file(filename , NULL);

		rstto_picture_viewer_set_pixbuf(RSTTO_PICTURE_VIEWER(viewer), pixbuf);

		if(pixbuf)
			g_object_unref(pixbuf);
		g_free(filename);
	}
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
