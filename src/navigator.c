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

static gboolean
cb_rstto_navigator_running(RsttoNavigator *navigator);

static GObjectClass *parent_class = NULL;

enum
{
	RSTTO_NAVIGATOR_SIGNAL_FILE_CHANGED = 0,
    RSTTO_NAVIGATOR_SIGNAL_COUNT	
};

struct _RsttoNavigatorEntry
{
    ThunarVfsInfo       *info;
    GdkPixbuf           *pixbuf;
    GdkPixbufRotation    rotation;
    gboolean             h_flipped;
    gboolean             v_flipped;
};

RsttoNavigatorEntry *
_rstto_navigator_entry_new (ThunarVfsInfo *info);
static void
_rstto_navigator_entry_free(RsttoNavigatorEntry *nav_entry);

static gint rstto_navigator_signals[RSTTO_NAVIGATOR_SIGNAL_COUNT];

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

	rstto_navigator_signals[RSTTO_NAVIGATOR_SIGNAL_FILE_CHANGED] = g_signal_new("file_changed",
			G_TYPE_FROM_CLASS(nav_class),
			G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
			0,
			NULL,
			NULL,
			g_cclosure_marshal_VOID__VOID,
			G_TYPE_NONE,
			0,
			NULL);
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
rstto_navigator_set_path(RsttoNavigator *navigator, ThunarVfsPath *path, gboolean index_path)
{
    if(navigator->path)
    {
        thunar_vfs_path_unref(navigator->path);
        navigator->path = NULL;
    }

    if(navigator->file_list)
    {
        g_list_foreach(navigator->file_list, (GFunc)_rstto_navigator_entry_free, NULL);
        navigator->file_list = NULL;
        navigator->file_iter = NULL;
    }

    if(path)
    {
        ThunarVfsInfo *info = thunar_vfs_info_new_for_path(path, NULL);
        if(info)
        {
            if(strcmp(thunar_vfs_mime_info_get_name(info->mime_info), "inode/directory"))
            {
                navigator->path = thunar_vfs_path_get_parent(path);
            }
            else
            {
                thunar_vfs_path_ref(path);
                navigator->path = path;
            }
            thunar_vfs_info_unref(info);
            info = NULL;
        }
        else
        {
            navigator->path = thunar_vfs_path_get_parent(path);
        }

        gchar *dir_name = thunar_vfs_path_dup_string(navigator->path);

        GDir *dir = g_dir_open(dir_name, 0, NULL);
        const gchar *filename = g_dir_read_name(dir);
        while(filename)
        {
            ThunarVfsPath *file_path = thunar_vfs_path_relative(navigator->path, filename);
            ThunarVfsInfo *file_info = thunar_vfs_info_new_for_path(file_path, NULL);
            if(file_info)
            {
                gchar *file_media = thunar_vfs_mime_info_get_media(file_info->mime_info);
                if(!strcmp(file_media, "image"))
                {
                    if(thunar_vfs_path_equal(path, file_path))
                    {
                        RsttoNavigatorEntry *nav_entry = _rstto_navigator_entry_new(file_info);

                        navigator->file_list = g_list_prepend(navigator->file_list, nav_entry);
                        navigator->file_iter = navigator->file_list;
                    }
                    else
                    {
                        if(index_path)
                        {
                            RsttoNavigatorEntry *nav_entry = _rstto_navigator_entry_new(file_info);
                            navigator->file_list = g_list_prepend(navigator->file_list, nav_entry);
                        }
                    }
                }
                else
                {
                    thunar_vfs_info_unref(file_info);
                }
                g_free(file_media);
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
        gchar *filename = thunar_vfs_path_dup_string(((ThunarVfsInfo *)((RsttoNavigatorEntry *)navigator->file_iter->data)->info)->path);
        GdkPixbuf *pixbuf = gdk_pixbuf_new_from_file(filename , NULL);
        if(pixbuf)
        {
            RsttoNavigatorEntry *entry = navigator->file_iter->data;
            GdkPixbuf *new_pixbuf = gdk_pixbuf_rotate_simple(pixbuf, entry->rotation);
            if(new_pixbuf)
            {
                g_object_unref(pixbuf);
                pixbuf = new_pixbuf;
            }
            if(entry->v_flipped)
            {
                new_pixbuf = gdk_pixbuf_flip(pixbuf, FALSE);
                if(new_pixbuf)
                {
                    g_object_unref(pixbuf);
                    pixbuf = new_pixbuf;
                }
            }
            if(entry->h_flipped)
            {
                new_pixbuf = gdk_pixbuf_flip(pixbuf, TRUE);
                if(new_pixbuf)
                {
                    g_object_unref(pixbuf);
                    pixbuf = new_pixbuf;
                }
            }
        }
        if(!pixbuf)
            pixbuf = gtk_icon_theme_load_icon(navigator->icon_theme, GTK_STOCK_MISSING_IMAGE, 48, 0, NULL);

        rstto_picture_viewer_set_pixbuf(navigator->viewer, pixbuf);
        if(pixbuf)
            gdk_pixbuf_unref(pixbuf);

        g_free(filename);
        g_signal_emit(G_OBJECT(navigator), rstto_navigator_signals[RSTTO_NAVIGATOR_SIGNAL_FILE_CHANGED], 0, NULL);
    }
}

void
rstto_navigator_first (RsttoNavigator *navigator)
{
    navigator->file_iter = g_list_first(navigator->file_list);

    if(navigator->file_iter)
    {
        ThunarVfsInfo *info = rstto_navigator_entry_get_info(((RsttoNavigatorEntry *)navigator->file_iter->data));
        gchar *filename = thunar_vfs_path_dup_string(info->path);
        GdkPixbuf *pixbuf = gdk_pixbuf_new_from_file(filename , NULL);
        if(pixbuf)
        {
            RsttoNavigatorEntry *entry = navigator->file_iter->data;
            GdkPixbuf *new_pixbuf = gdk_pixbuf_rotate_simple(pixbuf, entry->rotation);
            if(new_pixbuf)
            {
                g_object_unref(pixbuf);
                pixbuf = new_pixbuf;
            }
            if(entry->v_flipped)
            {
                new_pixbuf = gdk_pixbuf_flip(pixbuf, FALSE);
                if(new_pixbuf)
                {
                    g_object_unref(pixbuf);
                    pixbuf = new_pixbuf;
                }
            }
            if(entry->h_flipped)
            {
                new_pixbuf = gdk_pixbuf_flip(pixbuf, TRUE);
                if(new_pixbuf)
                {
                    g_object_unref(pixbuf);
                    pixbuf = new_pixbuf;
                }
            }
        }
        if(!pixbuf)
        {
            pixbuf = gtk_icon_theme_load_icon(navigator->icon_theme, GTK_STOCK_MISSING_IMAGE, 48, 0, NULL);
            rstto_picture_viewer_set_scale(navigator->viewer, 1);
        }

        rstto_picture_viewer_set_pixbuf(navigator->viewer, pixbuf);
        if(pixbuf)
            gdk_pixbuf_unref(pixbuf);

        g_free(filename);
        g_signal_emit(G_OBJECT(navigator), rstto_navigator_signals[RSTTO_NAVIGATOR_SIGNAL_FILE_CHANGED], 0, NULL);
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
        ThunarVfsInfo *info = rstto_navigator_entry_get_info(((RsttoNavigatorEntry *)navigator->file_iter->data));
        gchar *filename = thunar_vfs_path_dup_string(info->path);
        GdkPixbuf *pixbuf = gdk_pixbuf_new_from_file(filename , NULL);
        if(pixbuf)
        {
            RsttoNavigatorEntry *entry = navigator->file_iter->data;
            GdkPixbuf *new_pixbuf = gdk_pixbuf_rotate_simple(pixbuf, entry->rotation);
            if(new_pixbuf)
            {
                g_object_unref(pixbuf);
                pixbuf = new_pixbuf;
            }
            if(entry->v_flipped)
            {
                new_pixbuf = gdk_pixbuf_flip(pixbuf, FALSE);
                if(new_pixbuf)
                {
                    g_object_unref(pixbuf);
                    pixbuf = new_pixbuf;
                }
            }
            if(entry->h_flipped)
            {
                new_pixbuf = gdk_pixbuf_flip(pixbuf, TRUE);
                if(new_pixbuf)
                {
                    g_object_unref(pixbuf);
                    pixbuf = new_pixbuf;
                }
            }
        }
        if(!pixbuf)
        {
            pixbuf = gtk_icon_theme_load_icon(navigator->icon_theme, GTK_STOCK_MISSING_IMAGE, 48, 0, NULL);
            rstto_picture_viewer_set_scale(navigator->viewer, 1);
        }

        rstto_picture_viewer_set_pixbuf(navigator->viewer, pixbuf);
        if(pixbuf)
            gdk_pixbuf_unref(pixbuf);

        g_free(filename);
        g_signal_emit(G_OBJECT(navigator), rstto_navigator_signals[RSTTO_NAVIGATOR_SIGNAL_FILE_CHANGED], 0, NULL);
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
        ThunarVfsInfo *info = rstto_navigator_entry_get_info(((RsttoNavigatorEntry *)navigator->file_iter->data));
        gchar *filename = thunar_vfs_path_dup_string(info->path);
        GdkPixbuf *pixbuf = gdk_pixbuf_new_from_file(filename , NULL);
        if(pixbuf)
        {
            RsttoNavigatorEntry *entry = navigator->file_iter->data;
            GdkPixbuf *new_pixbuf = gdk_pixbuf_rotate_simple(pixbuf, entry->rotation);
            if(new_pixbuf)
            {
                g_object_unref(pixbuf);
                pixbuf = new_pixbuf;
            }
            if(entry->v_flipped)
            {
                new_pixbuf = gdk_pixbuf_flip(pixbuf, FALSE);
                if(new_pixbuf)
                {
                    g_object_unref(pixbuf);
                    pixbuf = new_pixbuf;
                }
            }
            if(entry->h_flipped)
            {
                new_pixbuf = gdk_pixbuf_flip(pixbuf, TRUE);
                if(new_pixbuf)
                {
                    g_object_unref(pixbuf);
                    pixbuf = new_pixbuf;
                }
            }
        }
        if(!pixbuf)
        {
            pixbuf = gtk_icon_theme_load_icon(navigator->icon_theme, GTK_STOCK_MISSING_IMAGE, 48, 0, NULL);
            rstto_picture_viewer_set_scale(navigator->viewer, 1);
        }

        rstto_picture_viewer_set_pixbuf(navigator->viewer, pixbuf);
        if(pixbuf)
            gdk_pixbuf_unref(pixbuf);

        g_free(filename);
        g_signal_emit(G_OBJECT(navigator), rstto_navigator_signals[RSTTO_NAVIGATOR_SIGNAL_FILE_CHANGED], 0, NULL);
    }
}

void
rstto_navigator_last (RsttoNavigator *navigator)
{
    navigator->file_iter = g_list_last(navigator->file_list);

    if(navigator->file_iter)
    {
        ThunarVfsInfo *info = rstto_navigator_entry_get_info(((RsttoNavigatorEntry *)navigator->file_iter->data));
        gchar *filename = thunar_vfs_path_dup_string(info->path);
        GdkPixbuf *pixbuf = gdk_pixbuf_new_from_file(filename , NULL);
        if(pixbuf)
        {
            RsttoNavigatorEntry *entry = navigator->file_iter->data;
            GdkPixbuf *new_pixbuf = gdk_pixbuf_rotate_simple(pixbuf, entry->rotation);
            if(new_pixbuf)
            {
                g_object_unref(pixbuf);
                pixbuf = new_pixbuf;
            }
            if(entry->v_flipped)
            {
                new_pixbuf = gdk_pixbuf_flip(pixbuf, FALSE);
                if(new_pixbuf)
                {
                    g_object_unref(pixbuf);
                    pixbuf = new_pixbuf;
                }
            }
            if(entry->h_flipped)
            {
                new_pixbuf = gdk_pixbuf_flip(pixbuf, TRUE);
                if(new_pixbuf)
                {
                    g_object_unref(pixbuf);
                    pixbuf = new_pixbuf;
                }
            }
        }
        if(!pixbuf)
        {
            pixbuf = gtk_icon_theme_load_icon(navigator->icon_theme, GTK_STOCK_MISSING_IMAGE, 48, 0, NULL);
            rstto_picture_viewer_set_scale(navigator->viewer, 1);
        }

        rstto_picture_viewer_set_pixbuf(navigator->viewer, pixbuf);
        if(pixbuf)
            gdk_pixbuf_unref(pixbuf);

        g_free(filename);
        g_signal_emit(G_OBJECT(navigator), rstto_navigator_signals[RSTTO_NAVIGATOR_SIGNAL_FILE_CHANGED], 0, NULL);
    }
}

void
rstto_navigator_set_running (RsttoNavigator *navigator, gboolean running)
{
    if(!navigator->running)
    {
        navigator->running = running;
        if(!navigator->id)
            navigator->id = g_timeout_add(5000, (GSourceFunc)cb_rstto_navigator_running, navigator);
    }
    else
    {
        navigator->running = running;
    }
}

RsttoNavigatorEntry *
rstto_navigator_get_file (RsttoNavigator *navigator)
{
    if(navigator->file_iter)
        return (RsttoNavigatorEntry *)(navigator->file_iter->data);
    else
        return NULL;
}

gint
rstto_navigator_get_n_files (RsttoNavigator *navigator)
{
    return g_list_length(navigator->file_list);
}

RsttoNavigatorEntry *
rstto_navigator_get_nth_file (RsttoNavigator *navigator, gint n)
{
    return g_list_nth_data(navigator->file_list, n);
}

void
rstto_navigator_set_file (RsttoNavigator *navigator, gint n)
{
    navigator->file_iter = g_list_nth(navigator->file_list, n);
    if(navigator->file_iter)
    {
        ThunarVfsInfo *info = rstto_navigator_entry_get_info(((RsttoNavigatorEntry *)navigator->file_iter->data));
        gchar *filename = thunar_vfs_path_dup_string(info->path);
        GdkPixbuf *pixbuf = gdk_pixbuf_new_from_file(filename , NULL);
        if(pixbuf)
        {
            RsttoNavigatorEntry *entry = navigator->file_iter->data;
            GdkPixbuf *new_pixbuf = gdk_pixbuf_rotate_simple(pixbuf, entry->rotation);
            if(new_pixbuf)
            {
                g_object_unref(pixbuf);
                pixbuf = new_pixbuf;
            }
            if(entry->v_flipped)
            {
                new_pixbuf = gdk_pixbuf_flip(pixbuf, FALSE);
                if(new_pixbuf)
                {
                    g_object_unref(pixbuf);
                    pixbuf = new_pixbuf;
                }
            }
            if(entry->h_flipped)
            {
                new_pixbuf = gdk_pixbuf_flip(pixbuf, TRUE);
                if(new_pixbuf)
                {
                    g_object_unref(pixbuf);
                    pixbuf = new_pixbuf;
                }
            }
        }
        if(!pixbuf)
        {
            pixbuf = gtk_icon_theme_load_icon(navigator->icon_theme, GTK_STOCK_MISSING_IMAGE, 48, 0, NULL);
            rstto_picture_viewer_set_scale(navigator->viewer, 1);
        }

        rstto_picture_viewer_set_pixbuf(navigator->viewer, pixbuf);
        if(pixbuf)
            gdk_pixbuf_unref(pixbuf);

        g_free(filename);
        g_signal_emit(G_OBJECT(navigator), rstto_navigator_signals[RSTTO_NAVIGATOR_SIGNAL_FILE_CHANGED], 0, NULL);
    }
}

RsttoNavigatorEntry *
_rstto_navigator_entry_new (ThunarVfsInfo *info)
{
    RsttoNavigatorEntry *entry = NULL;
    gchar *filename = thunar_vfs_path_dup_string(info->path);
    if(filename)
    {
        entry = g_new0(RsttoNavigatorEntry, 1);

        entry->info = info;
        entry->pixbuf = gdk_pixbuf_new_from_file_at_size(filename, 64, 64, NULL);

        g_free(filename);
    }
    return entry;
}

ThunarVfsInfo *
rstto_navigator_entry_get_info (RsttoNavigatorEntry *entry)
{
    return entry->info;
}

GdkPixbuf *
rstto_navigator_entry_get_thumbnail (RsttoNavigatorEntry *entry)
{
    return entry->pixbuf;
}

GdkPixbufRotation
rstto_navigator_entry_get_rotation (RsttoNavigatorEntry *entry)
{
    return entry->rotation;
}

gboolean
rstto_navigator_entry_get_flip (RsttoNavigatorEntry *entry, gboolean horizontal)
{
    if (horizontal)
        return entry->h_flipped;
    else
        return entry->v_flipped;
}

void
rstto_navigator_flip_entry(RsttoNavigator *navigator, RsttoNavigatorEntry *entry, gboolean horizontal)
{
    if (horizontal)
    {
        entry->h_flipped = !entry->h_flipped;
    }
    else
    {
        entry->v_flipped = !entry->v_flipped;
    }

    if(entry == navigator->file_iter->data)
    {
        ThunarVfsInfo *info = rstto_navigator_entry_get_info(((RsttoNavigatorEntry *)navigator->file_iter->data));
        gchar *filename = thunar_vfs_path_dup_string(info->path);
        GdkPixbuf *pixbuf = gdk_pixbuf_new_from_file(filename , NULL);
        if(pixbuf)
        {
            GdkPixbuf *new_pixbuf = gdk_pixbuf_rotate_simple(pixbuf, entry->rotation);
            if(new_pixbuf)
            {
                g_object_unref(pixbuf);
                pixbuf = new_pixbuf;
            }
            if(entry->v_flipped)
            {
                new_pixbuf = gdk_pixbuf_flip(pixbuf, FALSE);
                if(new_pixbuf)
                {
                    g_object_unref(pixbuf);
                    pixbuf = new_pixbuf;
                }
            }
            if(entry->h_flipped)
            {
                new_pixbuf = gdk_pixbuf_flip(pixbuf, TRUE);
                if(new_pixbuf)
                {
                    g_object_unref(pixbuf);
                    pixbuf = new_pixbuf;
                }
            }
        }
        if(!pixbuf)
        {
            pixbuf = gtk_icon_theme_load_icon(navigator->icon_theme, GTK_STOCK_MISSING_IMAGE, 48, 0, NULL);
            rstto_picture_viewer_set_scale(navigator->viewer, 1);
        }

        rstto_picture_viewer_set_pixbuf(navigator->viewer, pixbuf);
        if(pixbuf)
            gdk_pixbuf_unref(pixbuf);

        g_free(filename);
        g_signal_emit(G_OBJECT(navigator), rstto_navigator_signals[RSTTO_NAVIGATOR_SIGNAL_FILE_CHANGED], 0, NULL);
    }
}

void
rstto_navigator_set_entry_rotation (RsttoNavigator *navigator, RsttoNavigatorEntry *entry, GdkPixbufRotation rotation)
{
    entry->rotation = rotation;
    if(entry == navigator->file_iter->data)
    {
        ThunarVfsInfo *info = rstto_navigator_entry_get_info(((RsttoNavigatorEntry *)navigator->file_iter->data));
        gchar *filename = thunar_vfs_path_dup_string(info->path);
        GdkPixbuf *pixbuf = gdk_pixbuf_new_from_file(filename , NULL);
        if(pixbuf)
        {
            GdkPixbuf *new_pixbuf = gdk_pixbuf_rotate_simple(pixbuf, entry->rotation);
            if(new_pixbuf)
            {
                g_object_unref(pixbuf);
                pixbuf = new_pixbuf;
            }
            if(entry->v_flipped)
            {
                new_pixbuf = gdk_pixbuf_flip(pixbuf, FALSE);
                if(new_pixbuf)
                {
                    g_object_unref(pixbuf);
                    pixbuf = new_pixbuf;
                }
            }
            if(entry->h_flipped)
            {
                new_pixbuf = gdk_pixbuf_flip(pixbuf, TRUE);
                if(new_pixbuf)
                {
                    g_object_unref(pixbuf);
                    pixbuf = new_pixbuf;
                }
            }
        }
        if(!pixbuf)
        {
            pixbuf = gtk_icon_theme_load_icon(navigator->icon_theme, GTK_STOCK_MISSING_IMAGE, 48, 0, NULL);
            rstto_picture_viewer_set_scale(navigator->viewer, 1);
        }

        rstto_picture_viewer_set_pixbuf(navigator->viewer, pixbuf);
        if(pixbuf)
            gdk_pixbuf_unref(pixbuf);

        g_free(filename);
        g_signal_emit(G_OBJECT(navigator), rstto_navigator_signals[RSTTO_NAVIGATOR_SIGNAL_FILE_CHANGED], 0, NULL);
    }
}

static void
_rstto_navigator_entry_free(RsttoNavigatorEntry *nav_entry)
{
    thunar_vfs_info_unref(nav_entry->info);
    if(nav_entry->pixbuf)
        g_object_unref(nav_entry->pixbuf);
    g_free(nav_entry);
}

static gboolean
cb_rstto_navigator_running(RsttoNavigator *navigator)
{
    if(navigator->running)
    {
        rstto_navigator_forward(navigator);
    }
    else
        navigator->id = 0;
    return navigator->running;
}
