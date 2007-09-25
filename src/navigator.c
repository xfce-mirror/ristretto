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

static gint
rstto_navigator_entry_name_compare_func(RsttoNavigatorEntry *a, RsttoNavigatorEntry *b);

static void
rstto_navigator_entry_free_pixbuf (RsttoNavigatorEntry *entry);

enum
{
    RSTTO_NAVIGATOR_SIGNAL_ENTRY_MODIFIED = 0,
    RSTTO_NAVIGATOR_SIGNAL_NEW_ENTRY,
    RSTTO_NAVIGATOR_SIGNAL_ITER_CHANGED,
    RSTTO_NAVIGATOR_SIGNAL_REORDERED,
    RSTTO_NAVIGATOR_SIGNAL_COUNT    
};

struct _RsttoNavigatorEntry
{
    ThunarVfsInfo       *info;
    GdkPixbuf           *thumb;
    GdkPixbuf           *pixbuf;
    gdouble              scale;
    gboolean             fit_to_screen;
    GdkPixbufRotation    rotation;
    gboolean             h_flipped;
    gboolean             v_flipped;
};


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
rstto_navigator_init(RsttoNavigator *navigator)
{
    navigator->file_iter = NULL;
    navigator->compare_func = (GCompareFunc)rstto_navigator_entry_name_compare_func;
    navigator->old_position = -1;
    navigator->timeout = 5000;
}

static void
rstto_navigator_class_init(RsttoNavigatorClass *nav_class)
{
    GObjectClass *object_class = G_OBJECT_CLASS(nav_class);

    parent_class = g_type_class_peek_parent(nav_class);

    object_class->dispose = rstto_navigator_dispose;

    rstto_navigator_signals[RSTTO_NAVIGATOR_SIGNAL_REORDERED] = g_signal_new("reordered",
            G_TYPE_FROM_CLASS(nav_class),
            G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
            0,
            NULL,
            NULL,
            g_cclosure_marshal_VOID__VOID,
            G_TYPE_NONE,
            0,
            NULL);
    rstto_navigator_signals[RSTTO_NAVIGATOR_SIGNAL_ENTRY_MODIFIED] = g_signal_new("entry-modified",
            G_TYPE_FROM_CLASS(nav_class),
            G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
            0,
            NULL,
            NULL,
            g_cclosure_marshal_VOID__UINT_POINTER,
            G_TYPE_NONE,
            2,
            G_TYPE_UINT,
            G_TYPE_POINTER,
            NULL);
    rstto_navigator_signals[RSTTO_NAVIGATOR_SIGNAL_ITER_CHANGED] = g_signal_new("iter-changed",
            G_TYPE_FROM_CLASS(nav_class),
            G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
            0,
            NULL,
            NULL,
            g_cclosure_marshal_VOID__UINT_POINTER,
            G_TYPE_NONE,
            2,
            G_TYPE_UINT,
            G_TYPE_POINTER,
            NULL);
    rstto_navigator_signals[RSTTO_NAVIGATOR_SIGNAL_NEW_ENTRY] = g_signal_new("new-entry",
            G_TYPE_FROM_CLASS(nav_class),
            G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
            0,
            NULL,
            NULL,
            g_cclosure_marshal_VOID__UINT_POINTER,
            G_TYPE_NONE,
            2,
            G_TYPE_UINT,
            G_TYPE_POINTER,
            NULL);
}

static void
rstto_navigator_dispose(GObject *object)
{
    RsttoNavigator *navigator = RSTTO_NAVIGATOR(object);

    if(navigator->file_list)
    {
        g_list_foreach(navigator->file_list, (GFunc)rstto_navigator_entry_free, NULL);
        navigator->file_list = NULL;
        navigator->file_iter = NULL;
    }
}

RsttoNavigator *
rstto_navigator_new()
{
    RsttoNavigator *navigator;

    navigator = g_object_new(RSTTO_TYPE_NAVIGATOR, NULL);

    navigator->icon_theme = gtk_icon_theme_new();

    return navigator;
}

