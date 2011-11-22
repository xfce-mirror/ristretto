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
 *
 *  The adjust-brightness code is written by
 *  Brian Tarricone <bjt23@cornell.edu>, originally written for xfdesktop.
 */

#include <config.h>

#include <glib.h>
#include <gtk/gtk.h>
#include <gdk/gdkx.h>
#include <X11/Xlib.h>
#include <xfconf/xfconf.h>
#include <libxfce4util/libxfce4util.h>
#include <gio/gio.h>

#include <gdk-pixbuf/gdk-pixbuf.h>
#include <gdk-pixbuf/gdk-pixdata.h>

#include <libexif/exif-data.h>

#include "util.h"
#include "file.h"
#include "monitor_chooser.h"
#include "wallpaper_manager.h"
#include "xfce_wallpaper_manager.h"

enum MonitorStyle
{
    MONITOR_STYLE_AUTOMATIC = 0,
    MONITOR_STYLE_CENTERED,
    MONITOR_STYLE_TILED,
    MONITOR_STYLE_STRETCHED,
    MONITOR_STYLE_SCALED,
    MONITOR_STYLE_ZOOMED
};


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

static void
configure_monitor_chooser_pixbuf (
    RsttoXfceWallpaperManager *manager );

static GdkPixbuf *
adjust_brightness (
        GdkPixbuf *src,
        gint amount );

static void
cb_style_combo_changed (
        GtkComboBox *style_combo,
        RsttoXfceWallpaperManager *manager);
static void
cb_monitor_chooser_changed (
        RsttoMonitorChooser *monitor_chooser,
        RsttoXfceWallpaperManager *manager);
static void
cb_brightness_adjustment_value_changed (
        GtkAdjustment *adjustment,
        RsttoXfceWallpaperManager *man);
static void
cb_saturation_adjustment_value_changed (
        GtkAdjustment *adjustment,
        RsttoXfceWallpaperManager *man);

static GObjectClass *parent_class = NULL;

static RsttoWallpaperManager *xfce_wallpaper_manager_object;

struct _RsttoXfceWallpaperManagerPriv
{
    XfconfChannel *channel;
    gint    screen;
    gint    monitor;
    enum MonitorStyle style;
    gdouble saturation;
    gint    brightness;
    RsttoColor *color1;
    RsttoColor *color2;

    RsttoFile *file;
    GdkPixbuf *pixbuf;

    GtkWidget *monitor_chooser;
    GtkWidget *style_combo;
    GtkObject *saturation_adjustment;
    GtkObject *brightness_adjustment;

    GtkWidget *dialog;
};


enum
{
    PROP_0,
};

