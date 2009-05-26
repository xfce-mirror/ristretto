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
#include <gtk/gtk.h>
#include <libxfcegui4/libxfcegui4.h>
#include <libxfce4util/libxfce4util.h>

#include "settings.h"
#include "preferences_dialog.h"

#ifndef RSTTO_MIN_CACHE_SIZE
#define RSTTO_MIN_CACHE_SIZE 16
#endif

#ifndef RSTTO_DEFAULT_CACHE_SIZE
#define RSTTO_DEFAULT_CACHE_SIZE 256
#endif

static void
rstto_preferences_dialog_init(RsttoPreferencesDialog *);
static void
rstto_preferences_dialog_class_init(RsttoPreferencesDialogClass *);


static void
cb_rstto_preferences_dialog_bgcolor_override_toggled (GtkToggleButton *, gpointer);
static void
cb_rstto_preferences_dialog_bgcolor_color_set (GtkColorButton *, gpointer);
static void
cb_rstto_preferences_dialog_cache_check_button_toggled (GtkToggleButton *, gpointer);
static void
cb_rstto_preferences_dialog_cache_preload_hscale_value_changed (GtkRange *range, 
                                                                gpointer user_data);
static void
cb_rstto_preferences_dialog_cache_spin_button_value_changed (GtkSpinButton *, gpointer);
static void
cb_rstto_preferences_dialog_image_quality_combo_box_changed (GtkComboBox *, gpointer);

static GtkWidgetClass *parent_class = NULL;

struct _RsttoPreferencesDialogPriv
{
    RsttoSettings *settings;

    struct
    {
        GtkWidget *bgcolor_frame;
        GtkWidget *bgcolor_vbox;
        GtkWidget *bgcolor_hbox;
        GtkWidget *bgcolor_color_button;
        GtkWidget *bgcolor_override_check_button;


        GtkWidget *image_quality_frame;
        GtkWidget *image_quality_vbox;
        GtkWidget *image_quality_hbox;
        GtkWidget *image_quality_label;
        GtkWidget *image_quality_combo;
    } display_tab;

    struct
    {
    } control_tab;

    struct
    {
        GtkWidget *cache_frame;
        GtkWidget *cache_vbox;
        GtkWidget *cache_sub_vbox;
        GtkWidget *cache_hbox;
        GtkWidget *cache_size_label;
        GtkWidget *cache_size_unit;
        GtkWidget *cache_check_button;
        GtkWidget *cache_alignment;
        GtkWidget *cache_spin_button;
        GtkWidget *cache_preload_label;
        GtkWidget *cache_preload_hscale;
    } behaviour_tab;
};

GType
rstto_preferences_dialog_get_type ()
{
    static GType rstto_preferences_dialog_type = 0;

    if (!rstto_preferences_dialog_type)
    {
        static const GTypeInfo rstto_preferences_dialog_info = 
        {
            sizeof (RsttoPreferencesDialogClass),
            (GBaseInitFunc) NULL,
            (GBaseFinalizeFunc) NULL,
            (GClassInitFunc) rstto_preferences_dialog_class_init,
            (GClassFinalizeFunc) NULL,
            NULL,
            sizeof (RsttoPreferencesDialog),
            0,
            (GInstanceInitFunc) rstto_preferences_dialog_init,
            NULL
        };

        rstto_preferences_dialog_type = g_type_register_static (XFCE_TYPE_TITLED_DIALOG, "RsttoPreferencesDialog", &rstto_preferences_dialog_info, 0);
    }
    return rstto_preferences_dialog_type;
}