static gint
rstto_navigator_entry_name_compare_func(RsttoNavigatorEntry *a, RsttoNavigatorEntry *b)
{
    return g_strcasecmp(a->info->display_name, b->info->display_name);
}

void
rstto_navigator_jump_first (RsttoNavigator *navigator)
{
    if(navigator->file_iter)
    {
        navigator->old_position = rstto_navigator_get_position(navigator);
        rstto_navigator_entry_free_pixbuf(navigator->file_iter->data);
    }
    navigator->file_iter = g_list_first(navigator->file_list);
    if(navigator->file_iter)
    {
        g_signal_emit(G_OBJECT(navigator), rstto_navigator_signals[RSTTO_NAVIGATOR_SIGNAL_ITER_CHANGED], 0, 0, navigator->file_iter->data, NULL);
    }
}

void
rstto_navigator_jump_forward (RsttoNavigator *navigator)
{
    if(navigator->file_iter)
    {
        navigator->old_position = rstto_navigator_get_position(navigator);
        rstto_navigator_entry_free_pixbuf(navigator->file_iter->data);
        navigator->file_iter = g_list_next(navigator->file_iter);
    }
    if(!navigator->file_iter)
        navigator->file_iter = g_list_first(navigator->file_list);

    if(navigator->file_iter)
    {
        g_signal_emit(G_OBJECT(navigator),
                      rstto_navigator_signals[RSTTO_NAVIGATOR_SIGNAL_ITER_CHANGED],
                      0,
                      g_list_position(navigator->file_list, navigator->file_iter),
                      navigator->file_iter->data,
                      NULL);
    }
    else
    {
        g_signal_emit(G_OBJECT(navigator),
                      rstto_navigator_signals[RSTTO_NAVIGATOR_SIGNAL_ITER_CHANGED],
                      0,
                      -1,
                      NULL,
                      NULL);

    }
}

void
rstto_navigator_jump_back (RsttoNavigator *navigator)
{
    if(navigator->file_iter)
    {
        navigator->old_position = rstto_navigator_get_position(navigator);
        rstto_navigator_entry_free_pixbuf(navigator->file_iter->data);
        navigator->file_iter = g_list_previous(navigator->file_iter);
    }
    if(!navigator->file_iter)
        navigator->file_iter = g_list_last(navigator->file_list);

    if(navigator->file_iter)
    {
        g_signal_emit(G_OBJECT(navigator),
                      rstto_navigator_signals[RSTTO_NAVIGATOR_SIGNAL_ITER_CHANGED],
                      0,
                      g_list_position(navigator->file_list, navigator->file_iter),
                      navigator->file_iter->data,
                      NULL);
    }
}

void
rstto_navigator_jump_last (RsttoNavigator *navigator)
{
    if(navigator->file_iter)
    {
        navigator->old_position = rstto_navigator_get_position(navigator);
        rstto_navigator_entry_free_pixbuf(navigator->file_iter->data);
    }
    navigator->file_iter = g_list_last(navigator->file_list);

    if(navigator->file_iter)
    {
        g_signal_emit(G_OBJECT(navigator),
                      rstto_navigator_signals[RSTTO_NAVIGATOR_SIGNAL_ITER_CHANGED],
                      0,
                      g_list_position(navigator->file_list, navigator->file_iter),
                      navigator->file_iter->data,
                      NULL);
    }
}

void
rstto_navigator_set_running (RsttoNavigator *navigator, gboolean running)
{
    if(!navigator->running)
    {
        navigator->running = running;
        if(!navigator->id)
            navigator->id = g_timeout_add(navigator->timeout, (GSourceFunc)cb_rstto_navigator_running, navigator);
    }
    else
    {
        navigator->running = running;
    }
}

gboolean
rstto_navigator_is_running (RsttoNavigator *navigator)
{
    return navigator->running;
}

RsttoNavigatorEntry *
rstto_navigator_get_file (RsttoNavigator *navigator)
{
    if(navigator->file_iter)
    {
        return (RsttoNavigatorEntry *)(navigator->file_iter->data);
    }
    else
    {
        return NULL;
    }
}