static gint 
rstto_xfce_wallpaper_manager_configure_dialog_run (
        RsttoWallpaperManager *self,
        RsttoFile *file)
{
    gdouble saturation;
    gdouble brightness;
    RsttoXfceWallpaperManager *manager = RSTTO_XFCE_WALLPAPER_MANAGER (self);
    gint response = 0;
    manager->priv->file = file;
    saturation = gtk_adjustment_get_value (GTK_ADJUSTMENT(manager->priv->saturation_adjustment));
    brightness = gtk_adjustment_get_value (GTK_ADJUSTMENT(manager->priv->brightness_adjustment));

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
        manager->priv->saturation = gtk_adjustment_get_value (
                GTK_ADJUSTMENT (manager->priv->saturation_adjustment));
        manager->priv->brightness = (gint)gtk_adjustment_get_value (
                GTK_ADJUSTMENT (manager->priv->brightness_adjustment));
        manager->priv->monitor = rstto_monitor_chooser_get_selected (
                RSTTO_MONITOR_CHOOSER(manager->priv->monitor_chooser));
    }

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
rstto_xfce_wallpaper_manager_set (RsttoWallpaperManager *self, RsttoFile *file)
{
    RsttoXfceWallpaperManager *manager = RSTTO_XFCE_WALLPAPER_MANAGER (self);

    const gchar *uri = rstto_file_get_path (file);

    gchar *image_path_prop = g_strdup_printf (
            "/backdrop/screen%d/monitor%d/image-path",
            manager->priv->screen,
            manager->priv->monitor);
    gchar *image_show_prop = g_strdup_printf (
            "/backdrop/screen%d/monitor%d/image-show",
            manager->priv->screen,
            manager->priv->monitor);
    gchar *image_style_prop = g_strdup_printf (
            "/backdrop/screen%d/monitor%d/image-style",
            manager->priv->screen,
            manager->priv->monitor);
    gchar *brightness_prop = g_strdup_printf (
            "/backdrop/screen%d/monitor%d/brightness",
            manager->priv->screen,
            manager->priv->monitor);
    gchar *saturation_prop = g_strdup_printf (
            "/backdrop/screen%d/monitor%d/saturation",
            manager->priv->screen,
            manager->priv->monitor);

    gchar *color1_prop = g_strdup_printf (
            "/backdrop/screen%d/monitor%d/color1",
            manager->priv->screen,
            manager->priv->monitor);
    gchar *color2_prop = g_strdup_printf (
            "/backdrop/screen%d/monitor%d/color2",
            manager->priv->screen,
            manager->priv->monitor);

    xfconf_channel_set_string (
            manager->priv->channel,
            image_path_prop,
            uri);
    xfconf_channel_set_bool (
            manager->priv->channel,
            image_show_prop,
            TRUE);
    xfconf_channel_set_int (
            manager->priv->channel,
            image_style_prop,
            manager->priv->style);

    xfconf_channel_set_int (
            manager->priv->channel,
            brightness_prop,
            manager->priv->brightness);
    xfconf_channel_set_double (
            manager->priv->channel,
            saturation_prop,
            manager->priv->saturation);

    xfconf_channel_set_struct (
            manager->priv->channel,
            color1_prop,
            manager->priv->color1,
            XFCONF_TYPE_INT16, XFCONF_TYPE_INT16,
            XFCONF_TYPE_INT16, XFCONF_TYPE_INT16,
            G_TYPE_INVALID);
    xfconf_channel_set_struct (
            manager->priv->channel,
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

        rstto_xfce_wallpaper_manager_type = g_type_register_static (
                G_TYPE_OBJECT,
                "RsttoXfceWallpaperManager",
                &rstto_xfce_wallpaper_manager_info,
                0);

        g_type_add_interface_static (
                rstto_xfce_wallpaper_manager_type,
                RSTTO_WALLPAPER_MANAGER_TYPE,
                &wallpaper_manager_iface_info);

    }
    return rstto_xfce_wallpaper_manager_type;
}


