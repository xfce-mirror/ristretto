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
#include <libexif/exif-data.h>

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
cb_rstto_navigator_entry_area_prepared (GdkPixbufLoader *loader, RsttoNavigatorEntry *entry);
static void
cb_rstto_navigator_entry_closed (GdkPixbufLoader *loader, RsttoNavigatorEntry *entry);
static gboolean
cb_rstto_navigator_entry_read_file(GIOChannel *io_channel, GIOCondition cond, RsttoNavigatorEntry *entry);

static gboolean
cb_rstto_navigator_entry_update_image (RsttoNavigatorEntry *entry);

static void
cb_rstto_navigator_entry_fs_event (ThunarVfsMonitor *,
                                   ThunarVfsMonitorHandle *,
                                   ThunarVfsMonitorEvent,
                                   ThunarVfsPath *,
                                   ThunarVfsPath *,
                                   RsttoNavigatorEntry *);
static void
cb_rstto_navigator_fs_event (ThunarVfsMonitor *monitor,
                             ThunarVfsMonitorHandle *handl,
                             ThunarVfsMonitorEvent event,
                             ThunarVfsPath *handle_path,
                             ThunarVfsPath *event_path,
                             RsttoNavigator *nav);

static gint
cb_rstto_navigator_entry_path_compare_func(RsttoNavigatorEntry *entry, ThunarVfsPath *path);

enum
{
    RSTTO_NAVIGATOR_SIGNAL_ENTRY_MODIFIED = 0,
    RSTTO_NAVIGATOR_SIGNAL_ENTRY_REMOVED,
    RSTTO_NAVIGATOR_SIGNAL_NEW_ENTRY,
    RSTTO_NAVIGATOR_SIGNAL_ITER_CHANGED,
    RSTTO_NAVIGATOR_SIGNAL_REORDERED,
    RSTTO_NAVIGATOR_SIGNAL_COUNT    
};

struct _RsttoNavigatorEntry
{
    ThunarVfsInfo       *info;
    GdkPixbufLoader     *loader;
    ExifData            *exif_data;
    ThunarVfsMonitorHandle *monitor_handle;

    GdkPixbuf           *thumb;

    GdkPixbufAnimation  *animation;
    GdkPixbufAnimationIter *iter;
    GdkPixbuf           *src_pixbuf;

    GIOChannel          *io_channel;
    gint                 io_source_id;
    gint                 timeout_id;

    RsttoNavigator      *navigator;

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
    navigator->monitor = thunar_vfs_monitor_get_default();
    navigator->preload = FALSE;

    /* Max history size (in bytes) */
    navigator->max_history = 128000000;

    navigator->factory = thunar_vfs_thumb_factory_new(THUNAR_VFS_THUMB_SIZE_NORMAL);
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
            g_cclosure_marshal_VOID__POINTER,
            G_TYPE_NONE,
            1,
            G_TYPE_POINTER,
            NULL);
    rstto_navigator_signals[RSTTO_NAVIGATOR_SIGNAL_ENTRY_REMOVED] = g_signal_new("entry-removed",
            G_TYPE_FROM_CLASS(nav_class),
            G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
            0,
            NULL,
            NULL,
            g_cclosure_marshal_VOID__POINTER,
            G_TYPE_NONE,
            1,
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

    return navigator;
}

/*
 * static gint
 * rstto_navigator_entry_name_compare_func:
 *
 * @a: RsttoNavigatorEntry 
 * @b: RsttoNavigatorEntry
 *
 * Return value: see g_strcasecmp
 */
static gint
rstto_navigator_entry_name_compare_func(RsttoNavigatorEntry *a, RsttoNavigatorEntry *b)
{
    return g_strcasecmp(a->info->display_name, b->info->display_name);
}

