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

#ifndef __RISTRETTO_NAVIGATOR_H__
#define __RISTRETTO_NAVIGATOR_H__

G_BEGIN_DECLS

#define RSTTO_TYPE_NAVIGATOR rstto_navigator_get_type()

#define RSTTO_NAVIGATOR(obj)( \
        G_TYPE_CHECK_INSTANCE_CAST ((obj), \
                RSTTO_TYPE_NAVIGATOR, \
                RsttoNavigator))

#define RSTTO_IS_NAVIGATOR(obj)( \
        G_TYPE_CHECK_INSTANCE_TYPE ((obj), \
                RSTTO_TYPE_NAVIGATOR))

#define RSTTO_NAVIGATOR_CLASS(klass)( \
        G_TYPE_CHECK_CLASS_CAST ((klass), \
                RSTTO_TYPE_NAVIGATOR, \
                RsttoNavigatorClass))

#define RSTTO_IS_NAVIGATOR_CLASS(klass)( \
        G_TYPE_CHECK_CLASS_TYPE ((klass), \
                RSTTO_TYPE_NAVIGATOR()))

typedef struct _RsttoNavigatorEntry RsttoNavigatorEntry;

typedef struct _RsttoNavigator RsttoNavigator;

struct _RsttoNavigator
{
    GObject                parent;
    GtkRecentManager      *manager;
    ThunarVfsMonitor      *monitor;
    ThunarVfsMonitorHandle *monitor_handle;

    ThunarVfsThumbFactory *factory;

    ThunarVfsPath         *path;
    GCompareFunc           compare_func;
    GList                 *file_list;
    GList                 *file_iter;
    GList                 *history;
    gdouble                max_history;

    gboolean               preload;

    gint                   old_position;
    gboolean               running;
    gint                   timeout;
    gint                   id;

    gboolean               busy;
};

typedef struct _RsttoNavigatorClass RsttoNavigatorClass;
struct _RsttoNavigatorClass
{
    GObjectClass      parent_class;
};

GType      rstto_navigator_get_type ();

RsttoNavigator *
rstto_navigator_new (GtkRecentManager *recent_manager);

void       rstto_navigator_jump_first (RsttoNavigator *navigator);
void       rstto_navigator_jump_forward (RsttoNavigator *navigator);
void       rstto_navigator_jump_back (RsttoNavigator *navigator);
void       rstto_navigator_jump_last (RsttoNavigator *navigator);

void       rstto_navigator_set_running (RsttoNavigator *navigator,
                                        gboolean running);
gboolean   rstto_navigator_is_running (RsttoNavigator *navigator);
void
rstto_navigator_set_timeout (RsttoNavigator *nav, gint);


RsttoNavigatorEntry *
rstto_navigator_get_file (RsttoNavigator *navigator);
gint
rstto_navigator_get_position(RsttoNavigator *navigator);
gint
rstto_navigator_get_old_position(RsttoNavigator *navigator);

gint
rstto_navigator_add (RsttoNavigator *navigator, RsttoNavigatorEntry *entry, gboolean with_monitor);
void
rstto_navigator_remove (RsttoNavigator *navigator, RsttoNavigatorEntry *entry);
gint
rstto_navigator_get_n_files (RsttoNavigator *navigator);
RsttoNavigatorEntry *
rstto_navigator_get_nth_file (RsttoNavigator *navigator, gint n);
void
rstto_navigator_set_file_nr (RsttoNavigator *navigator, gint n);
void
rstto_navigator_clear (RsttoNavigator *navigator);

RsttoNavigatorEntry *
rstto_navigator_entry_new (RsttoNavigator *, ThunarVfsInfo *info);
void
rstto_navigator_entry_free(RsttoNavigatorEntry *nav_entry);
ThunarVfsInfo *
rstto_navigator_entry_get_info (RsttoNavigatorEntry *entry);
gboolean
rstto_navigator_entry_get_fit_to_screen (RsttoNavigatorEntry *entry);
gdouble
rstto_navigator_entry_get_scale (RsttoNavigatorEntry *entry);
void
rstto_navigator_entry_set_fit_to_screen (RsttoNavigatorEntry *entry, gboolean);
void
rstto_navigator_entry_set_scale (RsttoNavigatorEntry *entry, gdouble);
void
rstto_navigator_entry_set_rotation (RsttoNavigatorEntry *entry, GdkPixbufRotation rotation);
gint
rstto_navigator_entry_get_position(RsttoNavigatorEntry *entry);

GdkPixbufRotation
rstto_navigator_entry_get_rotation (RsttoNavigatorEntry *entry);
gboolean
rstto_navigator_entry_get_flip (RsttoNavigatorEntry *entry, gboolean horizontal);
void
rstto_navigator_flip_entry(RsttoNavigator *navigator, RsttoNavigatorEntry *entry, gboolean horizontal);
GdkPixbuf *
rstto_navigator_entry_get_thumb(RsttoNavigatorEntry *entry, gint size);
ExifData *
rstto_navigator_entry_get_exif_data (RsttoNavigatorEntry *entry);

GdkPixbufLoader *
rstto_navigator_entry_get_pixbuf_loader (RsttoNavigatorEntry *entry);
GdkPixbuf *
rstto_navigator_entry_get_pixbuf (RsttoNavigatorEntry *entry);
gboolean
rstto_navigator_entry_load_image (RsttoNavigatorEntry *entry, gboolean empty_cache);
void
rstto_navigator_entry_select (RsttoNavigatorEntry *entry);

guint64
rstto_navigator_entry_get_size (RsttoNavigatorEntry *entry);
gdouble
rstto_navigator_get_max_history_size(RsttoNavigator *navigator);
void
rstto_navigator_set_max_history_size(RsttoNavigator *nav, gdouble size);
void
rstto_navigator_set_monitor_handle_for_dir(RsttoNavigator *nav, ThunarVfsPath *dir_path);

gboolean
rstto_navigator_entry_is_selected(RsttoNavigatorEntry *entry);

gboolean
rstto_navigator_open_file(RsttoNavigator *navigator, const gchar *path, gboolean open_folder, GError **error);
gboolean
rstto_navigator_open_folder(RsttoNavigator *navigator, const gchar *path, gboolean clear, GError **error);

void
rstto_navigator_set_busy (RsttoNavigator *navigator, gboolean busy);

G_END_DECLS

#endif /* __RISTRETTO_NAVIGATOR_H__ */
