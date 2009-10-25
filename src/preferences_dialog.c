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
static void
cb_rstto_preferences_dialog_image_preview_toggled (GtkToggleButton *button, 
                                                      gpointer user_data);
static void
cb_rstto_preferences_dialog_no_scrollwheel_action_radio_button_toggled (GtkToggleButton *, gpointer);
static void
cb_rstto_preferences_dialog_zoom_scrollwheel_action_radio_button_toggled (GtkToggleButton *, gpointer);
static void
cb_rstto_preferences_dialog_switch_scrollwheel_action_radio_button_toggled (GtkToggleButton *, gpointer );

static void
cb_rstto_preferences_dialog_hide_thumbnails_fullscreen_check_button_toggled (
                                                        GtkToggleButton *button, 
                                                        gpointer user_data);

static void
cb_rstto_preferences_dialog_open_entire_folder_check_button_toggled (GtkToggleButton *button, 
                                                      gpointer user_data);
static void
cb_rstto_preferences_dialog_wrap_images_check_button_toggled (GtkToggleButton *button, 
                                                      gpointer user_data);

static void
cb_rstto_preferences_dialog_slideshow_timeout_value_changed (GtkRange *, gpointer);

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
        GtkWidget *image_preview_check_button;
    } display_tab;

    struct
    {
        GtkWidget *timeout_vbox;
        GtkWidget *timeout_frame;
        GtkWidget *thumbnail_vbox;
        GtkWidget *thumbnail_frame;
        GtkWidget *hide_thumbnails_fullscreen_lbl;
        GtkWidget *hide_thumbnails_fullscreen_check_button;
    } slideshow_tab;

    struct
    {
        GtkWidget *scroll_frame;
        GtkWidget *scroll_vbox;
        GtkWidget *no_scrollwheel_action_radio_button;
        GtkWidget *zoom_scrollwheel_action_radio_button;
        GtkWidget *switch_scrollwheel_action_radio_button;

    } control_tab;

    struct
    {
        GtkWidget *scaling_frame;
        GtkWidget *scaling_vbox;
        GtkWidget *resize_image_on_maximize;

        GtkWidget *startup_frame;
        GtkWidget *startup_vbox;
        GtkWidget *resize_window_on_startup_check_button;
        GtkWidget *open_entire_folder_check_button;
        GtkWidget *wrap_images_check_button;
    } behaviour_tab;

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
    } cache_tab;
};

GType
rstto_preferences_dialog_get_type (void)
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
    guint uint_image_quality;
    guint uint_cache_size;
    guint uint_preload_images;
    gboolean bool_enable_cache;
    gboolean bool_bgcolor_override;
    gchar *scrollwheel_primary_action;
    guint uint_slideshow_timeout;
    gboolean bool_hide_thumbnailbar_fullscreen;
    gboolean bool_show_preview;
    gboolean bool_open_entire_folder;
    gboolean bool_wrap_images;

    GdkColor *bgcolor;
    GtkWidget *timeout_lbl, *timeout_hscale;
    GtkWidget *thumbnail_lbl;
    GtkWidget *widget;
    GtkObject *cache_adjustment;
    GtkWidget *display_main_vbox;
    GtkWidget *display_main_lbl;
    GtkWidget *slideshow_main_vbox;
    GtkWidget *slideshow_main_lbl;
    GtkWidget *control_main_vbox;
    GtkWidget *control_main_lbl;
    GtkWidget *behaviour_main_vbox;
    GtkWidget *behaviour_main_lbl;
    GtkWidget *cache_main_vbox;
    GtkWidget *cache_main_lbl;
    GtkWidget *notebook = gtk_notebook_new ();


    dialog->priv = g_new0 (RsttoPreferencesDialogPriv, 1);

    dialog->priv->settings = rstto_settings_new ();
    g_object_get (G_OBJECT (dialog->priv->settings),
                  "image-quality", &uint_image_quality,
                  "cache-size", &uint_cache_size,
                  "show-preview", &bool_show_preview,
                  "preload-images", &uint_preload_images,
                  "enable-cache", &bool_enable_cache,
                  "bgcolor-override", &bool_bgcolor_override,
                  "bgcolor", &bgcolor,
                  "scrollwheel-primary-action", &scrollwheel_primary_action,
                  "slideshow-timeout", &uint_slideshow_timeout,
                  "hide-thumbnailbar-fullscreen", &bool_hide_thumbnailbar_fullscreen,
                  "open-entire-folder", &bool_open_entire_folder,
                  "wrap-images", &bool_wrap_images,
                  NULL);