gint
rstto_navigator_get_position(RsttoNavigator *navigator)
{
    return g_list_position(navigator->file_list, navigator->file_iter);
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
rstto_navigator_add (RsttoNavigator *navigator, RsttoNavigatorEntry *entry)
{
    if(navigator->file_iter)
    {
        rstto_navigator_entry_free_pixbuf(navigator->file_iter->data);
    }

    navigator->file_list = g_list_insert_sorted(navigator->file_list, entry, navigator->compare_func);
    if (!navigator->file_iter)
    {
        navigator->file_iter = navigator->file_list;
        g_signal_emit(G_OBJECT(navigator),
                      rstto_navigator_signals[RSTTO_NAVIGATOR_SIGNAL_ITER_CHANGED],
                      0,
                      g_list_index(navigator->file_list, entry),
                      entry,
                      NULL);
    }
    g_signal_emit(G_OBJECT(navigator), rstto_navigator_signals[RSTTO_NAVIGATOR_SIGNAL_NEW_ENTRY], 0, g_list_index(navigator->file_list, entry), entry, NULL);
}

void
rstto_navigator_remove (RsttoNavigator *navigator, RsttoNavigatorEntry *entry)
{
    if(navigator->file_iter)
    {
        if(navigator->file_iter->data == entry)
        {
            navigator->old_position = rstto_navigator_get_position(navigator);
            rstto_navigator_entry_free_pixbuf(navigator->file_iter->data);
            navigator->file_iter = g_list_next(navigator->file_iter);
            if(!navigator->file_iter)
                navigator->file_iter = g_list_first(navigator->file_list);

            navigator->file_list = g_list_remove(navigator->file_list, entry);
            if(g_list_length(navigator->file_list) == 0)
            {
                navigator->file_iter = NULL;
                navigator->file_list = NULL;
            }
            g_signal_emit(G_OBJECT(navigator), rstto_navigator_signals[RSTTO_NAVIGATOR_SIGNAL_REORDERED], 0, NULL);
            if(navigator->file_iter)
            {
                g_signal_emit(G_OBJECT(navigator),
                              rstto_navigator_signals[RSTTO_NAVIGATOR_SIGNAL_ITER_CHANGED],
                              0,
                              g_list_position(navigator->file_list, navigator->file_iter),
                              navigator->file_iter->data,
                              NULL);
            }
            else
            {
                g_signal_emit(G_OBJECT(navigator),
                              rstto_navigator_signals[RSTTO_NAVIGATOR_SIGNAL_ITER_CHANGED],
                              0,
                              -1,
                              NULL,
                              NULL);

            }
            return;
        }
    }
    navigator->file_list = g_list_remove(navigator->file_list, entry);
    if(g_list_length(navigator->file_list) == 0)
    {
        navigator->file_iter = NULL;
        navigator->file_list = NULL;
    }
    g_signal_emit(G_OBJECT(navigator), rstto_navigator_signals[RSTTO_NAVIGATOR_SIGNAL_REORDERED], 0, NULL);
}

void
rstto_navigator_clear (RsttoNavigator *navigator)
{
    if(navigator->file_list)
    {
        g_list_foreach(navigator->file_list, (GFunc)rstto_navigator_entry_free, NULL);
        navigator->file_list = NULL;
        navigator->file_iter = NULL;
        navigator->old_position = -1;
    }
    g_signal_emit(G_OBJECT(navigator), rstto_navigator_signals[RSTTO_NAVIGATOR_SIGNAL_REORDERED], 0, NULL);
    
}

void
rstto_navigator_set_file (RsttoNavigator *navigator, gint n)
{
    if(navigator->file_iter)
    {
        navigator->old_position = rstto_navigator_get_position(navigator);
        rstto_navigator_entry_free_pixbuf(navigator->file_iter->data);
    }
    navigator->file_iter = g_list_nth(navigator->file_list, n);
    if(navigator->file_iter)
    {
        g_signal_emit(G_OBJECT(navigator),
                      rstto_navigator_signals[RSTTO_NAVIGATOR_SIGNAL_ITER_CHANGED],
                      0,
                      g_list_position(navigator->file_list, navigator->file_iter),
                      navigator->file_iter->data,
                      NULL);
    }
}

static gboolean
cb_rstto_navigator_running(RsttoNavigator *navigator)
{
    if(navigator->running)
    {
        rstto_navigator_jump_forward(navigator);
    }
    else
        navigator->id = 0;
    return navigator->running;
}

void
rstto_navigator_set_timeout (RsttoNavigator *navigator, gint timeout)
{
    navigator->timeout = timeout;
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
}


RsttoNavigatorEntry *
rstto_navigator_entry_new (ThunarVfsInfo *info)
{
    RsttoNavigatorEntry *entry = NULL;
    gchar *filename = thunar_vfs_path_dup_string(info->path);
    if(filename)
    {
        entry = g_new0(RsttoNavigatorEntry, 1);

        entry->info = info;

        g_free(filename);
    }
    return entry;
}

ThunarVfsInfo *
rstto_navigator_entry_get_info (RsttoNavigatorEntry *entry)
{
    return entry->info;
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
rstto_navigator_entry_free(RsttoNavigatorEntry *nav_entry)
{
    if(nav_entry->pixbuf)
    {
        g_object_unref(nav_entry->pixbuf);
    }
    if(nav_entry->thumb)
    {
        g_object_unref(nav_entry->thumb);
    }
    thunar_vfs_info_unref(nav_entry->info);
    g_free(nav_entry);
}

GdkPixbuf *
rstto_navigator_entry_get_thumb(RsttoNavigatorEntry *entry, gint size)
{
    if(entry->thumb)    
    {
        if(!(gdk_pixbuf_get_width(entry->thumb) == size || gdk_pixbuf_get_height(entry->thumb) == size))
        {
            g_object_unref(entry->thumb);
            gchar *filename = thunar_vfs_path_dup_string(entry->info->path);
            entry->thumb = gdk_pixbuf_new_from_file_at_size(filename, size, size, NULL);
            g_free(filename);
        }
    }
    else
    {
        gchar *filename = thunar_vfs_path_dup_string(entry->info->path);
        entry->thumb = gdk_pixbuf_new_from_file_at_size(filename, size, size, NULL);
        g_free(filename);
    }
    return entry->thumb;
}

GdkPixbuf *
rstto_navigator_entry_get_pixbuf(RsttoNavigatorEntry *entry)
{
    g_return_val_if_fail(entry, NULL);

    if(!entry->pixbuf)
    {
        gchar *filename = thunar_vfs_path_dup_string(entry->info->path);
        entry->pixbuf = gdk_pixbuf_new_from_file(filename, NULL);
        g_free(filename);
    }
    return entry->pixbuf;
}

static void
rstto_navigator_entry_free_pixbuf (RsttoNavigatorEntry *entry)
{
    if(entry->pixbuf)
    {
        g_object_unref(entry->pixbuf);
        entry->pixbuf = NULL;
    }
}

gint
rstto_navigator_get_old_position (RsttoNavigator *navigator)
{
    return navigator->old_position;
}

gdouble
rstto_navigator_entry_get_scale (RsttoNavigatorEntry *entry)
{
    return entry->scale;
}

void
rstto_navigator_entry_set_scale (RsttoNavigatorEntry *entry, gdouble scale)
{
    /* Max scale 1600% */
    if (scale > 16)
        scale = 16;
    /* Min scale 5% */
    if (scale < 0.05)
        scale = 0.05;
    entry->scale = scale;
}

gboolean
rstto_navigator_entry_get_fit_to_screen (RsttoNavigatorEntry *entry)
{
    return entry->fit_to_screen;
}

void
rstto_navigator_entry_set_fit_to_screen (RsttoNavigatorEntry *entry, gboolean fts)
{
    entry->fit_to_screen = fts;
}