void
rstto_navigator_guard_history(RsttoNavigator *navigator, RsttoNavigatorEntry *entry)
{
    /* check if the image is still loading, if so... don't cache the image */
    if(entry->io_channel)
    {
        g_source_remove(entry->io_source_id);
        g_io_channel_unref(entry->io_channel);
        entry->io_channel = NULL;
        entry->io_source_id = 0;
        if(entry->loader)
        {
            g_signal_handlers_disconnect_by_func(entry->loader , cb_rstto_navigator_entry_area_prepared, entry);
            gdk_pixbuf_loader_close(entry->loader, NULL);
        }

        if (entry->timeout_id)
        {
            g_source_remove(entry->timeout_id);
            entry->timeout_id = 0;
        }

        if(entry->animation)
        {
            g_object_unref(entry->animation);
            entry->animation = NULL;
        }

        if(entry->src_pixbuf)
        {
            gdk_pixbuf_unref(entry->src_pixbuf);
            entry->src_pixbuf = NULL;
        }
    }

    /* add image to the cache-history */
    navigator->history = g_list_prepend(navigator->history, entry);
    
    GList *iter = NULL;
    guint64 size = 0;

    for (iter = navigator->history; iter != NULL; iter = g_list_next(iter))
    {
        RsttoNavigatorEntry *nav_entry = iter->data;

        if (nav_entry)
        {
            size += rstto_navigator_entry_get_size(nav_entry);

            if (size > navigator->max_history)
            {
                if(nav_entry->thumb)
                {
                    gdk_pixbuf_unref(nav_entry->thumb);
                    nav_entry->thumb = NULL;
                }

                if(nav_entry->io_channel)
                {
                    g_source_remove(nav_entry->io_source_id);
                    g_io_channel_unref(nav_entry->io_channel);
                    nav_entry->io_channel = NULL;
                    nav_entry->io_source_id = 0;
                }

                if (entry->timeout_id)
                {
                    g_source_remove(entry->timeout_id);
                    entry->timeout_id = 0;
                }

                if(nav_entry->loader)
                {
                    g_signal_handlers_disconnect_by_func(nav_entry->loader , cb_rstto_navigator_entry_area_prepared, nav_entry);
                    gdk_pixbuf_loader_close(nav_entry->loader, NULL);
                }
                if(nav_entry->animation)
                {
                    g_object_unref(nav_entry->animation);
                    nav_entry->animation = NULL;
                }
                if(nav_entry->src_pixbuf)
                {
                    gdk_pixbuf_unref(nav_entry->src_pixbuf);
                    nav_entry->src_pixbuf = NULL;
                }
                iter = g_list_previous(iter);
                navigator->history = g_list_remove(navigator->history, nav_entry);
            }
        }
    }
}

