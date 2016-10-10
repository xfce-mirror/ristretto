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

#include <gdk/gdkx.h>
#include <X11/Xlib.h>
#include <xfconf/xfconf.h>
#include <libxfce4util/libxfce4util.h>
#include <libxfce4ui/libxfce4ui.h>
#include <gio/gio.h>

#include <libexif/exif-data.h>

#include "util.h"
#include "file.h"
#include "monitor_chooser.h"
#include "gnome_wallpaper_manager.h"

enum MonitorStyle
{
    MONITOR_STYLE_AUTOMATIC = 0,
    MONITOR_STYLE_CENTERED,
    MONITOR_STYLE_TILED,
    MONITOR_STYLE_STRETCHED,
    MONITOR_STYLE_SCALED,
    MONITOR_STYLE_ZOOMED
};

enum ColorStyle
{
    COLOR_STYLE_SOLID = 0,
    COLOR_STYLE_HORIZONTAL_GRADIENT,
    COLOR_STYLE_VERTICAL_GRADIENT
};
	

typedef struct {
    gint16 r;
    gint16 g;
    gint16 b;
    gint16 a;
} RsttoColor;

static void
rstto_gnome_wallpaper_manager_init (GObject *);
static void
rstto_gnome_wallpaper_manager_class_init (GObjectClass *);

static void
rstto_gnome_wallpaper_manager_dispose (GObject *object);
static void
rstto_gnome_wallpaper_manager_finalize (GObject *object);

static void
cb_monitor_chooser_changed (
        RsttoMonitorChooser *monitor_chooser,
        RsttoGnomeWallpaperManager *manager);
static void
cb_style_combo_changed (
        GtkComboBox *style_combo,
        RsttoGnomeWallpaperManager *manager);

static void
configure_monitor_chooser_pixbuf (
    RsttoGnomeWallpaperManager *manager );

static GObjectClass *parent_class = NULL;

static RsttoWallpaperManager *gnome_wallpaper_manager_object = NULL;

struct _RsttoGnomeWallpaperManagerPriv
{
    gint screen;
    gint    monitor;
    enum MonitorStyle style;

    RsttoFile *file;
    GdkPixbuf *pixbuf;
	
    GtkWidget *monitor_chooser;
    GtkWidget *style_combo;

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
    RsttoGnomeWallpaperManager *manager = RSTTO_GNOME_WALLPAPER_MANAGER (self);
    gint response = GTK_RESPONSE_OK;
    manager->priv->file = file;

    if (manager->priv->pixbuf)
    {
        g_object_unref (manager->priv->pixbuf);
    }

    manager->priv->pixbuf = gdk_pixbuf_new_from_file_at_size (
            rstto_file_get_path(file),
            500,
            500,
            NULL);

    configure_monitor_chooser_pixbuf (manager);

    response = gtk_dialog_run (GTK_DIALOG (manager->priv->dialog));
    gtk_widget_hide (manager->priv->dialog);
    if ((response == GTK_RESPONSE_OK) || (response == GTK_RESPONSE_APPLY))
    {
        manager->priv->style = gtk_combo_box_get_active (
                GTK_COMBO_BOX (manager->priv->style_combo));
        manager->priv->monitor = rstto_monitor_chooser_get_selected (
                RSTTO_MONITOR_CHOOSER(manager->priv->monitor_chooser));
    }

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

    command = g_strdup_printf (
            "gsettings set org.gnome.desktop.background picture-uri %s",
            rstto_file_get_uri (file));

    g_spawn_command_line_async (command, NULL);
    g_free (command);

    command = g_strdup_printf (
            "gsettings set org.gnome.desktop.background picture-options %s",
            "scaled");

    g_spawn_command_line_async (command, NULL);
    g_free (command);

    command = g_strdup_printf (
            "gsettings set org.gnome.desktop.background primary-color '#000000000000'");

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
    GtkWidget *image_prop_table = gtk_table_new (3, 2, FALSE);
    GtkWidget *style_label = gtk_label_new( _("Style:"));
    GtkWidget *vbox;

    GdkScreen *screen = gdk_screen_get_default ();
    gint n_monitors = gdk_screen_get_n_monitors (screen);
    GdkRectangle monitor_geometry;
    gint i;