/*****************/
/** DISPLAY TAB **/
/*****************/
    display_main_vbox = gtk_vbox_new(FALSE, 0);
    display_main_lbl = gtk_label_new(_("Display"));
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
    dialog->priv->display_tab.image_preview_check_button = gtk_check_button_new_with_label (_("Show preview when loading image"));

    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (dialog->priv->display_tab.image_preview_check_button),
                                  bool_show_preview);
    g_signal_connect (G_OBJECT (dialog->priv->display_tab.image_preview_check_button), 
                      "toggled", (GCallback)cb_rstto_preferences_dialog_image_preview_toggled, dialog);

    gtk_combo_box_append_text (GTK_COMBO_BOX (dialog->priv->display_tab.image_quality_combo), _("Best"));
    gtk_combo_box_append_text (GTK_COMBO_BOX (dialog->priv->display_tab.image_quality_combo), _("High"));
    gtk_combo_box_append_text (GTK_COMBO_BOX (dialog->priv->display_tab.image_quality_combo), _("Medium"));
    gtk_combo_box_append_text (GTK_COMBO_BOX (dialog->priv->display_tab.image_quality_combo), _("Low"));

    gtk_box_pack_start (GTK_BOX (dialog->priv->display_tab.image_quality_vbox), 
                                 dialog->priv->display_tab.image_quality_hbox, FALSE, FALSE, 0);
    gtk_box_pack_start (GTK_BOX (dialog->priv->display_tab.image_quality_vbox), 
                                 dialog->priv->display_tab.image_preview_check_button, FALSE, FALSE, 0);

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
    slideshow_main_vbox = gtk_vbox_new(FALSE, 0);
    slideshow_main_lbl = gtk_label_new(_("Slideshow"));
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), slideshow_main_vbox, slideshow_main_lbl);

    dialog->priv->slideshow_tab.timeout_vbox = gtk_vbox_new(FALSE, 0);
    dialog->priv->slideshow_tab.timeout_frame = xfce_create_framebox_with_content (_("Timeout"), dialog->priv->slideshow_tab.timeout_vbox);
    gtk_box_pack_start (GTK_BOX (slideshow_main_vbox), dialog->priv->slideshow_tab.timeout_frame, FALSE, FALSE, 0);

    timeout_lbl = gtk_label_new(_("The time period an individual image is displayed during a slideshow\n(in seconds)"));
    timeout_hscale = gtk_hscale_new_with_range(1, 60, 1);
    gtk_misc_set_alignment(GTK_MISC(timeout_lbl), 0, 0.5);
    gtk_misc_set_padding(GTK_MISC(timeout_lbl), 2, 2);

    gtk_box_pack_start(GTK_BOX(dialog->priv->slideshow_tab.timeout_vbox), timeout_lbl, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(dialog->priv->slideshow_tab.timeout_vbox), timeout_hscale, FALSE, TRUE, 0);
    

    gtk_range_set_value (GTK_RANGE (timeout_hscale), (gdouble)uint_slideshow_timeout);
    g_signal_connect (G_OBJECT (timeout_hscale),
                      "value-changed", (GCallback)cb_rstto_preferences_dialog_slideshow_timeout_value_changed, dialog);

    
    dialog->priv->slideshow_tab.thumbnail_vbox = gtk_vbox_new(FALSE, 0);
    dialog->priv->slideshow_tab.thumbnail_frame = xfce_create_framebox_with_content (_("Thumbnails"), dialog->priv->slideshow_tab.thumbnail_vbox);
    gtk_box_pack_start (GTK_BOX (slideshow_main_vbox), dialog->priv->slideshow_tab.thumbnail_frame, FALSE, FALSE, 0);

    dialog->priv->slideshow_tab.hide_thumbnails_fullscreen_lbl = gtk_label_new(_("The thumbnailbar can be automatically hidden \nwhen the image-viewer is fullscreen."));
    gtk_misc_set_alignment(GTK_MISC(dialog->priv->slideshow_tab.hide_thumbnails_fullscreen_lbl), 0, 0.5);
    dialog->priv->slideshow_tab.hide_thumbnails_fullscreen_check_button = gtk_check_button_new_with_label (_("Hide thumbnailbar when fullscreen"));
    gtk_box_pack_start (GTK_BOX (dialog->priv->slideshow_tab.thumbnail_vbox), dialog->priv->slideshow_tab.hide_thumbnails_fullscreen_lbl, FALSE, FALSE, 0);
    gtk_box_pack_start (GTK_BOX (dialog->priv->slideshow_tab.thumbnail_vbox), dialog->priv->slideshow_tab.hide_thumbnails_fullscreen_check_button, FALSE, FALSE, 0);

    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (dialog->priv->slideshow_tab.hide_thumbnails_fullscreen_check_button),
                                  bool_hide_thumbnailbar_fullscreen);

    g_signal_connect (G_OBJECT (dialog->priv->slideshow_tab.hide_thumbnails_fullscreen_check_button), 
                      "toggled", (GCallback)cb_rstto_preferences_dialog_hide_thumbnails_fullscreen_check_button_toggled, dialog);