static void
rstto_preferences_dialog_init(RsttoPreferencesDialog *dialog)
{
    dialog->priv = g_new0 (RsttoPreferencesDialogPriv, 1);

    dialog->priv->settings = rstto_settings_new ();
    guint uint_image_quality;
    guint uint_cache_size;
    guint uint_preload_images;
    gboolean bool_enable_cache;
    gboolean bool_bgcolor_override;
    GdkColor *bgcolor;

    g_object_get (G_OBJECT (dialog->priv->settings),
                  "image-quality", &uint_image_quality,
                  "cache-size", &uint_cache_size,
                  "preload-images", &uint_preload_images,
                  "enable-cache", &bool_enable_cache,
                  "bgcolor-override", &bool_bgcolor_override,
                  "bgcolor", &bgcolor,
                  NULL);

    GtkObject *cache_adjustment;

    GtkWidget *notebook = gtk_notebook_new ();
    GtkWidget *scroll_frame, *scroll_vbox;
    GtkWidget *timeout_frame, *timeout_vbox, *timeout_lbl, *timeout_hscale;
    GtkWidget *scaling_frame, *scaling_vbox;
    GtkWidget *toolbar_vbox, *toolbar_frame;

    GtkWidget *widget;

/*****************/
/** DISPLAY TAB **/
/*****************/
    GtkWidget *display_main_vbox = gtk_vbox_new(FALSE, 0);
    GtkWidget *display_main_lbl = gtk_label_new(_("Display"));
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), display_main_vbox, display_main_lbl);

/** Bg-color frame */
    dialog->priv->display_tab.bgcolor_vbox = gtk_vbox_new (FALSE, 0);
    dialog->priv->display_tab.bgcolor_frame = xfce_create_framebox_with_content (_("Background color"),
                                                                                 dialog->priv->display_tab.bgcolor_vbox);
    gtk_box_pack_start (GTK_BOX (display_main_vbox), dialog->priv->display_tab.bgcolor_frame, FALSE, FALSE, 0);

    dialog->priv->display_tab.bgcolor_override_check_button = gtk_check_button_new_with_label (_("Override background color:"));
    dialog->priv->display_tab.bgcolor_hbox = gtk_hbox_new (FALSE, 4);
    dialog->priv->display_tab.bgcolor_color_button = gtk_color_button_new();

    gtk_box_pack_start (GTK_BOX (dialog->priv->display_tab.bgcolor_hbox), 
                        dialog->priv->display_tab.bgcolor_override_check_button, FALSE, FALSE, 0);
    gtk_box_pack_start (GTK_BOX (dialog->priv->display_tab.bgcolor_hbox),
                        dialog->priv->display_tab.bgcolor_color_button, FALSE, FALSE, 0);
    gtk_box_pack_start (GTK_BOX (dialog->priv->display_tab.bgcolor_vbox), 
                        dialog->priv->display_tab.bgcolor_hbox, FALSE, FALSE, 0);

    /* set current value */
    gtk_color_button_set_color (GTK_COLOR_BUTTON (dialog->priv->display_tab.bgcolor_color_button),
                                bgcolor);

    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (dialog->priv->display_tab.bgcolor_override_check_button),
                                  bool_bgcolor_override);
    gtk_widget_set_sensitive (GTK_WIDGET (dialog->priv->display_tab.bgcolor_color_button),
                              bool_bgcolor_override);
    
    /* connect signals */
    g_signal_connect (G_OBJECT (dialog->priv->display_tab.bgcolor_override_check_button), 
                      "toggled", (GCallback)cb_rstto_preferences_dialog_bgcolor_override_toggled, dialog);
    g_signal_connect (G_OBJECT (dialog->priv->display_tab.bgcolor_color_button), 
                      "color-set", G_CALLBACK (cb_rstto_preferences_dialog_bgcolor_color_set), dialog);

