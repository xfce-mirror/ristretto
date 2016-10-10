/*
 *  Copyright (c) Stephan Arts 2006-2012 <stephan@xfce.org>
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version 2
 *  of the License, or (at your option) any later version.
 * 
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 * 
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 *  02110-1301, USA.
 */

#ifndef __RISTRETTO_WALLPAPER_MANAGER_IFACE__
#define __RISTRETTO_WALLPAPER_MANAGER_IFACE__

#include <gtk/gtk.h>

#include "file.h"

G_BEGIN_DECLS

#define RSTTO_WALLPAPER_MANAGER_TYPE \
        rstto_wallpaper_manager_get_type ()
#define RSTTO_WALLPAPER_MANAGER(obj)( \
        G_TYPE_CHECK_INSTANCE_CAST ((obj), \
                RSTTO_WALLPAPER_MANAGER_TYPE, \
                RsttoWallpaperManager))
#define RSTTO_IS_WALLPAPER_MANAGER(obj)( \
        G_TYPE_CHECK_INSTANCE_TYPE ((obj), \
                RSTTO_WALLPAPER_MANAGER_TYPE))
#define RSTTO_WALLPAPER_MANAGER_GET_IFACE(inst)( \
        G_TYPE_INSTANCE_GET_INTERFACE ((inst), \
                RSTTO_WALLPAPER_MANAGER_TYPE, \
                RsttoWallpaperManagerIface))

typedef struct _RsttoWallpaperManager RsttoWallpaperManager; /* dummy object */
typedef struct _RsttoWallpaperManagerIface RsttoWallpaperManagerIface;

struct _RsttoWallpaperManagerIface {
    GTypeInterface parent;

    gint (*configure_dialog_run) (RsttoWallpaperManager *self, RsttoFile *file);
    gboolean (*set) (RsttoWallpaperManager *self, RsttoFile *file);
    gboolean (*check_running) (RsttoWallpaperManager *self);
};

GType
rstto_wallpaper_manager_get_type (void);

gboolean
rstto_wallpaper_manager_check_running (RsttoWallpaperManager *self);

gint
rstto_wallpaper_manager_configure_dialog_run (
        RsttoWallpaperManager *self,
        RsttoFile *file);

gboolean
rstto_wallpaper_manager_set (
        RsttoWallpaperManager *self,
        RsttoFile *file);

G_END_DECLS

#endif /* __RISTRETTO_WALLPAPER_MANAGER_IFACE__ */