static void
rstto_xfce_wallpaper_manager_init (GObject *object)
{
    RsttoXfceWallpaperManager *manager = RSTTO_XFCE_WALLPAPER_MANAGER (object);
    gint i;
    GdkScreen *screen = gdk_screen_get_default ();
    gint n_monitors = gdk_screen_get_n_monitors (screen);
    GdkRectangle monitor_geometry;
    GtkWidget *vbox;
    GtkWidget *style_label = gtk_label_new( _("Style:"));
    GtkWidget *brightness_label = gtk_label_new( _("Brightness:"));
    GtkWidget *saturation_label = gtk_label_new( _("Saturation:"));
    GtkWidget *brightness_slider;
    GtkWidget *saturation_slider;
    GtkWidget *image_prop_table = gtk_table_new (3, 2, FALSE);


    manager->priv = g_new0(RsttoXfceWallpaperManagerPriv, 1);
    manager->priv->channel = xfconf_channel_new ("xfce4-desktop");
    manager->priv->color1 = g_new0 (RsttoColor, 1);
    manager->priv->color1->a = 0xffff;
    manager->priv->color2 = g_new0 (RsttoColor, 1);
    manager->priv->color2->a = 0xffff;
    manager->priv->style = 4;
    manager->priv->brightness = 0;
    manager->priv->saturation = 1.0;

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
    vbox = gtk_dialog_get_content_area ( GTK_DIALOG (manager->priv->dialog));
    manager->priv->brightness_adjustment = gtk_adjustment_new (
            0.0,
            -128.0,
            127.0,
            1.0,
            10.0,
            0.0);
    manager->priv->saturation_adjustment = gtk_adjustment_new (
            1.0,
            -10.0,
            10.0,
            0.1,
            0.5,
            0.0);

    g_signal_connect (
            G_OBJECT(manager->priv->brightness_adjustment),
            "value-changed",
            G_CALLBACK (cb_brightness_adjustment_value_changed),
            manager);
    g_signal_connect (
            G_OBJECT(manager->priv->saturation_adjustment),
            "value-changed",
            G_CALLBACK (cb_saturation_adjustment_value_changed),
            manager);

    brightness_slider = gtk_hscale_new (
            GTK_ADJUSTMENT (manager->priv->brightness_adjustment));
    saturation_slider = gtk_hscale_new (
            GTK_ADJUSTMENT (manager->priv->saturation_adjustment));
    manager->priv->monitor_chooser = rstto_monitor_chooser_new ();
    manager->priv->style_combo = gtk_combo_box_new_text();

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

    gtk_scale_set_value_pos (
            GTK_SCALE (brightness_slider),
            GTK_POS_RIGHT);
    gtk_scale_set_digits (
            GTK_SCALE (brightness_slider),
            0);

    gtk_scale_set_value_pos (
            GTK_SCALE (saturation_slider),
            GTK_POS_RIGHT);
    gtk_scale_set_digits (
            GTK_SCALE (saturation_slider),
            1);
    gtk_table_attach (
            GTK_TABLE (image_prop_table),
            brightness_label,
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
            brightness_slider,
            1,
            2,
            0,
            1,
            GTK_EXPAND|GTK_FILL,
            0,
            0,
            0);
    gtk_table_attach (
            GTK_TABLE (image_prop_table),
            saturation_label,
            0,
            1,
            1,
            2,
            0,
            0,
            0,
            0);
    gtk_table_attach (
            GTK_TABLE (image_prop_table),
            saturation_slider,
            1,
            2,
            1,
            2,
            GTK_EXPAND|GTK_FILL,
            0,
            0,
            0);
    gtk_table_attach (
            GTK_TABLE (image_prop_table),
            style_label,
            0,
            1,
            2,
            3,
            0,
            0,
            0,
            0);
    gtk_table_attach (
            GTK_TABLE (image_prop_table),
            manager->priv->style_combo,
            1,
            2,
            2,
            3,
            GTK_EXPAND|GTK_FILL,
            0,
            0,
            0);

    gtk_combo_box_append_text (
            GTK_COMBO_BOX (manager->priv->style_combo),
            _("Auto"));
    gtk_combo_box_append_text (
            GTK_COMBO_BOX (manager->priv->style_combo),
            _("Centered"));
    gtk_combo_box_append_text (
            GTK_COMBO_BOX (manager->priv->style_combo),
            _("Tiled"));
    gtk_combo_box_append_text (
            GTK_COMBO_BOX (manager->priv->style_combo),
            _("Stretched"));
    gtk_combo_box_append_text (
            GTK_COMBO_BOX (manager->priv->style_combo),
            _("Scaled"));
    gtk_combo_box_append_text (
            GTK_COMBO_BOX (manager->priv->style_combo),
            _("Zoomed"));

    gtk_combo_box_set_active (
            GTK_COMBO_BOX (manager->priv->style_combo),
            4);

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
    if (xfce_wallpaper_manager_object)
    {
        xfce_wallpaper_manager_object = NULL;
    }
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
        xfce_wallpaper_manager_object = g_object_new (
                RSTTO_TYPE_XFCE_WALLPAPER_MANAGER,
                NULL);
    }
    else
    {
        g_object_ref (xfce_wallpaper_manager_object);
    }

    return xfce_wallpaper_manager_object;
}