    manager->priv = g_new0 (RsttoGnomeWallpaperManagerPriv, 1);

    manager->priv->dialog = gtk_dialog_new_with_buttons (
            _("Set as wallpaper"),
            NULL,
            0,
            _("_Cancel"),
            GTK_RESPONSE_CANCEL,
            _("_Apply"),
            GTK_RESPONSE_APPLY,
            _("_OK"),
            GTK_RESPONSE_OK,
            NULL);

    vbox = gtk_dialog_get_content_area ( GTK_DIALOG (manager->priv->dialog));

    manager->priv->monitor_chooser = rstto_monitor_chooser_new ();
    manager->priv->style_combo = gtk_combo_box_text_new ();

    gtk_table_set_row_spacing (GTK_TABLE(image_prop_table), 1, 4);

    for (i = 0; i < n_monitors; ++i)
    {
        gdk_screen_get_monitor_geometry (
                screen,
                i,
                &monitor_geometry);
        rstto_monitor_chooser_add (
                RSTTO_MONITOR_CHOOSER (manager->priv->monitor_chooser),
                monitor_geometry.width,
                monitor_geometry.height);
    }
    gtk_box_pack_start (
            GTK_BOX (vbox),
            manager->priv->monitor_chooser,
            FALSE,
            FALSE,
            0);
    gtk_box_pack_start (
            GTK_BOX (vbox),
            image_prop_table,
            FALSE,
            FALSE,
            0);

    gtk_table_attach (
            GTK_TABLE (image_prop_table),
            style_label,
            0,
            1,
            0,
            1,
            0,
            0,
            0,
            0);
    gtk_table_attach (
            GTK_TABLE (image_prop_table),
            manager->priv->style_combo,
            1,
            2,
            0,
            1,
            GTK_EXPAND|GTK_FILL,
            0,
            0,
            0);

    gtk_combo_box_text_append_text (
            GTK_COMBO_BOX_TEXT (manager->priv->style_combo),
            _("Auto"));

    gtk_combo_box_set_active (
            GTK_COMBO_BOX (manager->priv->style_combo),
            0);

    manager->priv->screen = gdk_screen_get_number (screen);

    gtk_window_set_resizable (GTK_WINDOW (manager->priv->dialog), FALSE);

    g_signal_connect (
            G_OBJECT(manager->priv->monitor_chooser),
            "changed",
            G_CALLBACK (cb_monitor_chooser_changed),
            manager);
    g_signal_connect (
            G_OBJECT(manager->priv->style_combo),
            "changed",
            G_CALLBACK (cb_style_combo_changed),
            manager);

