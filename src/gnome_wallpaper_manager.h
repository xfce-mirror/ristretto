/*
 *  Copyright (c) Stephan Arts 2009-2010 <stephan@gnome.org>
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

#ifndef __RISTRETTO_GNOME_WALLPAPER_MANAGER_H__
#define __RISTRETTO_GNOME_WALLPAPER_MANAGER_H__

G_BEGIN_DECLS

#define RSTTO_TYPE_GNOME_WALLPAPER_MANAGER rstto_gnome_wallpaper_manager_get_type()

#define RSTTO_GNOME_WALLPAPER_MANAGER(obj)( \
        G_TYPE_CHECK_INSTANCE_CAST ((obj), \
                RSTTO_TYPE_GNOME_WALLPAPER_MANAGER, \
                RsttoGnomeWallpaperManager))

#define RSTTO_IS_GNOME_WALLPAPER_MANAGER(obj)( \
        G_TYPE_CHECK_INSTANCE_TYPE ((obj), \
                RSTTO_TYPE_GNOME_WALLPAPER_MANAGER))

#define RSTTO_GNOME_WALLPAPER_MANAGER_CLASS(klass)( \
        G_TYPE_CHECK_CLASS_CAST ((klass), \
                RSTTO_TYPE_GNOME_WALLPAPER_MANAGER, \
                RsttoGnomeWallpaperManagerClass))

#define RSTTO_IS_GNOME_WALLPAPER_MANAGER_CLASS(klass)( \
        G_TYPE_CHECK_CLASS_TYPE ((klass), \
                RSTTO_TYPE_GNOME_WALLPAPER_MANAGER()))


typedef struct _RsttoGnomeWallpaperManager RsttoGnomeWallpaperManager;
typedef struct _RsttoGnomeWallpaperManagerPriv RsttoGnomeWallpaperManagerPriv;

struct _RsttoGnomeWallpaperManager
{
    GObject parent;

    RsttoGnomeWallpaperManagerPriv *priv;
};

typedef struct _RsttoGnomeWallpaperManagerClass RsttoGnomeWallpaperManagerClass;

struct _RsttoGnomeWallpaperManagerClass
{
    GObjectClass parent_class;
};

RsttoGnomeWallpaperManager *rstto_gnome_wallpaper_manager_new (void);
GType          rstto_gnome_wallpaper_manager_get_type (void);

G_END_DECLS

#endif /* __RISTRETTO_GNOME_WALLPAPER_MANAGER_H__ */