static void
cb_style_combo_changed (
        GtkComboBox *style_combo,
        RsttoXfceWallpaperManager *manager)
{

    manager->priv->style = gtk_combo_box_get_active (style_combo);

    manager->priv->monitor = rstto_monitor_chooser_get_selected (
            RSTTO_MONITOR_CHOOSER (manager->priv->monitor_chooser));

    configure_monitor_chooser_pixbuf (manager);
}

static void
cb_monitor_chooser_changed (
        RsttoMonitorChooser *monitor_chooser,
        RsttoXfceWallpaperManager *manager)
{
    enum MonitorStyle style = MONITOR_STYLE_AUTOMATIC;

    style = gtk_combo_box_get_active (
            GTK_COMBO_BOX(manager->priv->style_combo));

    rstto_monitor_chooser_set_pixbuf (
            monitor_chooser,
            manager->priv->monitor,
            NULL,
            NULL);

    manager->priv->monitor = rstto_monitor_chooser_get_selected (monitor_chooser);

    configure_monitor_chooser_pixbuf (manager);
}

static void
cb_brightness_adjustment_value_changed (
        GtkAdjustment *adjustment,
        RsttoXfceWallpaperManager *manager)
{
    configure_monitor_chooser_pixbuf (manager);
}

static void
cb_saturation_adjustment_value_changed (
        GtkAdjustment *adjustment,
        RsttoXfceWallpaperManager *manager)
{
    configure_monitor_chooser_pixbuf (manager);
}

/** adjust_brightness:
 * @src: Source Pixbuf
 * @amount: Amount of brightness to be adjusted, a positive value means
 * increased brightness, a negative value means decreased brightness.
 *
 * The contents of this function are copied from xfdesktop, written by 
 * Brian Tarricone
 */
static GdkPixbuf *
adjust_brightness (
        GdkPixbuf *src,
        gint amount )
{
    GdkPixbuf *newpix;
    GdkPixdata pdata;
    gboolean has_alpha = FALSE;
    gint i, len;
    GError *err = NULL;
    
    g_return_val_if_fail(src != NULL, NULL);
    if(amount == 0)
    {
        return src;
    }
    
    gdk_pixdata_from_pixbuf(&pdata, src, FALSE);
    has_alpha = (pdata.pixdata_type & GDK_PIXDATA_COLOR_TYPE_RGBA);
    if(pdata.length < 1)
        len = pdata.width * pdata.height * (has_alpha?4:3);
    else
        len = pdata.length - GDK_PIXDATA_HEADER_LENGTH;
    
    for(i = 0; i < len; i++) {
        gshort scaled;
        
        if(has_alpha && (i+1)%4)
            continue;
        
        scaled = pdata.pixel_data[i] + amount;
        if(scaled > 255)
            scaled = 255;
        if(scaled < 0)
            scaled = 0;
        pdata.pixel_data[i] = scaled;
    }
    
    newpix = gdk_pixbuf_from_pixdata(&pdata, TRUE, &err);
    if(!newpix) {
        g_warning("%s: Unable to modify image brightness: %s", PACKAGE,
                err->message);
        g_error_free(err);
        return src;
    }
    g_object_unref(G_OBJECT(src));
    
    return newpix;
}