/** Image-quality frame */
    dialog->priv->display_tab.image_quality_vbox = gtk_vbox_new(FALSE, 0);
    dialog->priv->display_tab.image_quality_frame = xfce_create_framebox_with_content (_("Quality"),
                                                                                 dialog->priv->display_tab.image_quality_vbox);
    gtk_box_pack_start (GTK_BOX (display_main_vbox), dialog->priv->display_tab.image_quality_frame, FALSE, FALSE, 0);

    dialog->priv->display_tab.image_quality_label = gtk_label_new (_("Maximum render quality:"));
    dialog->priv->display_tab.image_quality_hbox= gtk_hbox_new (FALSE, 4);
    dialog->priv->display_tab.image_quality_combo= gtk_combo_box_new_text ();

    gtk_combo_box_append_text (GTK_COMBO_BOX (dialog->priv->display_tab.image_quality_combo), _("Really High"));
    gtk_combo_box_append_text (GTK_COMBO_BOX (dialog->priv->display_tab.image_quality_combo), _("High"));
    gtk_combo_box_append_text (GTK_COMBO_BOX (dialog->priv->display_tab.image_quality_combo), _("Medium"));
    gtk_combo_box_append_text (GTK_COMBO_BOX (dialog->priv->display_tab.image_quality_combo), _("Low"));

    gtk_box_pack_start (GTK_BOX (dialog->priv->display_tab.image_quality_vbox), 
                                 dialog->priv->display_tab.image_quality_hbox, FALSE, FALSE, 0);
    gtk_box_pack_start (GTK_BOX (dialog->priv->display_tab.image_quality_hbox), 
                                 dialog->priv->display_tab.image_quality_label, FALSE, FALSE, 0);
    gtk_box_pack_start (GTK_BOX (dialog->priv->display_tab.image_quality_hbox), 
                                 dialog->priv->display_tab.image_quality_combo, FALSE, FALSE, 0);
    /* set current value */
    switch (uint_image_quality-(uint_image_quality%1000000))
    {
        case 0:
            gtk_combo_box_set_active (GTK_COMBO_BOX (dialog->priv->display_tab.image_quality_combo), 0);
            break;
        case 8000000:
            gtk_combo_box_set_active (GTK_COMBO_BOX (dialog->priv->display_tab.image_quality_combo), 1);
            break;
        case 4000000:
            gtk_combo_box_set_active (GTK_COMBO_BOX (dialog->priv->display_tab.image_quality_combo), 2);
            break;
        case 2000000:
            gtk_combo_box_set_active (GTK_COMBO_BOX (dialog->priv->display_tab.image_quality_combo), 3);
            break;
        default:
            gtk_combo_box_set_active (GTK_COMBO_BOX (dialog->priv->display_tab.image_quality_combo), 2);
            break;
    }

    /* connect signals */
    g_signal_connect (G_OBJECT (dialog->priv->display_tab.image_quality_combo), 
                      "changed", (GCallback)cb_rstto_preferences_dialog_image_quality_combo_box_changed, dialog);
    

/*******************/
/** Slideshow tab **/
/*******************/
    GtkWidget *slideshow_main_vbox = gtk_vbox_new(FALSE, 0);
    GtkWidget *slideshow_main_lbl = gtk_label_new(_("Slideshow"));
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), slideshow_main_vbox, slideshow_main_lbl);
    /* not used */
    gtk_widget_set_sensitive (slideshow_main_vbox, FALSE);

    timeout_vbox = gtk_vbox_new(FALSE, 0);
    timeout_frame = xfce_create_framebox_with_content (_("Timeout"), timeout_vbox);
    gtk_box_pack_start (GTK_BOX (slideshow_main_vbox), timeout_frame, FALSE, FALSE, 0);

    
    timeout_lbl = gtk_label_new(_("The time period an individual image is displayed during a slideshow\n(in seconds)"));
    timeout_hscale = gtk_hscale_new_with_range(1, 60, 1);
    gtk_misc_set_alignment(GTK_MISC(timeout_lbl), 0, 0.5);
    gtk_misc_set_padding(GTK_MISC(timeout_lbl), 2, 2);

    gtk_box_pack_start(GTK_BOX(timeout_vbox), timeout_lbl, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(timeout_vbox), timeout_hscale, FALSE, TRUE, 0);


/********************************************/
    GtkWidget *control_main_vbox = gtk_vbox_new(FALSE, 0);
    GtkWidget *control_main_lbl = gtk_label_new(_("Control"));
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), control_main_vbox, control_main_lbl);

    scroll_vbox = gtk_vbox_new(FALSE, 0);
    scroll_frame = xfce_create_framebox_with_content (_("Scrollwheel"), scroll_vbox);
    gtk_box_pack_start (GTK_BOX (control_main_vbox), scroll_frame, FALSE, FALSE, 0);
    gtk_widget_set_sensitive (scroll_vbox, FALSE);

    widget = gtk_radio_button_new_with_label (NULL, _("No action"));
    gtk_container_add (GTK_CONTAINER (scroll_vbox), widget);
    widget = gtk_radio_button_new_with_label_from_widget (GTK_RADIO_BUTTON (widget), _("Zoom in and out"));
    gtk_container_add (GTK_CONTAINER (scroll_vbox), widget);
    widget = gtk_radio_button_new_with_label_from_widget (GTK_RADIO_BUTTON (widget), _("Switch images"));
    gtk_container_add (GTK_CONTAINER (scroll_vbox), widget);