/********************************************/
    control_main_vbox = gtk_vbox_new(FALSE, 0);
    control_main_lbl = gtk_label_new(_("Control"));
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), control_main_vbox, control_main_lbl);

    dialog->priv->control_tab.scroll_vbox = gtk_vbox_new(FALSE, 0);
    dialog->priv->control_tab.scroll_frame = xfce_create_framebox_with_content (_("Scrollwheel"), dialog->priv->control_tab.scroll_vbox);
    gtk_box_pack_start (GTK_BOX (control_main_vbox), dialog->priv->control_tab.scroll_frame, FALSE, FALSE, 0);

    dialog->priv->control_tab.no_scrollwheel_action_radio_button = gtk_radio_button_new_with_label (NULL, _("No action"));
    gtk_container_add (GTK_CONTAINER (dialog->priv->control_tab.scroll_vbox), dialog->priv->control_tab.no_scrollwheel_action_radio_button);


    dialog->priv->control_tab.zoom_scrollwheel_action_radio_button = gtk_radio_button_new_with_label_from_widget (GTK_RADIO_BUTTON (dialog->priv->control_tab.no_scrollwheel_action_radio_button), _("Zoom in and out"));
    gtk_container_add (GTK_CONTAINER (dialog->priv->control_tab.scroll_vbox), dialog->priv->control_tab.zoom_scrollwheel_action_radio_button);

    dialog->priv->control_tab.switch_scrollwheel_action_radio_button = gtk_radio_button_new_with_label_from_widget (GTK_RADIO_BUTTON (dialog->priv->control_tab.no_scrollwheel_action_radio_button), _("Switch images"));
    gtk_container_add (GTK_CONTAINER (dialog->priv->control_tab.scroll_vbox), dialog->priv->control_tab.switch_scrollwheel_action_radio_button);

    if (scrollwheel_primary_action)
    {
        if (!strcmp (scrollwheel_primary_action, "zoom"))
        {
            gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (dialog->priv->control_tab.zoom_scrollwheel_action_radio_button), TRUE);
        } 
        else if (!strcmp (scrollwheel_primary_action, "switch"))
        {
            gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (dialog->priv->control_tab.switch_scrollwheel_action_radio_button), TRUE);
        }
        else
        {
            gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (dialog->priv->control_tab.no_scrollwheel_action_radio_button), TRUE);
        }
    }
    else
    {
        gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (dialog->priv->control_tab.no_scrollwheel_action_radio_button), TRUE);
    }
    

    g_signal_connect (G_OBJECT (dialog->priv->control_tab.no_scrollwheel_action_radio_button), 
                      "toggled", (GCallback)cb_rstto_preferences_dialog_no_scrollwheel_action_radio_button_toggled, dialog);
    g_signal_connect (G_OBJECT (dialog->priv->control_tab.zoom_scrollwheel_action_radio_button), 
                      "toggled", (GCallback)cb_rstto_preferences_dialog_zoom_scrollwheel_action_radio_button_toggled, dialog);
    g_signal_connect (G_OBJECT (dialog->priv->control_tab.switch_scrollwheel_action_radio_button), 
                      "toggled", (GCallback)cb_rstto_preferences_dialog_switch_scrollwheel_action_radio_button_toggled, dialog);
