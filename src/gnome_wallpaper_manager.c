/*
 *  Copyright (c) Stephan Arts 2009-2011 <stephan@gnome.org>
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


#include <libexif/exif-data.h>

#include "util.h"
#include "file.h"

#include "wallpaper_manager.h"
#include "gnome_wallpaper_manager.h"

typedef struct {
    gint16 r;
    gint16 g;
    gint16 b;
    gint16 a;
} RsttoColor;

#define IMAGE_PATH_PROP "/desktop/gnome/background/picture_filename"
#define IMAGE_SHOW_PROP "/desktop/gnome/background/draw_background"

static void
rstto_gnome_wallpaper_manager_init (GObject *);
static void
rstto_gnome_wallpaper_manager_class_init (GObjectClass *);

static void
rstto_gnome_wallpaper_manager_dispose (GObject *object);
static void
rstto_gnome_wallpaper_manager_finalize (GObject *object);

static GObjectClass *parent_class = NULL;

static RsttoWallpaperManager *gnome_wallpaper_manager_object;

struct _RsttoGnomeWallpaperManagerPriv
{
    RsttoFile *file;

    GtkWidget *dialog;
};


enum
{
    PROP_0,
};

static gint 
rstto_gnome_wallpaper_manager_configure_dialog_run (
        RsttoWallpaperManager *self,
        RsttoFile *file)
{
    //RsttoGnomeWallpaperManager *manager = RSTTO_GNOME_WALLPAPER_MANAGER (self);
    gint response = GTK_RESPONSE_OK;

    //response = gtk_dialog_run (GTK_DIALOG(manager->priv->dialog));
    //gtk_widget_hide (manager->priv->dialog);
    return response;
}

static gboolean
rstto_gnome_wallpaper_manager_check_running (
        RsttoWallpaperManager *self)
{
    return TRUE;
}

static gboolean
rstto_gnome_wallpaper_manager_set (
        RsttoWallpaperManager *self,
        RsttoFile *file)
{
    gchar *command = NULL;
    gchar *escaped_file_name = g_shell_quote (
            rstto_file_get_path (file));

    command = g_strdup_printf (
            "gconftool-2 %s --set %s --type string",
            IMAGE_PATH_PROP,
            escaped_file_name);

    g_spawn_command_line_async (command, NULL);
    g_free (command);

    command = g_strdup_printf (
            "gconftool-2 %s --set true --type boolean",
            IMAGE_SHOW_PROP);

    g_spawn_command_line_async (command, NULL);
    g_free (command);

    return FALSE;
}

static void
rstto_gnome_wallpaper_manager_iface_init (RsttoWallpaperManagerIface *iface)
{
    iface->configure_dialog_run = rstto_gnome_wallpaper_manager_configure_dialog_run;
    iface->check_running = rstto_gnome_wallpaper_manager_check_running;
    iface->set = rstto_gnome_wallpaper_manager_set;
}

GType
rstto_gnome_wallpaper_manager_get_type (void)
{
    static GType rstto_gnome_wallpaper_manager_type = 0;

    if (!rstto_gnome_wallpaper_manager_type)
    {
        static const GTypeInfo rstto_gnome_wallpaper_manager_info = 
        {
            sizeof (RsttoGnomeWallpaperManagerClass),
            (GBaseInitFunc) NULL,
            (GBaseFinalizeFunc) NULL,
            (GClassInitFunc) rstto_gnome_wallpaper_manager_class_init,
            (GClassFinalizeFunc) NULL,
            NULL,
            sizeof (RsttoGnomeWallpaperManager),
            0,
            (GInstanceInitFunc) rstto_gnome_wallpaper_manager_init,
            NULL
        };

        static const GInterfaceInfo wallpaper_manager_iface_info = 
        {
            (GInterfaceInitFunc) rstto_gnome_wallpaper_manager_iface_init,
            NULL,
            NULL
        };

        rstto_gnome_wallpaper_manager_type = g_type_register_static (G_TYPE_OBJECT, "RsttoGnomeWallpaperManager", &rstto_gnome_wallpaper_manager_info, 0);
        g_type_add_interface_static (rstto_gnome_wallpaper_manager_type, RSTTO_WALLPAPER_MANAGER_TYPE,  &wallpaper_manager_iface_info);

    }
    return rstto_gnome_wallpaper_manager_type;
}


static void
rstto_gnome_wallpaper_manager_init (GObject *object)
{
    RsttoGnomeWallpaperManager *manager = RSTTO_GNOME_WALLPAPER_MANAGER (object);

    manager->priv = g_new0 (RsttoGnomeWallpaperManagerPriv, 1);

    manager->priv->dialog = gtk_dialog_new_with_buttons (
            _("Set as wallpaper"),
            NULL,
            0,
            GTK_STOCK_CANCEL,
            GTK_RESPONSE_CANCEL,
            GTK_STOCK_APPLY,
            GTK_RESPONSE_APPLY,
            GTK_STOCK_OK,
            GTK_RESPONSE_OK,
            NULL);
}


static void
rstto_gnome_wallpaper_manager_class_init (GObjectClass *object_class)
{
    RsttoGnomeWallpaperManagerClass *gnome_wallpaper_manager_class = RSTTO_GNOME_WALLPAPER_MANAGER_CLASS (object_class);

    parent_class = g_type_class_peek_parent (gnome_wallpaper_manager_class);

    object_class->dispose = rstto_gnome_wallpaper_manager_dispose;
    object_class->finalize = rstto_gnome_wallpaper_manager_finalize;
}

/**
 * rstto_gnome_wallpaper_manager_dispose:
 * @object:
 *
 */
static void
rstto_gnome_wallpaper_manager_dispose (GObject *object)
{
    RsttoGnomeWallpaperManager *gnome_wallpaper_manager = RSTTO_GNOME_WALLPAPER_MANAGER (object);

    if (gnome_wallpaper_manager->priv)
    {
        g_free (gnome_wallpaper_manager->priv);
        gnome_wallpaper_manager->priv = NULL;
    }
}

/**
 * rstto_gnome_wallpaper_manager_finalize:
 * @object:
 *
 */
static void
rstto_gnome_wallpaper_manager_finalize (GObject *object)
{
}



/**
 * rstto_gnome_wallpaper_manager_new:
 *
 *
 * Singleton
 */
RsttoWallpaperManager *
rstto_gnome_wallpaper_manager_new (void)
{
    if (gnome_wallpaper_manager_object == NULL)
    {
        gnome_wallpaper_manager_object = g_object_new (RSTTO_TYPE_GNOME_WALLPAPER_MANAGER, NULL);
    }
    else
    {
        g_object_ref (gnome_wallpaper_manager_object);
    }

    return gnome_wallpaper_manager_object;
}