/*******************/
/** Behaviour tab **/
/*******************/
    GtkWidget *behaviour_main_vbox = gtk_vbox_new(FALSE, 0);
    GtkWidget *behaviour_main_lbl = gtk_label_new(_("Behaviour"));
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), behaviour_main_vbox, behaviour_main_lbl);


/** Image-cache frame */
    dialog->priv->behaviour_tab.cache_vbox = gtk_vbox_new(FALSE, 0);
    dialog->priv->behaviour_tab.cache_frame = xfce_create_framebox_with_content (_("Image cache"),
                                                                                 dialog->priv->behaviour_tab.cache_vbox);
    gtk_box_pack_start (GTK_BOX (behaviour_main_vbox), dialog->priv->behaviour_tab.cache_frame, FALSE, FALSE, 0);

    cache_adjustment = gtk_adjustment_new (RSTTO_DEFAULT_CACHE_SIZE, RSTTO_MIN_CACHE_SIZE, 4096, 1, 0, 0);

    dialog->priv->behaviour_tab.cache_size_label = gtk_label_new (_("Cache size"));
    dialog->priv->behaviour_tab.cache_size_unit = gtk_label_new (_("MB"));
    dialog->priv->behaviour_tab.cache_hbox = gtk_hbox_new (FALSE, 4);
    dialog->priv->behaviour_tab.cache_sub_vbox = gtk_vbox_new (FALSE, 4);
    dialog->priv->behaviour_tab.cache_check_button = gtk_check_button_new_with_label (_("Enable cache"));
    dialog->priv->behaviour_tab.cache_alignment = gtk_alignment_new (0, 0, 1, 1);
    gtk_alignment_set_padding (GTK_ALIGNMENT (dialog->priv->behaviour_tab.cache_alignment), 0, 0, 20, 0);
    dialog->priv->behaviour_tab.cache_spin_button = gtk_spin_button_new(GTK_ADJUSTMENT(cache_adjustment), 1.0, 0);

    dialog->priv->behaviour_tab.cache_preload_label = gtk_label_new (_("Preload images"));
    gtk_misc_set_alignment(GTK_MISC(dialog->priv->behaviour_tab.cache_preload_label), 0, 0.5);
    gtk_misc_set_padding(GTK_MISC(dialog->priv->behaviour_tab.cache_preload_label), 2, 2);
    dialog->priv->behaviour_tab.cache_preload_hscale = gtk_hscale_new_with_range(0, 50, 1);
    gtk_scale_set_value_pos (GTK_SCALE (dialog->priv->behaviour_tab.cache_preload_hscale), GTK_POS_LEFT);

    gtk_box_pack_start (GTK_BOX (dialog->priv->behaviour_tab.cache_hbox), 
                                 dialog->priv->behaviour_tab.cache_size_label, FALSE, FALSE, 0);
    gtk_box_pack_start (GTK_BOX (dialog->priv->behaviour_tab.cache_hbox), 
                                 dialog->priv->behaviour_tab.cache_spin_button, FALSE, FALSE, 0);
    gtk_box_pack_start (GTK_BOX (dialog->priv->behaviour_tab.cache_hbox), 
                                 dialog->priv->behaviour_tab.cache_size_unit, FALSE, FALSE, 0);

    gtk_box_pack_start (GTK_BOX (dialog->priv->behaviour_tab.cache_vbox), 
                                 dialog->priv->behaviour_tab.cache_check_button, FALSE, FALSE, 0);
    gtk_box_pack_start (GTK_BOX (dialog->priv->behaviour_tab.cache_vbox), 
                                 dialog->priv->behaviour_tab.cache_alignment, FALSE, FALSE, 0);
    gtk_container_add (GTK_CONTAINER (dialog->priv->behaviour_tab.cache_alignment),
                                      dialog->priv->behaviour_tab.cache_sub_vbox);

    gtk_box_pack_start (GTK_BOX (dialog->priv->behaviour_tab.cache_sub_vbox), 
                                 dialog->priv->behaviour_tab.cache_hbox, FALSE, FALSE, 0);
    gtk_box_pack_start (GTK_BOX (dialog->priv->behaviour_tab.cache_sub_vbox), 
                                 dialog->priv->behaviour_tab.cache_preload_label, FALSE, FALSE, 0);
    gtk_box_pack_start (GTK_BOX (dialog->priv->behaviour_tab.cache_sub_vbox), 
                                 dialog->priv->behaviour_tab.cache_preload_hscale, FALSE, FALSE, 0);
    
    /* set current value */
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (dialog->priv->behaviour_tab.cache_check_button),
                                  bool_enable_cache);
    gtk_widget_set_sensitive (GTK_WIDGET (dialog->priv->behaviour_tab.cache_sub_vbox),
                              bool_enable_cache);

    gtk_range_set_value ( GTK_RANGE(dialog->priv->behaviour_tab.cache_preload_hscale),
                                  uint_preload_images);

    if (uint_cache_size < RSTTO_MIN_CACHE_SIZE)
    {
        gtk_adjustment_set_value (GTK_ADJUSTMENT (cache_adjustment), RSTTO_DEFAULT_CACHE_SIZE);
    }
    else
    {
        gtk_adjustment_set_value (GTK_ADJUSTMENT (cache_adjustment), uint_cache_size);
    }

    /* connect signals */
    g_signal_connect (G_OBJECT (dialog->priv->behaviour_tab.cache_check_button), 
                      "toggled", (GCallback)cb_rstto_preferences_dialog_cache_check_button_toggled, dialog);
    g_signal_connect (G_OBJECT (dialog->priv->behaviour_tab.cache_preload_hscale), 
                      "value-changed", (GCallback)cb_rstto_preferences_dialog_cache_preload_hscale_value_changed, dialog);
    g_signal_connect (G_OBJECT (dialog->priv->behaviour_tab.cache_spin_button), 
                      "value-changed", (GCallback)cb_rstto_preferences_dialog_cache_spin_button_value_changed, dialog);

    /********************************************/
    scaling_vbox = gtk_vbox_new(FALSE, 0);
    scaling_frame = xfce_create_framebox_with_content (_("Scaling"), scaling_vbox);
    gtk_box_pack_start (GTK_BOX (behaviour_main_vbox), scaling_frame, FALSE, FALSE, 0);
    /* not used */
    gtk_widget_set_sensitive (scaling_vbox, FALSE);

    widget = gtk_check_button_new_with_label (_("Don't scale over 100% when maximizing the window."));
    gtk_container_add (GTK_CONTAINER (scaling_vbox), widget);