/*******************/
/** Behaviour tab **/
/*******************/
    behaviour_main_vbox = gtk_vbox_new(FALSE, 0);
    behaviour_main_lbl = gtk_label_new(_("Behaviour"));
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), behaviour_main_vbox, behaviour_main_lbl);

    /********************************************/
    dialog->priv->behaviour_tab.scaling_vbox = gtk_vbox_new(FALSE, 0);
    dialog->priv->behaviour_tab.scaling_frame = xfce_create_framebox_with_content (_("Scaling"), dialog->priv->behaviour_tab.scaling_vbox);
    gtk_box_pack_start (GTK_BOX (behaviour_main_vbox), dialog->priv->behaviour_tab.scaling_frame, FALSE, FALSE, 0);
    /* not used */
    gtk_widget_set_sensitive (dialog->priv->behaviour_tab.scaling_vbox, FALSE);

    dialog->priv->behaviour_tab.resize_image_on_maximize = gtk_check_button_new_with_label (_("Don't scale over 100% when maximizing the window."));
    gtk_container_add (GTK_CONTAINER (dialog->priv->behaviour_tab.scaling_vbox), dialog->priv->behaviour_tab.resize_image_on_maximize);

    dialog->priv->behaviour_tab.startup_vbox = gtk_vbox_new(FALSE, 0);
    dialog->priv->behaviour_tab.startup_frame = xfce_create_framebox_with_content (_("Startup"), dialog->priv->behaviour_tab.startup_vbox);
    gtk_box_pack_start (GTK_BOX (behaviour_main_vbox), dialog->priv->behaviour_tab.startup_frame, FALSE, FALSE, 0);
    dialog->priv->behaviour_tab.resize_window_on_startup_check_button = gtk_check_button_new_with_label (_("Resize window to image on startup"));
    gtk_container_add (GTK_CONTAINER (dialog->priv->behaviour_tab.startup_vbox), dialog->priv->behaviour_tab.resize_window_on_startup_check_button);
    gtk_widget_set_sensitive (dialog->priv->behaviour_tab.resize_window_on_startup_check_button, FALSE);

    dialog->priv->behaviour_tab.open_entire_folder_check_button = gtk_check_button_new_with_label (_("Open entire folder on startup"));
    gtk_container_add (GTK_CONTAINER (dialog->priv->behaviour_tab.startup_vbox), dialog->priv->behaviour_tab.open_entire_folder_check_button);
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (dialog->priv->behaviour_tab.open_entire_folder_check_button),
                                  bool_open_entire_folder);

    dialog->priv->behaviour_tab.wrap_images_check_button = gtk_check_button_new_with_label (_("Wrap around images"));
    gtk_container_add (GTK_CONTAINER (dialog->priv->behaviour_tab.startup_vbox), dialog->priv->behaviour_tab.wrap_images_check_button);
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (dialog->priv->behaviour_tab.wrap_images_check_button),
                                  bool_wrap_images);

    g_signal_connect (G_OBJECT (dialog->priv->behaviour_tab.open_entire_folder_check_button), 
                      "toggled", (GCallback)cb_rstto_preferences_dialog_open_entire_folder_check_button_toggled, dialog);
    g_signal_connect (G_OBJECT (dialog->priv->behaviour_tab.wrap_images_check_button), 
                      "toggled", (GCallback)cb_rstto_preferences_dialog_wrap_images_check_button_toggled, dialog);



/***************/
/** Cache tab **/
/***************/
    cache_main_vbox = gtk_vbox_new(FALSE, 0);
    cache_main_lbl = gtk_label_new(_("Memory"));
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), cache_main_vbox, cache_main_lbl);