static void
configure_monitor_chooser_pixbuf (
    RsttoXfceWallpaperManager *manager )
{
    GdkPixbuf *monitor_pixbuf = NULL;
    GdkPixbuf *tmp_pixbuf = NULL;
    gint monitor_width = 0;
    gint monitor_height = 0;
    gdouble saturation = gtk_adjustment_get_value (GTK_ADJUSTMENT(manager->priv->saturation_adjustment));
    gdouble brightness = gtk_adjustment_get_value (GTK_ADJUSTMENT(manager->priv->brightness_adjustment));

    gint pixbuf_width = 0;
    gint pixbuf_height = 0;
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
            gdk_pixbuf_saturate_and_pixelate (
                tmp_pixbuf,
                tmp_pixbuf,
                saturation,
                FALSE);
            tmp_pixbuf = adjust_brightness (
                tmp_pixbuf,
                (gint)brightness);

            rstto_monitor_chooser_get_dimensions (
                    RSTTO_MONITOR_CHOOSER (manager->priv->monitor_chooser),
                    manager->priv->monitor,
                    &monitor_width,
                    &monitor_height);

            pixbuf_width = monitor_width * 0.2;
            pixbuf_height = monitor_height * 0.2;

            monitor_pixbuf = gdk_pixbuf_new (
                    GDK_COLORSPACE_RGB,     /* Colorspace       */
                    FALSE,                  /* has-alpha        */
                    8,                      /* bits per sample  */
                    pixbuf_width,           /* width            */
                    pixbuf_height);         /* height           */
            gdk_pixbuf_fill (
                    monitor_pixbuf,
                    0x00000000);

            switch (manager->priv->style)
            {
                case MONITOR_STYLE_ZOOMED:
                    dest_x = 0;
                    dest_y = 0;
                    dest_width = pixbuf_width;
                    dest_height = pixbuf_height;
                    x_scale = (gdouble)dest_width / (gdouble)gdk_pixbuf_get_width (tmp_pixbuf);
                    y_scale = (gdouble)dest_height / (gdouble)gdk_pixbuf_get_height (tmp_pixbuf);
                    if (x_scale > y_scale)
                    {
                        y_scale = x_scale;
                    }
                    else
                    {
                        x_scale = y_scale;
                    }
                    break;
                case MONITOR_STYLE_SCALED:
                    x_scale = (gdouble)pixbuf_width / (gdouble)gdk_pixbuf_get_width (tmp_pixbuf);
                    y_scale = (gdouble)pixbuf_height / (gdouble)gdk_pixbuf_get_height (tmp_pixbuf);
                    if (x_scale > y_scale)
                    {
                        x_scale = y_scale;
                    }
                    else
                    {
                        y_scale = x_scale;
                    }
                    dest_width = x_scale * (gdouble)gdk_pixbuf_get_width (tmp_pixbuf);
                    dest_height = y_scale * (gdouble)gdk_pixbuf_get_height (tmp_pixbuf);
                    dest_x = (gdouble)(dest_width - gdk_pixbuf_get_width (tmp_pixbuf)*x_scale) / 2;
                    dest_y = (gdouble)(dest_height - gdk_pixbuf_get_height (tmp_pixbuf)*y_scale) / 2;
                    break;
                case MONITOR_STYLE_AUTOMATIC:
                case MONITOR_STYLE_STRETCHED:
                default:
                    dest_x = 0;
                    dest_y = 0;
                    dest_width = pixbuf_width;
                    dest_height = pixbuf_height;
                    x_scale = (gdouble)dest_width / (gdouble)gdk_pixbuf_get_width (tmp_pixbuf);
                    y_scale = (gdouble)dest_height / (gdouble)gdk_pixbuf_get_height (tmp_pixbuf);
                    break;
            }

            gdk_pixbuf_composite (
                tmp_pixbuf,
                monitor_pixbuf,
                dest_x,
                dest_y,
                dest_width,
                dest_height,
                0.0,
                0.0,
                x_scale,
                y_scale,
                GDK_INTERP_BILINEAR,
                255);

            g_object_unref (tmp_pixbuf);
        }
    }

    rstto_monitor_chooser_set_pixbuf (
            RSTTO_MONITOR_CHOOSER(manager->priv->monitor_chooser),
            manager->priv->monitor,
            monitor_pixbuf,
            NULL);

    if ( NULL != monitor_pixbuf )
    {
       g_object_unref (monitor_pixbuf);
    }
}
