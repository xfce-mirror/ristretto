/*
 *  Copyright (c) Stephan Arts 2009-2011 <stephan@xfce.org>
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

static RsttoWallpaperManager *xfce_wallpaper_manager_object;

struct _RsttoXfceWallpaperManagerPriv
{
    XfconfChannel *channel;
    gint    screen;
    gint    monitor;
    gint    style;
    gdouble saturation;
    gint    brightness;
    RsttoColor *color1;
    RsttoColor *color2;
};


enum
{
    PROP_0,
};

static gint 
rstto_xfce_wallpaper_manager_configure_dialog_run (RsttoWallpaperManager *self, GFile *file)
{
    RsttoXfceWallpaperManager *manager = RSTTO_XFCE_WALLPAPER_MANAGER (self);
    gint response = GTK_RESPONSE_OK;
    gint i;
    GdkScreen *screen = gdk_screen_get_default ();
    gint n_monitors = gdk_screen_get_n_monitors (screen);
    GtkWidget *dialog = gtk_dialog_new_with_buttons (_("Set as wallpaper"), NULL, 0, GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL, GTK_STOCK_OK, GTK_RESPONSE_OK, NULL);
    GtkWidget *vbox = gtk_dialog_get_content_area (GTK_DIALOG (dialog));
    GtkWidget *style_label = gtk_label_new( _("Style:"));
    GtkWidget *style_combo = gtk_combo_box_new_text();
    GtkWidget *monitor_label = gtk_label_new( _("Monitor:"));
    GtkWidget *monitor_combo = gtk_combo_box_new_text();
    GtkWidget *brightness_label = gtk_label_new( _("Brightness:"));
    GtkWidget *saturation_label = gtk_label_new( _("Saturation:"));
    GtkObject *brightness_adjustment = gtk_adjustment_new (0.0, -128.0, 127.0, 1.0, 10.0, 0.0);
    GtkObject *saturation_adjustment = gtk_adjustment_new (1.0, 0.0, 10.0, 0.1, 0.5, 0);
    GtkWidget *brightness_slider = gtk_hscale_new (GTK_ADJUSTMENT (brightness_adjustment));
    GtkWidget *saturation_slider = gtk_hscale_new (GTK_ADJUSTMENT (saturation_adjustment));
    GtkWidget *image_hbox = gtk_hbox_new (FALSE, 4);
    GdkPixbuf *image_pixbuf = gtk_icon_theme_load_icon (gtk_icon_theme_get_default(),
                                                     "image-missing",
                                                     128,
                                                     0,
                                                     NULL);
    GtkWidget *image_box = gtk_image_new_from_pixbuf (image_pixbuf);
    GtkWidget *prop_table = gtk_table_new (1, 2, FALSE);
    GtkWidget *image_prop_table = gtk_table_new (2, 2, FALSE);

    gtk_widget_set_size_request (image_box, 128, 128);
    gtk_misc_set_padding (GTK_MISC (image_box), 4, 4);

    gtk_box_pack_start (GTK_BOX (vbox), image_hbox, FALSE, FALSE, 0);
    gtk_box_pack_start (GTK_BOX (image_hbox), image_box, FALSE, FALSE, 0);
    gtk_box_pack_start (GTK_BOX (image_hbox), prop_table, FALSE, FALSE, 0);
    gtk_table_attach (GTK_TABLE (prop_table), style_label, 0, 1, 0, 1, 0, 0, 0, 0);
    gtk_table_attach (GTK_TABLE (prop_table), style_combo, 1, 2, 0, 1, 0, 0, 0, 0);


    gtk_box_pack_start (GTK_BOX (vbox), image_prop_table, FALSE, FALSE, 0);

    gtk_scale_set_value_pos (GTK_SCALE (brightness_slider), GTK_POS_RIGHT);
    gtk_scale_set_digits (GTK_SCALE (brightness_slider), 0);
    gtk_scale_set_value_pos (GTK_SCALE (saturation_slider), GTK_POS_RIGHT);
    gtk_scale_set_digits (GTK_SCALE (saturation_slider), 1);
    gtk_table_attach (GTK_TABLE (image_prop_table), brightness_label, 0, 1, 0, 1, 0, 0, 0, 0);
    gtk_table_attach (GTK_TABLE (image_prop_table), brightness_slider, 1, 2, 0, 1, GTK_EXPAND|GTK_FILL, 0, 0, 0);
    gtk_table_attach (GTK_TABLE (image_prop_table), saturation_label, 0, 1, 1, 2, 0, 0, 0, 0);
    gtk_table_attach (GTK_TABLE (image_prop_table), saturation_slider, 1, 2, 1, 2, GTK_EXPAND|GTK_FILL, 0, 0, 0);

    gtk_combo_box_append_text (GTK_COMBO_BOX (style_combo), _("Auto"));
    gtk_combo_box_append_text (GTK_COMBO_BOX (style_combo), _("Centered"));
    gtk_combo_box_append_text (GTK_COMBO_BOX (style_combo), _("Tiled"));
    gtk_combo_box_append_text (GTK_COMBO_BOX (style_combo), _("Stretched"));
    gtk_combo_box_append_text (GTK_COMBO_BOX (style_combo), _("Scaled"));
    gtk_combo_box_append_text (GTK_COMBO_BOX (style_combo), _("Zoomed"));
    gtk_combo_box_set_active (GTK_COMBO_BOX (style_combo), 4);

    if (n_monitors > 1)
    {
        gtk_table_attach (GTK_TABLE (prop_table), monitor_label, 0, 1, 1, 2, 0, 0, 0, 0);
        gtk_table_attach (GTK_TABLE (prop_table), monitor_combo, 1, 2, 1, 2, 0, 0, 0, 0);
    }
    for (i = 0; i  < n_monitors; ++i)
    {
        gtk_combo_box_append_text (GTK_COMBO_BOX (monitor_combo), "1");
    }

    gtk_combo_box_set_active (GTK_COMBO_BOX (monitor_combo), 0);

    manager->priv->screen = gdk_screen_get_number (screen);

    gtk_window_set_resizable (GTK_WINDOW (dialog), FALSE);
    gtk_widget_show_all (vbox);
    response = gtk_dialog_run (GTK_DIALOG (dialog));
    gtk_widget_hide (dialog);
    if (response == GTK_RESPONSE_OK)
    {
        manager->priv->style = gtk_combo_box_get_active (GTK_COMBO_BOX (style_combo));
        manager->priv->saturation = gtk_adjustment_get_value (GTK_ADJUSTMENT (saturation_adjustment));
        manager->priv->brightness = (gint)gtk_adjustment_get_value (GTK_ADJUSTMENT (brightness_adjustment));
        manager->priv->monitor = gtk_combo_box_get_active (GTK_COMBO_BOX(monitor_combo));
    }

    gtk_widget_destroy (dialog);
    return response;
}

static gboolean
rstto_xfce_wallpaper_manager_check_running (RsttoWallpaperManager *self)
{
    gchar selection_name[100];
    Atom xfce_selection_atom;
    GdkScreen *gdk_screen = gdk_screen_get_default();
    gint xscreen = gdk_screen_get_number(gdk_screen);

    g_snprintf(selection_name, 100, XFDESKTOP_SELECTION_FMT, xscreen);

    xfce_selection_atom = XInternAtom (gdk_display, selection_name, False);
    if((XGetSelectionOwner(GDK_DISPLAY(), xfce_selection_atom)))
    {
        return TRUE;
    }
    return FALSE;
}

static gboolean
rstto_xfce_wallpaper_manager_set (RsttoWallpaperManager *self, GFile *file)
{
    RsttoXfceWallpaperManager *manager = RSTTO_XFCE_WALLPAPER_MANAGER (self);
    gchar *uri = g_file_get_path (file);

    gchar *image_path_prop = g_strdup_printf("/backdrop/screen%d/monitor%d/image-path",
                                        manager->priv->screen,
                                        manager->priv->monitor);
    gchar *image_show_prop = g_strdup_printf("/backdrop/screen%d/monitor%d/image-show",
                                        manager->priv->screen,
                                        manager->priv->monitor);
    gchar *image_style_prop = g_strdup_printf("/backdrop/screen%d/monitor%d/image-style",
                                        manager->priv->screen,
                                        manager->priv->monitor);
    gchar *brightness_prop = g_strdup_printf("/backdrop/screen%d/monitor%d/brightness",
                                        manager->priv->screen,
                                        manager->priv->monitor);
    gchar *saturation_prop = g_strdup_printf("/backdrop/screen%d/monitor%d/saturation",
                                        manager->priv->screen,
                                        manager->priv->monitor);

    gchar *color1_prop = g_strdup_printf("/backdrop/screen%d/monitor%d/color1",
                                        manager->priv->screen,
                                        manager->priv->monitor);
    gchar *color2_prop = g_strdup_printf("/backdrop/screen%d/monitor%d/color2",
                                        manager->priv->screen,
                                        manager->priv->monitor);

    xfconf_channel_set_string (manager->priv->channel,
                               image_path_prop, uri);
    xfconf_channel_set_bool   (manager->priv->channel,
                               image_show_prop, TRUE);
    xfconf_channel_set_int    (manager->priv->channel,
                               image_style_prop, manager->priv->style);

    xfconf_channel_set_int    (manager->priv->channel,
                               brightness_prop, manager->priv->brightness);
    xfconf_channel_set_double (manager->priv->channel,
                               saturation_prop,
                               manager->priv->saturation);

    xfconf_channel_set_struct (manager->priv->channel,
                               color1_prop,
                               manager->priv->color1,
                               XFCONF_TYPE_INT16, XFCONF_TYPE_INT16,
                               XFCONF_TYPE_INT16, XFCONF_TYPE_INT16,
                               G_TYPE_INVALID);
    xfconf_channel_set_struct (manager->priv->channel,
                               color2_prop,
                               manager->priv->color2,
                               XFCONF_TYPE_INT16, XFCONF_TYPE_INT16,
                               XFCONF_TYPE_INT16, XFCONF_TYPE_INT16,
                               G_TYPE_INVALID);

    g_free (image_path_prop);
    g_free (image_show_prop);
    g_free (image_style_prop);
    g_free (brightness_prop);
    g_free (saturation_prop);
    g_free (color1_prop);
    g_free (color2_prop);

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

        static const GInterfaceInfo wallpaper_manager_iface_info = 
        {
            (GInterfaceInitFunc) rstto_xfce_wallpaper_manager_iface_init,
            NULL,
            NULL
        };

        rstto_xfce_wallpaper_manager_type = g_type_register_static (G_TYPE_OBJECT, "RsttoXfceWallpaperManager", &rstto_xfce_wallpaper_manager_info, 0);
        g_type_add_interface_static (rstto_xfce_wallpaper_manager_type, RSTTO_WALLPAPER_MANAGER_TYPE,  &wallpaper_manager_iface_info);

    }
    return rstto_xfce_wallpaper_manager_type;
}


static void
rstto_xfce_wallpaper_manager_init (GObject *object)
{
    RsttoXfceWallpaperManager *xfce_wallpaper_manager = RSTTO_XFCE_WALLPAPER_MANAGER (object);

    xfce_wallpaper_manager->priv = g_new0 (RsttoXfceWallpaperManagerPriv, 1);
    xfce_wallpaper_manager->priv->channel = xfconf_channel_new ("xfce4-desktop");
    xfce_wallpaper_manager->priv->color1 = g_new0 (RsttoColor, 1);
    xfce_wallpaper_manager->priv->color1->a = 0xffff;
    xfce_wallpaper_manager->priv->color2 = g_new0 (RsttoColor, 1);
    xfce_wallpaper_manager->priv->color2->a = 0xffff;
    xfce_wallpaper_manager->priv->style = 4;
    xfce_wallpaper_manager->priv->brightness = 0;
    xfce_wallpaper_manager->priv->saturation = 1.0;
}


static void
rstto_xfce_wallpaper_manager_class_init (GObjectClass *object_class)
{
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

    if (xfce_wallpaper_manager->priv->channel)
    {
        g_object_unref (xfce_wallpaper_manager->priv->channel);
        xfce_wallpaper_manager->priv->channel = NULL;
    }
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
RsttoWallpaperManager *
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