/********************************************/
    gtk_container_add (GTK_CONTAINER (GTK_DIALOG (dialog)->vbox), notebook);
    gtk_widget_show_all (notebook);

    gtk_dialog_add_button (GTK_DIALOG (dialog), GTK_STOCK_CLOSE, GTK_RESPONSE_OK);
}

static void
rstto_preferences_dialog_class_init(RsttoPreferencesDialogClass *dialog_class)
{
    GObjectClass *object_class = (GObjectClass*)dialog_class;
    parent_class = g_type_class_peek_parent(dialog_class);
}

GtkWidget *
rstto_preferences_dialog_new (GtkWindow *parent)
{
    GtkWidget *dialog = g_object_new (RSTTO_TYPE_PREFERENCES_DIALOG,
                                      "title", _("Preferences"),
                                      "icon-name", GTK_STOCK_PREFERENCES,
                                      NULL);
    gtk_window_set_transient_for (GTK_WINDOW (dialog), parent);

    return dialog;
}


static void
cb_rstto_preferences_dialog_bgcolor_override_toggled (GtkToggleButton *button, 
                                                      gpointer user_data)
{
    RsttoPreferencesDialog *dialog = RSTTO_PREFERENCES_DIALOG (user_data);
    
    GValue bgcolor_override_val = {0, };
    g_value_init (&bgcolor_override_val, G_TYPE_BOOLEAN);

    if (gtk_toggle_button_get_active (button))
    {
        g_value_set_boolean (&bgcolor_override_val, TRUE);
        gtk_widget_set_sensitive (dialog->priv->display_tab.bgcolor_color_button, TRUE);
    }
    else
    {
        g_value_set_boolean (&bgcolor_override_val, FALSE);
        gtk_widget_set_sensitive (dialog->priv->display_tab.bgcolor_color_button, FALSE);
    }

    g_object_set_property (G_OBJECT (dialog->priv->settings), "bgcolor-override", &bgcolor_override_val);
    
}

