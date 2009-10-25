/*
 *  Copyright (C) Stephan Arts 2009 <stephan@xfce.org>
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

#include <config.h>
#include <string.h>

#include <glib.h>
#include <glib-object.h>
#include <gtk/gtk.h>
#include <gio/gio.h>



#include "image.h"

#include "wallpaper_manager.h"

gint 
rstto_wallpaper_manager_configure_dialog_run (RsttoWallpaperManager *self, RsttoImage *image)
{
    return RSTTO_WALLPAPER_MANAGER_GET_IFACE (self)->configure_dialog_run(self, image);
}

gboolean 
rstto_wallpaper_manager_check_running (RsttoWallpaperManager *self)
{
    return RSTTO_WALLPAPER_MANAGER_GET_IFACE (self)->check_running (self);
}

gboolean
rstto_wallpaper_manager_set (RsttoWallpaperManager *self, RsttoImage *image)
{
    return RSTTO_WALLPAPER_MANAGER_GET_IFACE (self)->set (self, image);
}


static void
rstto_wallpaper_manager_iface_init (gpointer g_iface)
{
    RsttoWallpaperManagerIface *iface = (RsttoWallpaperManagerIface *)g_iface;
    iface->configure_dialog_run = rstto_wallpaper_manager_configure_dialog_run;
}


GType 
rstto_wallpaper_manager_get_type (void)
{
    static GType iface_type = 0;
    if (iface_type == 0)
    {
        static const GTypeInfo info = {
            sizeof (RsttoWallpaperManagerIface),
            rstto_wallpaper_manager_iface_init,   /* base_init */
            NULL,   /* base_finalize */
        };

        iface_type = g_type_register_static (G_TYPE_INTERFACE, "RsttoWallpaperManagerIface",
                               &info, 0);
    }

    return iface_type;

}