/** Image-cache frame */
    dialog->priv->cache_tab.cache_vbox = gtk_vbox_new(FALSE, 0);
    dialog->priv->cache_tab.cache_frame = xfce_create_framebox_with_content (_("Image cache"),
                                                                                 dialog->priv->cache_tab.cache_vbox);
    gtk_box_pack_start (GTK_BOX (cache_main_vbox), dialog->priv->cache_tab.cache_frame, FALSE, FALSE, 0);

    cache_adjustment = gtk_adjustment_new (RSTTO_DEFAULT_CACHE_SIZE, RSTTO_MIN_CACHE_SIZE, 4096, 1, 0, 0);

    dialog->priv->cache_tab.cache_size_label = gtk_label_new (_("Cache size"));
    dialog->priv->cache_tab.cache_size_unit = gtk_label_new (_("MB"));
    dialog->priv->cache_tab.cache_hbox = gtk_hbox_new (FALSE, 4);
    dialog->priv->cache_tab.cache_sub_vbox = gtk_vbox_new (FALSE, 4);
    dialog->priv->cache_tab.cache_check_button = gtk_check_button_new_with_label (_("Enable cache"));
    dialog->priv->cache_tab.cache_alignment = gtk_alignment_new (0, 0, 1, 1);
    gtk_alignment_set_padding (GTK_ALIGNMENT (dialog->priv->cache_tab.cache_alignment), 0, 0, 20, 0);
    dialog->priv->cache_tab.cache_spin_button = gtk_spin_button_new(GTK_ADJUSTMENT(cache_adjustment), 1.0, 0);

    dialog->priv->cache_tab.cache_preload_label = gtk_label_new (_("Preload images"));
    gtk_misc_set_alignment(GTK_MISC(dialog->priv->cache_tab.cache_preload_label), 0, 0.5);
    gtk_misc_set_padding(GTK_MISC(dialog->priv->cache_tab.cache_preload_label), 2, 2);
    dialog->priv->cache_tab.cache_preload_hscale = gtk_hscale_new_with_range(0, 50, 1);
    gtk_scale_set_value_pos (GTK_SCALE (dialog->priv->cache_tab.cache_preload_hscale), GTK_POS_LEFT);

    gtk_box_pack_start (GTK_BOX (dialog->priv->cache_tab.cache_hbox), 
                                 dialog->priv->cache_tab.cache_size_label, FALSE, FALSE, 0);
    gtk_box_pack_start (GTK_BOX (dialog->priv->cache_tab.cache_hbox), 
                                 dialog->priv->cache_tab.cache_spin_button, FALSE, FALSE, 0);
    gtk_box_pack_start (GTK_BOX (dialog->priv->cache_tab.cache_hbox), 
                                 dialog->priv->cache_tab.cache_size_unit, FALSE, FALSE, 0);

    gtk_box_pack_start (GTK_BOX (dialog->priv->cache_tab.cache_vbox), 
                                 dialog->priv->cache_tab.cache_check_button, FALSE, FALSE, 0);
    gtk_box_pack_start (GTK_BOX (dialog->priv->cache_tab.cache_vbox), 
                                 dialog->priv->cache_tab.cache_alignment, FALSE, FALSE, 0);
    gtk_container_add (GTK_CONTAINER (dialog->priv->cache_tab.cache_alignment),
                                      dialog->priv->cache_tab.cache_sub_vbox);

    gtk_box_pack_start (GTK_BOX (dialog->priv->cache_tab.cache_sub_vbox), 
                                 dialog->priv->cache_tab.cache_hbox, FALSE, FALSE, 0);
    gtk_box_pack_start (GTK_BOX (dialog->priv->cache_tab.cache_sub_vbox), 
                                 dialog->priv->cache_tab.cache_preload_label, FALSE, FALSE, 0);
    gtk_box_pack_start (GTK_BOX (dialog->priv->cache_tab.cache_sub_vbox), 
                                 dialog->priv->cache_tab.cache_preload_hscale, FALSE, FALSE, 0);
    
    /* set current value */
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (dialog->priv->cache_tab.cache_check_button),
                                  bool_enable_cache);
    gtk_widget_set_sensitive (GTK_WIDGET (dialog->priv->cache_tab.cache_sub_vbox),
                              bool_enable_cache);

    gtk_range_set_value ( GTK_RANGE(dialog->priv->cache_tab.cache_preload_hscale),
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
    g_signal_connect (G_OBJECT (dialog->priv->cache_tab.cache_check_button), 
                      "toggled", (GCallback)cb_rstto_preferences_dialog_cache_check_button_toggled, dialog);
    g_signal_connect (G_OBJECT (dialog->priv->cache_tab.cache_preload_hscale), 
                      "value-changed", (GCallback)cb_rstto_preferences_dialog_cache_preload_hscale_value_changed, dialog);
    g_signal_connect (G_OBJECT (dialog->priv->cache_tab.cache_spin_button), 
                      "value-changed", (GCallback)cb_rstto_preferences_dialog_cache_spin_button_value_changed, dialog);

/********************************************/
    gtk_container_add (GTK_CONTAINER (GTK_DIALOG (dialog)->vbox), notebook);
    gtk_widget_show_all (notebook);

    gtk_dialog_add_button (GTK_DIALOG (dialog), GTK_STOCK_CLOSE, GTK_RESPONSE_OK);
}

