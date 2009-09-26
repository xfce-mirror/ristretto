/*
 *  Copyright (c) 2009 Stephan Arts <stephan@xfce.org>
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

#include <glib.h>
#include <gtk/gtk.h>
#include <gdk/gdkx.h>
#include <X11/Xlib.h>
#include <xfconf/xfconf.h>
#include <libxfce4util/libxfce4util.h>
#include <gio/gio.h>

#include "image.h"

#include "wallpaper_manager.h"
#include "xfce_wallpaper_manager.h"

#define XFDESKTOP_SELECTION_FMT "XFDESKTOP_SELECTION_%d"

typedef struct {
    gint16 r;
    gint16 g;
    gint16 b;
    gint16 a;
} RsttoColor;


static void
rstto_xfce_wallpaper_manager_init (GObject *);
static void
rstto_xfce_wallpaper_manager_class_init (GObjectClass *);

static void
rstto_xfce_wallpaper_manager_dispose (GObject *object);
static void
rstto_xfce_wallpaper_manager_finalize (GObject *object);

static GObjectClass *parent_class = NULL;

static RsttoXfceWallpaperManager *xfce_wallpaper_manager_object;

struct _RsttoXfceWallpaperManagerPriv
{
    XfconfChannel *channel;
};


enum
{
    PROP_0,
};

static gint 
rstto_xfce_wallpaper_manager_configure_dialog_run (RsttoWallpaperManager *self, RsttoImage *image)
{
    return GTK_RESPONSE_OK;
}

static gboolean
rstto_xfce_wallpaper_manager_check_running (RsttoWallpaperManager *self)
{
    gchar selection_name[100];

    GdkScreen *gdk_screen = gdk_screen_get_default();
    gint xscreen = gdk_screen_get_number(gdk_screen);

    g_snprintf(selection_name, 100, XFDESKTOP_SELECTION_FMT, xscreen);

    Atom xfce_selection_atom = XInternAtom (gdk_display, selection_name, False);
    if((XGetSelectionOwner(GDK_DISPLAY(), xfce_selection_atom)))
    {
        return TRUE;
    }
    return FALSE;
}

static gboolean
rstto_xfce_wallpaper_manager_set (RsttoWallpaperManager *self, RsttoImage *image)
{
    RsttoXfceWallpaperManager *manager = RSTTO_XFCE_WALLPAPER_MANAGER (self);
    GFile *file = rstto_image_get_file (image);
    gchar *uri = g_file_get_path (file);
    
    RsttoColor *color = g_new0 (RsttoColor, 1);
    color->a = 0xffff;

    xfconf_channel_set_string (manager->priv->channel, "/backdrop/screen0/monitor0/image-path", uri);
    xfconf_channel_set_bool   (manager->priv->channel, "/backdrop/screen0/monitor0/image-show", TRUE);
    xfconf_channel_set_int    (manager->priv->channel, "/backdrop/screen0/monitor0/image-style", 4);
    xfconf_channel_set_int    (manager->priv->channel, "/backdrop/screen0/monitor0/brightness", 0);
    xfconf_channel_set_double (manager->priv->channel, "/backdrop/screen0/monitor0/saturation", 1.0);
    xfconf_channel_set_struct (manager->priv->channel, "/backdrop/screen0/monitor0/color1", color, XFCONF_TYPE_INT16, XFCONF_TYPE_INT16, XFCONF_TYPE_INT16, XFCONF_TYPE_INT16, G_TYPE_INVALID);

    return FALSE;
}

static void
rstto_xfce_wallpaper_manager_iface_init (RsttoWallpaperManagerIface *iface)
{
    iface->configure_dialog_run = rstto_xfce_wallpaper_manager_configure_dialog_run;
    iface->check_running = rstto_xfce_wallpaper_manager_check_running;
    iface->set = rstto_xfce_wallpaper_manager_set;
}

GType
rstto_xfce_wallpaper_manager_get_type (void)
{
    static GType rstto_xfce_wallpaper_manager_type = 0;

    if (!rstto_xfce_wallpaper_manager_type)
    {
        static const GTypeInfo rstto_xfce_wallpaper_manager_info = 
        {
            sizeof (RsttoXfceWallpaperManagerClass),
            (GBaseInitFunc) NULL,
            (GBaseFinalizeFunc) NULL,
            (GClassInitFunc) rstto_xfce_wallpaper_manager_class_init,
            (GClassFinalizeFunc) NULL,
            NULL,
            sizeof (RsttoXfceWallpaperManager),
            0,
            (GInstanceInitFunc) rstto_xfce_wallpaper_manager_init,
            NULL
        };

        rstto_xfce_wallpaper_manager_type = g_type_register_static (G_TYPE_OBJECT, "RsttoXfceWallpaperManager", &rstto_xfce_wallpaper_manager_info, 0);

        static const GInterfaceInfo wallpaper_manager_iface_info = 
        {
            (GInstanceInitFunc) rstto_xfce_wallpaper_manager_iface_init,
            NULL,
            NULL
        };

        g_type_add_interface_static (rstto_xfce_wallpaper_manager_type, RSTTO_WALLPAPER_MANAGER_TYPE,  &wallpaper_manager_iface_info);

    }
    return rstto_xfce_wallpaper_manager_type;
}


static void
rstto_xfce_wallpaper_manager_init (GObject *object)
{
    gchar *accelmap_path = NULL;

    RsttoXfceWallpaperManager *xfce_wallpaper_manager = RSTTO_XFCE_WALLPAPER_MANAGER (object);

    xfce_wallpaper_manager->priv = g_new0 (RsttoXfceWallpaperManagerPriv, 1);
    xfce_wallpaper_manager->priv->channel = xfconf_channel_new ("xfce4-desktop");
}


static void
rstto_xfce_wallpaper_manager_class_init (GObjectClass *object_class)
{
    GParamSpec *pspec;

    RsttoXfceWallpaperManagerClass *xfce_wallpaper_manager_class = RSTTO_XFCE_WALLPAPER_MANAGER_CLASS (object_class);

    parent_class = g_type_class_peek_parent (xfce_wallpaper_manager_class);

    object_class->dispose = rstto_xfce_wallpaper_manager_dispose;
    object_class->finalize = rstto_xfce_wallpaper_manager_finalize;
}

/**
 * rstto_xfce_wallpaper_manager_dispose:
 * @object:
 *
 */
static void
rstto_xfce_wallpaper_manager_dispose (GObject *object)
{
    RsttoXfceWallpaperManager *xfce_wallpaper_manager = RSTTO_XFCE_WALLPAPER_MANAGER (object);

    if (xfce_wallpaper_manager->priv)
    {
        g_free (xfce_wallpaper_manager->priv);
        xfce_wallpaper_manager->priv = NULL;
    }
}

/**
 * rstto_xfce_wallpaper_manager_finalize:
 * @object:
 *
 */
static void
rstto_xfce_wallpaper_manager_finalize (GObject *object)
{
}



/**
 * rstto_xfce_wallpaper_manager_new:
 *
 *
 * Singleton
 */
RsttoXfceWallpaperManager *
rstto_xfce_wallpaper_manager_new (void)
{
    if (xfce_wallpaper_manager_object == NULL)
    {
        xfce_wallpaper_manager_object = g_object_new (RSTTO_TYPE_XFCE_WALLPAPER_MANAGER, NULL);
    }
    else
    {
        g_object_ref (xfce_wallpaper_manager_object);
    }

    return xfce_wallpaper_manager_object;
}