    gtk_widget_show_all (vbox);
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
    if (gnome_wallpaper_manager_object)
    {
        gnome_wallpaper_manager_object = NULL;
    }
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


static void
cb_monitor_chooser_changed (
        RsttoMonitorChooser *monitor_chooser,
        RsttoGnomeWallpaperManager *manager)
{
    rstto_monitor_chooser_set_image_surface (
            monitor_chooser,
            manager->priv->monitor,
            NULL,
            NULL);

    manager->priv->monitor = rstto_monitor_chooser_get_selected (monitor_chooser);

    configure_monitor_chooser_pixbuf (manager);
}

static void
configure_monitor_chooser_pixbuf (
    RsttoGnomeWallpaperManager *manager )
{
    cairo_surface_t *image_surface = NULL;
    cairo_t *ctx;
    //GdkColor bg_color;

    GdkPixbuf *tmp_pixbuf = NULL;

    gint monitor_width = 0;
    gint monitor_height = 0;

    gint surface_width = 0;
    gint surface_height = 0;
    gint dest_x = 0;
    gint dest_y = 0;
    gint dest_width = 0;
    gint dest_height = 0;
    gdouble x_scale = 0.0;
    gdouble y_scale = 0.0;

    if (manager->priv->pixbuf)
    {
        tmp_pixbuf = gdk_pixbuf_copy (manager->priv->pixbuf);
        if ( NULL != tmp_pixbuf )
        {

            rstto_monitor_chooser_get_dimensions (
                    RSTTO_MONITOR_CHOOSER (manager->priv->monitor_chooser),
                    manager->priv->monitor,
                    &monitor_width,
                    &monitor_height);

            surface_width = monitor_width * 0.2;
            surface_height = monitor_height * 0.2;

            image_surface = cairo_image_surface_create (
                    CAIRO_FORMAT_ARGB32,
                    surface_width,
                    surface_height);
            ctx = cairo_create ( image_surface );

            //gdk_cairo_set_source_color ( ctx, bg_color );
            cairo_set_source_rgb (ctx, 0, 0, 0);
            cairo_paint (ctx);

            x_scale = (gdouble)surface_width / (gdouble)gdk_pixbuf_get_width (tmp_pixbuf);
            y_scale = (gdouble)surface_height / (gdouble)gdk_pixbuf_get_height (tmp_pixbuf);

            switch (manager->priv->style)
            {
                case MONITOR_STYLE_ZOOMED:
                    if (x_scale > y_scale)
                    {
                        dest_width = (gint)((gdouble)gdk_pixbuf_get_width (tmp_pixbuf) * x_scale);
                        dest_height = (gint)((gdouble)gdk_pixbuf_get_height (tmp_pixbuf) * x_scale);
                        dest_x = (gint)((gdouble)(surface_width - dest_width) / 2);
                        dest_y = (gint)((gdouble)(surface_height - dest_height) / 2);
                        y_scale = x_scale;
                    }
                    else
                    {
                        dest_width = (gint)((gdouble)gdk_pixbuf_get_width (tmp_pixbuf) * y_scale);
                        dest_height = (gint)((gdouble)gdk_pixbuf_get_height (tmp_pixbuf) * y_scale);
                        dest_x = (gint)((gdouble)(surface_width - dest_width) / 2);
                        dest_y = (gint)((gdouble)(surface_height - dest_height) / 2);
                        x_scale = y_scale;
                    }
                    break;
                case MONITOR_STYLE_STRETCHED:
                    dest_x = 0.0;
                    dest_y = 0.0;
                    break;
                case MONITOR_STYLE_AUTOMATIC:
                case MONITOR_STYLE_SCALED:
                    if (x_scale < y_scale)
                    {
                        dest_width = (gint)((gdouble)gdk_pixbuf_get_width (tmp_pixbuf) * x_scale);
                        dest_height = (gint)((gdouble)gdk_pixbuf_get_height (tmp_pixbuf) * x_scale);
                        dest_x = (gint)((gdouble)(surface_width - dest_width) / 2);
                        dest_y = (gint)((gdouble)(surface_height - dest_height) / 2);
                        y_scale = x_scale;
                    }
                    else
                    {
                        dest_width = (gint)((gdouble)gdk_pixbuf_get_width (tmp_pixbuf) * y_scale);
                        dest_height = (gint)((gdouble)gdk_pixbuf_get_height (tmp_pixbuf) * y_scale);
                        dest_x = (gint)((gdouble)(surface_width - dest_width) / 2);
                        dest_y = (gint)((gdouble)(surface_height - dest_height) / 2);
                        x_scale = y_scale;
                    }
                    break;
                default:
                    gdk_pixbuf_saturate_and_pixelate (
                        tmp_pixbuf,
                        tmp_pixbuf,
                        0.0,
                        TRUE);
                    break;
            }
            cairo_scale (ctx, x_scale, y_scale);
            gdk_cairo_set_source_pixbuf (
                    ctx,
                    tmp_pixbuf,
                    dest_x/x_scale,
                    dest_y/y_scale);
            cairo_paint (ctx);
            cairo_destroy (ctx);


            g_object_unref (tmp_pixbuf);
        }
    }

    rstto_monitor_chooser_set_image_surface (
            RSTTO_MONITOR_CHOOSER (manager->priv->monitor_chooser),
            manager->priv->monitor,
            image_surface,
            NULL);
}


static void
cb_style_combo_changed (
        GtkComboBox *style_combo,
        RsttoGnomeWallpaperManager *manager)
{

    manager->priv->style = gtk_combo_box_get_active (style_combo);

    manager->priv->monitor = rstto_monitor_chooser_get_selected (
            RSTTO_MONITOR_CHOOSER (manager->priv->monitor_chooser));

    configure_monitor_chooser_pixbuf (manager);
}