static void
rstto_preferences_dialog_class_init(RsttoPreferencesDialogClass *dialog_class)
{
    parent_class = g_type_class_peek_parent (dialog_class);
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
    	gtk_widget_set_sensitive (GTK_WIDGET (dialog->priv->cache_tab.cache_sub_vbox), TRUE);
    }
    else
    {
        g_value_set_boolean (&value, FALSE);
    	gtk_widget_set_sensitive (GTK_WIDGET (dialog->priv->cache_tab.cache_sub_vbox), FALSE);
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
                          "image-quality", 8000000,
                          NULL);
            break;
        case 2: /* 2 MegaPixel */
            g_object_set (G_OBJECT (dialog->priv->settings),
                          "image-quality", 4000000,
                          NULL);
            break;
        case 3: /* 4 MegaPixel */
            g_object_set (G_OBJECT (dialog->priv->settings),
                          "image-quality", 2000000,
                          NULL);
            break;
    }
}

static void
cb_rstto_preferences_dialog_image_preview_toggled (GtkToggleButton *button, 
                                                      gpointer user_data)
{
    RsttoPreferencesDialog *dialog = RSTTO_PREFERENCES_DIALOG (user_data);

    rstto_settings_set_boolean_property (dialog->priv->settings, "show-preview", gtk_toggle_button_get_active (button));
}

static void
cb_rstto_preferences_dialog_no_scrollwheel_action_radio_button_toggled (GtkToggleButton *button, 
                                                                        gpointer user_data)
{
    RsttoPreferencesDialog *dialog = RSTTO_PREFERENCES_DIALOG (user_data);

    if (gtk_toggle_button_get_active (button))
    {
        rstto_settings_set_string_property (dialog->priv->settings, "scrollwheel-primary-action", "none");
    }
}

static void
cb_rstto_preferences_dialog_zoom_scrollwheel_action_radio_button_toggled (GtkToggleButton *button, 
                                                                        gpointer user_data)
{
    RsttoPreferencesDialog *dialog = RSTTO_PREFERENCES_DIALOG (user_data);

    if (gtk_toggle_button_get_active (button))
    {
        rstto_settings_set_string_property (dialog->priv->settings, "scrollwheel-primary-action", "zoom");
    }
}

static void
cb_rstto_preferences_dialog_switch_scrollwheel_action_radio_button_toggled (GtkToggleButton *button, 
                                                                        gpointer user_data)
{
    RsttoPreferencesDialog *dialog = RSTTO_PREFERENCES_DIALOG (user_data);

    if (gtk_toggle_button_get_active (button))
    {
        rstto_settings_set_string_property (dialog->priv->settings, "scrollwheel-primary-action", "switch");
    }
}

static void
cb_rstto_preferences_dialog_slideshow_timeout_value_changed (GtkRange *range, gpointer user_data)
{
    RsttoPreferencesDialog *dialog = RSTTO_PREFERENCES_DIALOG (user_data);

    rstto_settings_set_uint_property (dialog->priv->settings, "slideshow-timeout", (guint)gtk_range_get_value (range));

}

static void
cb_rstto_preferences_dialog_hide_thumbnails_fullscreen_check_button_toggled (
                                                        GtkToggleButton *button, 
                                                        gpointer user_data)
{
    RsttoPreferencesDialog *dialog = RSTTO_PREFERENCES_DIALOG (user_data);

    rstto_settings_set_boolean_property (dialog->priv->settings, "hide-thumbnailbar-fullscreen", gtk_toggle_button_get_active(button));
}

static void
cb_rstto_preferences_dialog_open_entire_folder_check_button_toggled (GtkToggleButton *button, 
                                                      gpointer user_data)
{
    RsttoPreferencesDialog *dialog = RSTTO_PREFERENCES_DIALOG (user_data);

    rstto_settings_set_boolean_property (dialog->priv->settings, "open-entire-folder", gtk_toggle_button_get_active(button));
}

static void
cb_rstto_preferences_dialog_wrap_images_check_button_toggled (GtkToggleButton *button, 
                                                      gpointer user_data)
{
    RsttoPreferencesDialog *dialog = RSTTO_PREFERENCES_DIALOG (user_data);

    rstto_settings_set_boolean_property (dialog->priv->settings, "wrap-images", gtk_toggle_button_get_active(button));
}
