/*
 *  Copyright (c) Stephan Arts 2009-2010 <stephan@xfce.org>
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

#ifndef __RISTRETTO_WALLPAPER_MANAGER_IFACE__
#define __RISTRETTO_WALLPAPER_MANAGER_IFACE__

G_BEGIN_DECLS

#define RSTTO_WALLPAPER_MANAGER_TYPE    rstto_wallpaper_manager_get_type ()
#define RSTTO_WALLPAPER_MANAGER(obj)    (G_TYPE_CHECK_INSTANCE_CAST ((obj), RSTTO_WALLPAPER_MANAGER_TYPE, RsttoWallpaperManager))
#define RSTTO_IS_WALLPAPER_MANAGER(obj) (G_TYPE_CHECK_INSTANCE_TYPE ((obj), RSTTO_WALLPAPER_MANAGER_TYPE))
#define RSTTO_WALLPAPER_MANAGER_GET_IFACE(inst) (G_TYPE_INSTANCE_GET_INTERFACE ((inst), RSTTO_WALLPAPER_MANAGER_TYPE, RsttoWallpaperManagerIface))

typedef struct _RsttoWallpaperManager RsttoWallpaperManager; /* dummy object */
typedef struct _RsttoWallpaperManagerIface RsttoWallpaperManagerIface;

struct _RsttoWallpaperManagerIface {
    GTypeInterface parent;

    gint (*configure_dialog_run) (RsttoWallpaperManager *self, GFile *file);
    gboolean (*set) (RsttoWallpaperManager *self, GFile *file);
    gboolean (*check_running) (RsttoWallpaperManager *self);
};

GType rstto_wallpaper_manager_get_type (void);

gboolean rstto_wallpaper_manager_check_running (RsttoWallpaperManager *self);

gint
rstto_wallpaper_manager_configure_dialog_run (RsttoWallpaperManager *self, GFile *file);

gboolean
rstto_wallpaper_manager_set (RsttoWallpaperManager *self, GFile *file);

G_END_DECLS

#endif /* __RISTRETTO_WALLPAPER_MANAGER_IFACE__ */