void
rstto_navigator_jump_first (RsttoNavigator *navigator)
{
    if(navigator->file_iter)
    {
        rstto_navigator_guard_history(navigator, navigator->file_iter->data);
        navigator->old_position = rstto_navigator_get_position(navigator);
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
        rstto_navigator_guard_history(navigator, navigator->file_iter->data);
        navigator->old_position = rstto_navigator_get_position(navigator);
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
        rstto_navigator_guard_history(navigator, navigator->file_iter->data);
        navigator->old_position = rstto_navigator_get_position(navigator);
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
        rstto_navigator_guard_history(navigator, navigator->file_iter->data);
        navigator->old_position = rstto_navigator_get_position(navigator);
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
        {
            navigator->id = g_timeout_add(navigator->timeout, (GSourceFunc)cb_rstto_navigator_running, navigator);
            if (navigator->preload)
            {
                GList *next = g_list_next(navigator->file_iter);
                if (next == NULL)
                {
                    next = navigator->file_list;
                }
                if (next != NULL)
                {
                    RsttoNavigatorEntry *next_entry = next->data;
                    
                    rstto_navigator_entry_load_image(next_entry, FALSE);
                }
            }
        }
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

gint
rstto_navigator_add (RsttoNavigator *navigator, RsttoNavigatorEntry *entry, gboolean with_monitor)
{
    g_return_val_if_fail(navigator == entry->navigator, -1);

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

    if (with_monitor == TRUE)
        entry->monitor_handle = thunar_vfs_monitor_add_file(navigator->monitor, entry->info->path, (ThunarVfsMonitorCallback)cb_rstto_navigator_entry_fs_event, entry);

    g_signal_emit(G_OBJECT(navigator), rstto_navigator_signals[RSTTO_NAVIGATOR_SIGNAL_NEW_ENTRY], 0, g_list_index(navigator->file_list, entry), entry, NULL);
    return g_list_index(navigator->file_list, entry);
}

void
rstto_navigator_remove (RsttoNavigator *navigator, RsttoNavigatorEntry *entry)
{
    if(navigator->file_iter)
    {
        if(navigator->file_iter->data == entry)
        {
            navigator->old_position = rstto_navigator_get_position(navigator);
            navigator->file_iter = g_list_next(navigator->file_iter);

            navigator->file_list = g_list_remove(navigator->file_list, entry);

            if(!navigator->file_iter)
                navigator->file_iter = g_list_first(navigator->file_list);

            /* Ehm... an item can exist several times inside the history? */
            if (navigator->history)
                navigator->history = g_list_remove_all(navigator->history, entry);

            g_signal_emit(G_OBJECT(navigator), rstto_navigator_signals[RSTTO_NAVIGATOR_SIGNAL_ENTRY_REMOVED], 0, entry, NULL);
            if(g_list_length(navigator->file_list) == 0)
            {
                navigator->file_iter = NULL;
                navigator->file_list = NULL;
            }
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
        if (g_list_find(navigator->history, entry))
        {
            navigator->history = g_list_remove(navigator->history, entry);
        }
    }
    navigator->file_list = g_list_remove(navigator->file_list, entry);
    if (entry->monitor_handle)
    {
        thunar_vfs_monitor_remove(navigator->monitor, entry->monitor_handle);
    }
    if(g_list_length(navigator->file_list) == 0)
    {
        navigator->file_iter = NULL;
        navigator->file_list = NULL;
    }
}

void
rstto_navigator_clear (RsttoNavigator *navigator)
{
    if(navigator->file_list)
    {
        g_list_free(navigator->history);
        g_list_foreach(navigator->file_list, (GFunc)rstto_navigator_entry_free, NULL);
        navigator->file_list = NULL;
        navigator->file_iter = NULL;
        navigator->old_position = -1;
        navigator->history = NULL;
    }
    g_signal_emit(G_OBJECT(navigator), rstto_navigator_signals[RSTTO_NAVIGATOR_SIGNAL_ITER_CHANGED], 0, -1, NULL, NULL);
    g_signal_emit(G_OBJECT(navigator), rstto_navigator_signals[RSTTO_NAVIGATOR_SIGNAL_REORDERED], 0, NULL);
    
}

void
rstto_navigator_set_file_nr (RsttoNavigator *navigator, gint n)
{
    if(navigator->file_iter)
    {
        rstto_navigator_guard_history(navigator, navigator->file_iter->data);
        navigator->old_position = rstto_navigator_get_position(navigator);
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

/* Callbacks */

static gboolean
cb_rstto_navigator_running(RsttoNavigator *navigator)
{
    if(navigator->running)
    {
        rstto_navigator_jump_forward(navigator);

        if (navigator->preload)
        {
            GList *next = g_list_next(navigator->file_iter);
            if (next == NULL)
            {
                next = navigator->file_list;
            }
            if (next != NULL)
            {
                RsttoNavigatorEntry *next_entry = next->data;
                
                rstto_navigator_entry_load_image(next_entry, FALSE);
            }
        }
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
rstto_navigator_entry_new (RsttoNavigator *navigator, ThunarVfsInfo *info)
{
    RsttoNavigatorEntry *entry = NULL;
    gchar *filename = thunar_vfs_path_dup_string(info->path);
    if(filename)
    {
        entry = g_new0(RsttoNavigatorEntry, 1);

        entry->info = info;
        entry->exif_data = exif_data_new_from_file(filename);
        entry->navigator = navigator;
        
        ExifEntry *exifentry = exif_data_get_entry(entry->exif_data, EXIF_TAG_ORIENTATION);
        if (exifentry)
        {
            gchar *val = g_new0(gchar, 20);
            exif_entry_get_value(exifentry, val, 20);
            if (!strcmp(val, "top - left"))
            {
                entry->v_flipped = FALSE;
                entry->h_flipped = FALSE;
                entry->rotation = GDK_PIXBUF_ROTATE_NONE;
            }
            if (!strcmp(val, "top - right"))
            {
                entry->v_flipped = FALSE;
                entry->h_flipped = TRUE;
                entry->rotation = GDK_PIXBUF_ROTATE_NONE;
            }
            if (!strcmp(val, "bottom - left"))
            {
                entry->v_flipped = TRUE;
                entry->h_flipped = FALSE;
                entry->rotation = GDK_PIXBUF_ROTATE_NONE;
            }
            if (!strcmp(val, "bottom - right"))
            {
                entry->v_flipped = FALSE;
                entry->h_flipped = FALSE;
                entry->rotation = GDK_PIXBUF_ROTATE_UPSIDEDOWN;
            }
            if (!strcmp(val, "right - top"))
            {
                entry->v_flipped = FALSE;
                entry->h_flipped = FALSE;
                entry->rotation = GDK_PIXBUF_ROTATE_CLOCKWISE;
            }
            if (!strcmp(val, "right - bottom"))
            {
                entry->v_flipped = FALSE;
                entry->h_flipped = TRUE;
                entry->rotation = GDK_PIXBUF_ROTATE_COUNTERCLOCKWISE;
            }
            if (!strcmp(val, "left - top"))
            {
                entry->v_flipped = FALSE;
                entry->h_flipped = TRUE;
                entry->rotation = GDK_PIXBUF_ROTATE_CLOCKWISE;
            }
            if (!strcmp(val, "left - bottom"))
            {
                entry->v_flipped = FALSE;
                entry->h_flipped = FALSE;
                entry->rotation = GDK_PIXBUF_ROTATE_COUNTERCLOCKWISE;
            }
            g_free(val);
        }

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
    if(nav_entry->thumb)
    {
        gdk_pixbuf_unref(nav_entry->thumb);
        nav_entry->thumb = NULL;
    }
    if(nav_entry->exif_data)
    {   
        exif_data_free(nav_entry->exif_data);
        nav_entry->exif_data = NULL;
    }
    
    if(nav_entry->io_channel)
    {
        g_io_channel_unref(nav_entry->io_channel);
        g_source_remove(nav_entry->io_source_id);
    }

    if (nav_entry->timeout_id)
    {
        g_source_remove(nav_entry->timeout_id);
        nav_entry->timeout_id = 0;
    }

    if(nav_entry->loader)
    {
        g_signal_handlers_disconnect_by_func(nav_entry->loader , cb_rstto_navigator_entry_area_prepared, nav_entry);
        gdk_pixbuf_loader_close(nav_entry->loader, NULL);
    }
    if(nav_entry->animation)
    {
        g_object_unref(nav_entry->animation);
    }
    if(nav_entry->src_pixbuf)
    {
        gdk_pixbuf_unref(nav_entry->src_pixbuf);
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
        }
    }
    else
    {
        ThunarVfsInfo *info = rstto_navigator_entry_get_info(entry);
        gchar *thumbnail = thunar_vfs_thumb_factory_lookup_thumbnail(entry->navigator->factory, info);
        if (thumbnail == NULL)
        {
            GdkPixbuf *pixbuf = thunar_vfs_thumb_factory_generate_thumbnail(entry->navigator->factory, info);
            if (pixbuf != NULL)
            {
                if (!thunar_vfs_thumb_factory_store_thumbnail(entry->navigator->factory, pixbuf, info, NULL))
                {
                    g_critical("Storing thumbnail failed");
                }

                gint width = gdk_pixbuf_get_width(pixbuf);
                gint height = gdk_pixbuf_get_height(pixbuf);

                if (width > height)
                {
                    entry->thumb = gdk_pixbuf_new(GDK_COLORSPACE_RGB,
                                                  gdk_pixbuf_get_has_alpha(pixbuf),
                                                  gdk_pixbuf_get_bits_per_sample(pixbuf),
                                                  size,
                                                  height*size/width);
                }
                else
                {
                    entry->thumb = gdk_pixbuf_new(GDK_COLORSPACE_RGB,
                                                  gdk_pixbuf_get_has_alpha(pixbuf),
                                                  gdk_pixbuf_get_bits_per_sample(pixbuf),
                                                  width*size/height,
                                                  size);
                }
                gdk_pixbuf_scale(pixbuf, entry->thumb,
                                 0, 0, 
                                 gdk_pixbuf_get_width(entry->thumb),
                                 gdk_pixbuf_get_height(entry->thumb),
                                 0, 0,
                                 ((gdouble)gdk_pixbuf_get_width(entry->thumb)) / (gdouble)width,
                                 ((gdouble)gdk_pixbuf_get_height(entry->thumb)) / (gdouble)height,
                                 GDK_INTERP_BILINEAR);
            }
            else
            {
                thumbnail = thunar_vfs_path_dup_string(info->path);
                entry->thumb = gdk_pixbuf_new_from_file_at_scale(thumbnail, size, size, TRUE, NULL);
                g_free(thumbnail);
            }
        }
        else
        {
            entry->thumb = gdk_pixbuf_new_from_file_at_scale(thumbnail, size, size, TRUE, NULL);
            g_free(thumbnail);
        }
    }
    return entry->thumb;
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
    if (scale == 0.0)
    {
        entry->scale = scale;
        return;
    }
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

void
rstto_navigator_entry_set_rotation (RsttoNavigatorEntry *entry, GdkPixbufRotation rotation)
{
    GdkPixbuf *pixbuf = entry->src_pixbuf;
    if (pixbuf)
    {
        entry->src_pixbuf = gdk_pixbuf_rotate_simple(pixbuf, (360+(rotation-entry->rotation))%360);
    }
    entry->rotation = rotation;
    g_signal_emit(G_OBJECT(entry->navigator), rstto_navigator_signals[RSTTO_NAVIGATOR_SIGNAL_ENTRY_MODIFIED], 0, entry, NULL);
}


ExifData *
rstto_navigator_entry_get_exif_data (RsttoNavigatorEntry *entry)
{
    return entry->exif_data;
}


GdkPixbufLoader *
rstto_navigator_entry_get_pixbuf_loader (RsttoNavigatorEntry *entry)
{
    if (!entry->loader)
    {
        entry->loader = gdk_pixbuf_loader_new();
    }
    return entry->loader;
}

GdkPixbuf *
rstto_navigator_entry_get_pixbuf (RsttoNavigatorEntry *entry)
{
    return entry->src_pixbuf;
}

guint64
rstto_navigator_entry_get_size (RsttoNavigatorEntry *entry)
{
    if (entry->src_pixbuf)
    {
        gint width = gdk_pixbuf_get_width(entry->src_pixbuf);
        gint height = gdk_pixbuf_get_height(entry->src_pixbuf);

        gint n_channels = gdk_pixbuf_get_n_channels(entry->src_pixbuf);

        return (guint64) width * height * n_channels;
    }
    return 0;
}

gboolean
rstto_navigator_entry_load_image (RsttoNavigatorEntry *entry, gboolean empty_cache)
{
    g_return_val_if_fail(entry != NULL, FALSE);
    gchar *path = NULL;

    if (entry->io_channel)
    {
        return FALSE;
    }
    if ((entry->loader == NULL) && ((empty_cache == TRUE ) || entry->src_pixbuf == NULL))
    {
        entry->loader = gdk_pixbuf_loader_new();

        g_signal_connect(entry->loader, "area-prepared", G_CALLBACK(cb_rstto_navigator_entry_area_prepared), entry);
        /*g_signal_connect(entry->loader, "area-updated", G_CALLBACK(cb_rstto_navigator_entry_area_updated), viewer);*/
        g_signal_connect(entry->loader, "closed", G_CALLBACK(cb_rstto_navigator_entry_closed), entry);

        path = thunar_vfs_path_dup_string(entry->info->path);
        entry->io_channel = g_io_channel_new_file(path, "r", NULL);

        g_io_channel_set_encoding(entry->io_channel, NULL, NULL);
        entry->io_source_id = g_io_add_watch(entry->io_channel, G_IO_IN | G_IO_PRI, (GIOFunc)cb_rstto_navigator_entry_read_file, entry);
    }
    else
    {
        if (entry->src_pixbuf)
        {
            g_signal_emit(G_OBJECT(entry->navigator), rstto_navigator_signals[RSTTO_NAVIGATOR_SIGNAL_ENTRY_MODIFIED], 0, entry, NULL);
        }
    }

    return TRUE;
}


static gboolean
cb_rstto_navigator_entry_read_file(GIOChannel *io_channel, GIOCondition cond, RsttoNavigatorEntry *entry)
{
    gchar buffer[1024];
    gsize bytes_read = 0;
    GError *error = NULL;
    GIOStatus status;

    g_return_val_if_fail(io_channel == entry->io_channel, FALSE);

    if (entry->loader)
    {

        status = g_io_channel_read_chars(io_channel, buffer, 1024, &bytes_read,  &error);

        switch (status)
        {
            case G_IO_STATUS_NORMAL:
                if(gdk_pixbuf_loader_write(entry->loader, (const guchar *)buffer, bytes_read, NULL) == FALSE)
                {
                    g_io_channel_unref(io_channel);
                    entry->io_channel = NULL;
                    entry->io_source_id = 0;
                    return FALSE;
                }
                return TRUE;
                break;
            case G_IO_STATUS_EOF:
                gdk_pixbuf_loader_write(entry->loader, (const guchar *)buffer, bytes_read, NULL);
                gdk_pixbuf_loader_close(entry->loader, NULL);
                g_io_channel_unref(io_channel);
                entry->io_channel = NULL;
                entry->io_source_id = 0;
                return FALSE;
                break;
            case G_IO_STATUS_ERROR:
                if (entry->loader)
                {
                    gdk_pixbuf_loader_close(entry->loader, NULL);
                }
                g_io_channel_unref(io_channel);
                entry->io_channel = NULL;
                entry->io_source_id = 0;
                return FALSE;
                break;
            case G_IO_STATUS_AGAIN:
                return TRUE;
                break;
        }
    }
    g_io_channel_unref(io_channel);
    entry->io_channel = NULL;
    entry->io_source_id = 0;
    return FALSE;
}

static void
cb_rstto_navigator_entry_area_prepared (GdkPixbufLoader *loader, RsttoNavigatorEntry *entry)
{
    entry->animation = gdk_pixbuf_loader_get_animation(loader);
    entry->iter = gdk_pixbuf_animation_get_iter(entry->animation, NULL);
    if (entry->src_pixbuf)
    {
        gdk_pixbuf_unref(entry->src_pixbuf);
        entry->src_pixbuf = NULL;
    }

    gint time = gdk_pixbuf_animation_iter_get_delay_time(entry->iter);

    if (time != -1)
    {
        /* fix borked stuff */
        if (time == 0)
        {
            g_warning("timeout == 0: defaulting to 40ms");
            time = 40;
        }

        entry->timeout_id = g_timeout_add(time, (GSourceFunc)cb_rstto_navigator_entry_update_image, entry);
    }   
    else
    {
        entry->iter = NULL;
    }
    g_signal_emit(G_OBJECT(entry->navigator), rstto_navigator_signals[RSTTO_NAVIGATOR_SIGNAL_ENTRY_MODIFIED], 0, entry, NULL);
}

static void
cb_rstto_navigator_entry_closed (GdkPixbufLoader *loader, RsttoNavigatorEntry *entry)
{
    if (entry->src_pixbuf)
    {
        gdk_pixbuf_unref(entry->src_pixbuf);
        entry->src_pixbuf = NULL;
    }

    GdkPixbuf *pixbuf = NULL;

    if (entry->iter)
    {
        pixbuf = gdk_pixbuf_animation_iter_get_pixbuf(entry->iter);
    }
    else
    {
        if (entry->loader)
        {
            pixbuf = gdk_pixbuf_loader_get_pixbuf(entry->loader);
        }
    }

    if (entry->loader == loader)
    {
        g_object_unref(entry->loader);
        entry->loader = NULL;
    }

   
    if (pixbuf != NULL)
    {
        entry->src_pixbuf = gdk_pixbuf_rotate_simple(pixbuf, rstto_navigator_entry_get_rotation(entry));
        if (rstto_navigator_entry_get_flip(entry, FALSE))
        {
            pixbuf = entry->src_pixbuf;
            entry->src_pixbuf = gdk_pixbuf_flip(pixbuf, FALSE);
            gdk_pixbuf_unref(pixbuf);
        }

        if (rstto_navigator_entry_get_flip(entry, TRUE))
        {
            pixbuf = entry->src_pixbuf;
            entry->src_pixbuf = gdk_pixbuf_flip(pixbuf, TRUE);
            gdk_pixbuf_unref(pixbuf);
        }
        g_signal_emit(G_OBJECT(entry->navigator), rstto_navigator_signals[RSTTO_NAVIGATOR_SIGNAL_ENTRY_MODIFIED], 0, entry, NULL);
    }
}

static gboolean
cb_rstto_navigator_entry_update_image (RsttoNavigatorEntry *entry)
{
    GdkPixbuf *src_pixbuf = NULL;

    if (entry->iter)
    {
        if(gdk_pixbuf_animation_iter_advance(entry->iter, NULL))
        {
            /* Cleanup old image */
            if (entry->src_pixbuf)
            {
                gdk_pixbuf_unref(entry->src_pixbuf);
                entry->src_pixbuf = NULL;
            }
            entry->src_pixbuf = gdk_pixbuf_animation_iter_get_pixbuf(entry->iter);
            src_pixbuf = entry->src_pixbuf;

            if (src_pixbuf)
            {
                entry->src_pixbuf = gdk_pixbuf_rotate_simple(src_pixbuf, rstto_navigator_entry_get_rotation(entry));
                if (rstto_navigator_entry_get_flip(entry, FALSE))
                {
                    src_pixbuf = entry->src_pixbuf;
                    entry->src_pixbuf = gdk_pixbuf_flip(src_pixbuf, FALSE);
                    gdk_pixbuf_unref(src_pixbuf);
                }

                if (rstto_navigator_entry_get_flip(entry, TRUE))
                {
                    src_pixbuf = entry->src_pixbuf;
                    entry->src_pixbuf = gdk_pixbuf_flip(src_pixbuf, TRUE);
                    gdk_pixbuf_unref(src_pixbuf);
                }
            }
        }

        gint time = gdk_pixbuf_animation_iter_get_delay_time(entry->iter);
        if (time != -1)
        {
            if (time == 0)
            {
                g_warning("timeout == 0: defaulting to 40ms");
                time = 40;
            }
            entry->timeout_id = g_timeout_add(time, (GSourceFunc)cb_rstto_navigator_entry_update_image, entry);
        }
        g_signal_emit(G_OBJECT(entry->navigator), rstto_navigator_signals[RSTTO_NAVIGATOR_SIGNAL_ENTRY_MODIFIED], 0, entry, NULL);

        return FALSE;
    }
    return TRUE;
}

static void
cb_rstto_navigator_entry_fs_event (ThunarVfsMonitor *monitor,
                                   ThunarVfsMonitorHandle *handl,
                                   ThunarVfsMonitorEvent event,
                                   ThunarVfsPath *handle_path,
                                   ThunarVfsPath *event_path,
                                   RsttoNavigatorEntry *entry)
{
    switch (event)
    {
        case THUNAR_VFS_MONITOR_EVENT_CHANGED:
            rstto_navigator_entry_load_image (entry, TRUE);
            break;
        case THUNAR_VFS_MONITOR_EVENT_CREATED:
            break;
        case THUNAR_VFS_MONITOR_EVENT_DELETED:
            rstto_navigator_remove(entry->navigator, entry);
            rstto_navigator_entry_free(entry);
            break;
        default:
            break;
    }
}

static void
cb_rstto_navigator_fs_event (ThunarVfsMonitor *monitor,
                             ThunarVfsMonitorHandle *handl,
                             ThunarVfsMonitorEvent event,
                             ThunarVfsPath *handle_path,
                             ThunarVfsPath *event_path,
                             RsttoNavigator *nav)
{
    RsttoNavigatorEntry *entry = NULL;
    GList *iter = g_list_find_custom(nav->file_list, event_path, (GCompareFunc)cb_rstto_navigator_entry_path_compare_func);
    if (iter != NULL)
        entry = iter->data;

    switch (event)
    {
        case THUNAR_VFS_MONITOR_EVENT_CHANGED:
            if(entry)
            {
                rstto_navigator_entry_load_image (entry, TRUE);
            }
            break;
        case THUNAR_VFS_MONITOR_EVENT_CREATED:
            if (entry)
            {
                g_critical("File created... yet it is already here");
                rstto_navigator_remove(entry->navigator, entry);
                rstto_navigator_entry_free(entry);
            }

            ThunarVfsInfo *info = thunar_vfs_info_new_for_path(event_path, NULL);
            if (info)
            {
                gchar *file_media = thunar_vfs_mime_info_get_media(info->mime_info);
                if(!strcmp(file_media, "image"))
                {
                    entry = rstto_navigator_entry_new(nav, info);
                    rstto_navigator_add (nav, entry, FALSE);
                }
                g_free(file_media);
            }
            break;
        case THUNAR_VFS_MONITOR_EVENT_DELETED:
            if(entry)
            {
                rstto_navigator_remove(entry->navigator, entry);
                rstto_navigator_entry_free(entry);
            }
            break;
        default:
            break;
    }
}

void
rstto_navigator_entry_select (RsttoNavigatorEntry *entry)
{
    RsttoNavigator *navigator = entry->navigator;
    GList *iter = g_list_find (navigator->file_list, entry);
    if (iter)
    {
        if(navigator->file_iter)
        {
            rstto_navigator_guard_history(navigator, navigator->file_iter->data);
            navigator->old_position = rstto_navigator_get_position(navigator);
        }

        navigator->file_iter = iter;

        g_signal_emit(G_OBJECT(navigator),
                      rstto_navigator_signals[RSTTO_NAVIGATOR_SIGNAL_ITER_CHANGED],
                      0,
                      g_list_position(navigator->file_list, navigator->file_iter),
                      navigator->file_iter->data,
                      NULL);
    }

}

gdouble
rstto_navigator_get_max_history_size (RsttoNavigator *navigator)
{
    return navigator->max_history;
}



gint
rstto_navigator_entry_get_position(RsttoNavigatorEntry *entry)
{
    RsttoNavigator *navigator = entry->navigator;
    GList *iter = g_list_find (navigator->file_list, entry);
    if (iter)
    {
        return g_list_position(navigator->file_list, iter);
    }
    return -1;
}

void
rstto_navigator_set_max_history_size(RsttoNavigator *nav, gdouble size)
{
    nav->max_history = size;
}

void
rstto_navigator_set_monitor_handle_for_dir(RsttoNavigator *nav, ThunarVfsPath *dir_path)
{
    if (nav->monitor_handle)
    {
        thunar_vfs_monitor_remove(nav->monitor, nav->monitor_handle);
        nav->monitor_handle = NULL;
    }
    
    if (dir_path)
    {
        nav->monitor_handle = thunar_vfs_monitor_add_directory(nav->monitor, dir_path, (ThunarVfsMonitorCallback)cb_rstto_navigator_fs_event, nav);
    }
}

static gint
cb_rstto_navigator_entry_path_compare_func(RsttoNavigatorEntry *entry, ThunarVfsPath *path)
{
    if (thunar_vfs_path_equal(entry->info->path, path) == TRUE)
    {
        return 0;
    }
    return 1;
}

gboolean
rstto_navigator_entry_is_selected(RsttoNavigatorEntry *entry)
{
    g_return_val_if_fail(RSTTO_IS_NAVIGATOR(entry->navigator), FALSE);
    g_return_val_if_fail((entry->navigator->file_iter != NULL), FALSE);

    if (entry == entry->navigator->file_iter->data)
        return TRUE;
    else
        return FALSE;
}
