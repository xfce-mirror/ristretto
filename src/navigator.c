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
#include <gtk/gtkmarshal.h>
#include <string.h>

#include <thunar-vfs/thunar-vfs.h>

#include "picture_viewer.h"
#include "navigator.h"

static void 
rstto_navigator_init(RsttoNavigator *);
static void
rstto_navigator_class_init(RsttoNavigatorClass *);
static void
rstto_navigator_dispose(GObject *object);

static GObjectClass *parent_class = NULL;

GType
rstto_navigator_get_type ()
{
	static GType rstto_navigator_type = 0;

	if (!rstto_navigator_type)
	{
		static const GTypeInfo rstto_navigator_info = 
		{
			sizeof (RsttoNavigatorClass),
			(GBaseInitFunc) NULL,
			(GBaseFinalizeFunc) NULL,
			(GClassInitFunc) rstto_navigator_class_init,
			(GClassFinalizeFunc) NULL,
			NULL,
			sizeof (RsttoNavigator),
			0,
			(GInstanceInitFunc) rstto_navigator_init,
			NULL
		};

		rstto_navigator_type = g_type_register_static (G_TYPE_OBJECT, "RsttoNavigator", &rstto_navigator_info, 0);
	}
	return rstto_navigator_type;
}

static void
rstto_navigator_init(RsttoNavigator *viewer)
{

}

static void
rstto_navigator_class_init(RsttoNavigatorClass *nav_class)
{
    GObjectClass *object_class = G_OBJECT_CLASS(nav_class);

	parent_class = g_type_class_peek_parent(nav_class);

	object_class->dispose = rstto_navigator_dispose;
}

static void
rstto_navigator_dispose(GObject *object)
{
	RsttoNavigator *navigator = RSTTO_NAVIGATOR(object);

    if(navigator->file_list)
    {
        g_list_foreach(navigator->file_list, (GFunc)thunar_vfs_info_unref, NULL);
        navigator->file_list = NULL;
    }
}

RsttoNavigator *
rstto_navigator_new(RsttoPictureViewer *viewer)
{
	RsttoNavigator *navigator;

	navigator = g_object_new(RSTTO_TYPE_NAVIGATOR, NULL);

    navigator->viewer = viewer;
    navigator->icon_theme = gtk_icon_theme_new();

	return navigator;
}

void
rstto_navigator_set_path(RsttoNavigator *navigator, ThunarVfsPath *path)
{
    if(navigator->path)
    {
        thunar_vfs_path_unref(navigator->path);
        navigator->path = NULL;
    }

    if(navigator->file_list)
    {
        g_list_foreach(navigator->file_list, (GFunc)thunar_vfs_info_unref, NULL);
        navigator->file_list = NULL;
        navigator->file_iter = NULL;
    }

    if(path)
    {
        ThunarVfsInfo *info = thunar_vfs_info_new_for_path(path, NULL);

        if(strcmp(thunar_vfs_mime_info_get_name(info->mime_info), "inode/directory"))
        {
            navigator->path = thunar_vfs_path_get_parent(path);
        }
        else
        {
            thunar_vfs_path_ref(path);
            navigator->path = path;
        }

        gchar *dir_name = thunar_vfs_path_dup_string(navigator->path);

        GDir *dir = g_dir_open(dir_name, 0, NULL);
        const gchar *filename = g_dir_read_name(dir);
        while(filename)
        {
            ThunarVfsPath *file_path = thunar_vfs_path_relative(navigator->path, filename);
            ThunarVfsInfo *file_info = thunar_vfs_info_new_for_path(file_path, NULL);
            if(strcmp(thunar_vfs_mime_info_get_name(file_info->mime_info), "inode/directory"))
            {
                navigator->file_list = g_list_prepend(navigator->file_list, file_info);

                if(thunar_vfs_path_equal(path, file_path))
                {
                    navigator->file_iter = navigator->file_list;
                }
            }
            else
            {
                thunar_vfs_info_unref(file_info);
            }

            thunar_vfs_path_unref(file_path);
            filename = g_dir_read_name(dir);
        }
        g_free(dir_name);
        if(!navigator->file_iter)
        {
            navigator->file_iter = navigator->file_list;
        }
    }

    if(navigator->file_iter)
    {
        gchar *filename = thunar_vfs_path_dup_string(((ThunarVfsInfo *)navigator->file_iter->data)->path);
        GdkPixbuf *pixbuf = gdk_pixbuf_new_from_file(filename , NULL);
        if(!pixbuf)
            pixbuf = gtk_icon_theme_load_icon(navigator->icon_theme, GTK_STOCK_MISSING_IMAGE, 48, 0, NULL);

        rstto_picture_viewer_set_pixbuf(navigator->viewer, pixbuf);
        if(pixbuf)
            gdk_pixbuf_unref(pixbuf);

        g_free(filename);
    }
}

void
rstto_navigator_forward (RsttoNavigator *navigator)
{

    if(navigator->file_iter)
        navigator->file_iter = g_list_next(navigator->file_iter);
    if(!navigator->file_iter)
        navigator->file_iter = g_list_first(navigator->file_list);

    if(navigator->file_iter)
    {
        gchar *filename = thunar_vfs_path_dup_string(((ThunarVfsInfo *)navigator->file_iter->data)->path);
        GdkPixbuf *pixbuf = gdk_pixbuf_new_from_file(filename , NULL);
        if(!pixbuf)
        {
            pixbuf = gtk_icon_theme_load_icon(navigator->icon_theme, GTK_STOCK_MISSING_IMAGE, 48, 0, NULL);
            rstto_picture_viewer_set_scale(navigator->viewer, 1);
        }

        rstto_picture_viewer_set_pixbuf(navigator->viewer, pixbuf);
        if(pixbuf)
            gdk_pixbuf_unref(pixbuf);

        g_free(filename);
    }
}

void
rstto_navigator_back (RsttoNavigator *navigator)
{
    if(navigator->file_iter)
        navigator->file_iter = g_list_previous(navigator->file_iter);
    if(!navigator->file_iter)
        navigator->file_iter = g_list_last(navigator->file_list);

    if(navigator->file_iter)
    {
        gchar *filename = thunar_vfs_path_dup_string(((ThunarVfsInfo *)navigator->file_iter->data)->path);
        GdkPixbuf *pixbuf = gdk_pixbuf_new_from_file(filename , NULL);
        if(!pixbuf)
        {
            pixbuf = gtk_icon_theme_load_icon(navigator->icon_theme, GTK_STOCK_MISSING_IMAGE, 48, 0, NULL);
            rstto_picture_viewer_set_scale(navigator->viewer, 1);
        }

        rstto_picture_viewer_set_pixbuf(navigator->viewer, pixbuf);
        if(pixbuf)
            gdk_pixbuf_unref(pixbuf);

        g_free(filename);
    }
}