static void
cb_rstto_preferences_dialog_bgcolor_color_set (GtkColorButton *button, gpointer user_data)
{
    RsttoPreferencesDialog *dialog = RSTTO_PREFERENCES_DIALOG (user_data);
    GValue bgcolor_val = {0, };
    g_value_init (&bgcolor_val, GDK_TYPE_COLOR);

    g_object_get_property (G_OBJECT(button), "color", &bgcolor_val);
    g_object_set_property (G_OBJECT(dialog->priv->settings), "bgcolor", &bgcolor_val);
    
}

static void
cb_rstto_preferences_dialog_cache_check_button_toggled (GtkToggleButton *button, 
                                                        gpointer user_data)
{
    RsttoPreferencesDialog *dialog = RSTTO_PREFERENCES_DIALOG (user_data);

    GValue value = {0, };
    g_value_init (&value, G_TYPE_BOOLEAN);

    if (gtk_toggle_button_get_active (button))
    {
        g_value_set_boolean (&value, TRUE);
    	gtk_widget_set_sensitive (GTK_WIDGET (dialog->priv->behaviour_tab.cache_sub_vbox), TRUE);
    }
    else
    {
        g_value_set_boolean (&value, FALSE);
    	gtk_widget_set_sensitive (GTK_WIDGET (dialog->priv->behaviour_tab.cache_sub_vbox), FALSE);
    }
    
    g_object_set_property (G_OBJECT (dialog->priv->settings), "enable-cache", &value);

    g_value_unset (&value);

}

static void
cb_rstto_preferences_dialog_cache_preload_hscale_value_changed (GtkRange *range, 
                                                                gpointer user_data)
{
    RsttoPreferencesDialog *dialog = RSTTO_PREFERENCES_DIALOG (user_data);

    GValue value = {0, };
    g_value_init (&value, G_TYPE_UINT);

    g_value_set_uint (&value, gtk_range_get_value (range));

    g_object_set_property (G_OBJECT (dialog->priv->settings), "preload-images", &value);
    g_value_unset (&value);
}

static void
cb_rstto_preferences_dialog_cache_spin_button_value_changed (GtkSpinButton *button, 
                                                                gpointer user_data)
{
    RsttoPreferencesDialog *dialog = RSTTO_PREFERENCES_DIALOG (user_data);

    GValue value = {0, };
    g_value_init (&value, G_TYPE_UINT);

    g_value_set_uint (&value, (guint)gtk_spin_button_get_value (button));

    g_object_set_property (G_OBJECT (dialog->priv->settings), "cache-size", &value);

    g_value_unset (&value);
}

static void
cb_rstto_preferences_dialog_image_quality_combo_box_changed (GtkComboBox *combo_box,
                                                             gpointer user_data)

{
    /* FIXME */
    RsttoPreferencesDialog *dialog = RSTTO_PREFERENCES_DIALOG (user_data);
    switch (gtk_combo_box_get_active (combo_box))
    {
        case 0: /* unlimited */
            g_object_set (G_OBJECT (dialog->priv->settings),
                          "image-quality", 0,
                          NULL);
            break;
        case 1: /* 1 MegaPixel */
            g_object_set (G_OBJECT (dialog->priv->settings),
                          "image-quality", 1000000,
                          NULL);
            break;
        case 2: /* 2 MegaPixel */
            g_object_set (G_OBJECT (dialog->priv->settings),
                          "image-quality", 2000000,
                          NULL);
            break;
        case 3: /* 4 MegaPixel */
            g_object_set (G_OBJECT (dialog->priv->settings),
                          "image-quality", 4000000,
                          NULL);
            break;
        case 4: /* 8 MegaPixel */
            g_object_set (G_OBJECT (dialog->priv->settings),
                          "image-quality", 8000000,
                          NULL);
            break;

    }
}
