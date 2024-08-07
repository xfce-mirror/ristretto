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

#include "util.h"
#include "wallpaper_manager.h"



G_DEFINE_INTERFACE (RsttoWallpaperManager, rstto_wallpaper_manager, G_TYPE_OBJECT)



gint
rstto_wallpaper_manager_configure_dialog_run (RsttoWallpaperManager *self,
                                              RsttoFile *file,
                                              GtkWindow *parent)
{
    return RSTTO_WALLPAPER_MANAGER_GET_IFACE (self)->configure_dialog_run (self, file, parent);
}

gboolean
rstto_wallpaper_manager_check_running (RsttoWallpaperManager *self)
{
    return RSTTO_WALLPAPER_MANAGER_GET_IFACE (self)->check_running (self);
}

gboolean
rstto_wallpaper_manager_set (RsttoWallpaperManager *self,
                             RsttoFile *file)
{
    return RSTTO_WALLPAPER_MANAGER_GET_IFACE (self)->set (self, file);
}


static void
rstto_wallpaper_manager_default_init (RsttoWallpaperManagerInterface *klass)
{
}
