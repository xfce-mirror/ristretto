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

#include <config.h>
#include <string.h>

#include <glib.h>
#include <glib-object.h>
#include <gio/gio.h>

#include <libexif/exif-data.h>

#include "util.h"
#include "wallpaper_manager.h"

gint 
rstto_wallpaper_manager_configure_dialog_run (
        RsttoWallpaperManager *self,
        RsttoFile *file)
{
    return RSTTO_WALLPAPER_MANAGER_GET_IFACE (self)->configure_dialog_run(self, file);
}

gboolean 
rstto_wallpaper_manager_check_running (RsttoWallpaperManager *self)
{
    return RSTTO_WALLPAPER_MANAGER_GET_IFACE (self)->check_running (self);
}

gboolean
rstto_wallpaper_manager_set (
        RsttoWallpaperManager *self,
        RsttoFile *file)
{
    return RSTTO_WALLPAPER_MANAGER_GET_IFACE (self)->set (self, file);
}


static void
rstto_wallpaper_manager_iface_init (gpointer g_iface)
{
}


GType 
rstto_wallpaper_manager_get_type (void)
{
    static GType iface_type = 0;
    if (iface_type == 0)
    {
        static const GTypeInfo info = {
            /* other fields are initialized to 0 */
            .class_size = sizeof (RsttoWallpaperManagerIface),
            .base_init = rstto_wallpaper_manager_iface_init,
        };

        iface_type = g_type_register_static (
                G_TYPE_INTERFACE,
                "RsttoWallpaperManagerIface",
                &info, 0);
    }

    return iface_type;
}
