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
#include <gtk/gtk.h>
#include <libxfce4ui/libxfce4ui.h>
#include <libxfce4util/libxfce4util.h>

#include "util.h"
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
rstto_preferences_dialog_class_init(GObjectClass *);

static void
rstto_preferences_dialog_dispose (GObject *object);


static void
cb_rstto_preferences_dialog_bgcolor_override_toggled (GtkToggleButton *, gpointer);
static void
cb_rstto_preferences_dialog_bgcolor_color_set (GtkColorButton *, gpointer);
static void
cb_rstto_preferences_dialog_merge_toolbars_toggled (GtkToggleButton *, gpointer);

static void
cb_rstto_preferences_dialog_zoom_revert_check_button_toggled (GtkToggleButton *, gpointer);

static void
cb_rstto_preferences_dialog_hide_thumbnails_fullscreen_check_button_toggled (
                                                        GtkToggleButton *button, 
                                                        gpointer user_data);
static void
cb_wrap_images_check_button_toggled (
        GtkToggleButton *button, 
        gpointer user_data);
static void
cb_maximize_on_startup_check_button_toggled (
        GtkToggleButton *button, 
        gpointer user_data);
static void
cb_choose_desktop_combo_box_changed (
        GtkComboBox *combo_box,
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
        GtkWidget *toolbars_frame;
        GtkWidget *toolbars_vbox;
        GtkWidget *merge_toolbars_check_button;
        GtkWidget *thumbnail_vbox;
        GtkWidget *thumbnail_frame;
        GtkWidget *hide_thumbnails_fullscreen_lbl;
        GtkWidget *hide_thumbnails_fullscreen_check_button;
    } display_tab;

    struct
    {
        GtkWidget *timeout_vbox;
        GtkWidget *timeout_frame;
    } slideshow_tab;

    struct
    {
        GtkWidget *scroll_frame;
        GtkWidget *scroll_vbox;
        GtkWidget *zoom_revert_check_button;
    } control_tab;

    struct
    {
        GtkWidget *desktop_frame;
        GtkWidget *desktop_vbox;
        GtkWidget *choose_desktop_combo_box;

        GtkWidget *startup_frame;
        GtkWidget *startup_vbox;
        GtkWidget *maximize_window_on_startup_check_button;
        GtkWidget *open_entire_folder_check_button;
        GtkWidget *wrap_images_check_button;
    } behaviour_tab;

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
    gboolean bool_revert_zoom_direction;
    gboolean bool_bgcolor_override;
    guint uint_slideshow_timeout;
    gboolean bool_hide_thumbnailbar_fullscreen;
    gboolean bool_wrap_images;
    gboolean bool_maximize_on_startup;
    gboolean bool_merge_toolbars;
    gchar   *str_desktop_type = NULL;

    GdkColor *bgcolor;
    GtkWidget *timeout_lbl, *timeout_hscale;
    GtkWidget *display_main_vbox;
    GtkWidget *display_main_lbl;
    GtkWidget *slideshow_main_vbox;
    GtkWidget *slideshow_main_lbl;
    GtkWidget *control_main_vbox;
    GtkWidget *control_main_lbl;
    GtkWidget *behaviour_main_vbox;
    GtkWidget *behaviour_main_lbl;
    GtkWidget *behaviour_desktop_lbl;
    GtkWidget *notebook = gtk_notebook_new ();


    dialog->priv = g_new0 (RsttoPreferencesDialogPriv, 1);

    dialog->priv->settings = rstto_settings_new ();
    g_object_get (G_OBJECT (dialog->priv->settings),
                  "revert-zoom-direction", &bool_revert_zoom_direction,
                  "bgcolor-override", &bool_bgcolor_override,
                  "bgcolor", &bgcolor,
                  "slideshow-timeout", &uint_slideshow_timeout,
                  "hide-thumbnailbar-fullscreen", &bool_hide_thumbnailbar_fullscreen,
                  "maximize-on-startup", &bool_maximize_on_startup,
                  "wrap-images", &bool_wrap_images,
                  "merge-toolbars", &bool_merge_toolbars,
                  "desktop-type", &str_desktop_type,
                  NULL);

/*****************/
/** DISPLAY TAB **/
/*****************/
    display_main_vbox = gtk_vbox_new(FALSE, 0);
    display_main_lbl = gtk_label_new(_("Display"));
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), display_main_vbox, display_main_lbl);

/** Bg-color frame */
    dialog->priv->display_tab.bgcolor_vbox = gtk_vbox_new (FALSE, 0);
    dialog->priv->display_tab.toolbars_vbox = gtk_vbox_new (FALSE, 0);
    dialog->priv->display_tab.bgcolor_frame = xfce_gtk_frame_box_new_with_content(_("Background color"),
                                                                                 dialog->priv->display_tab.bgcolor_vbox);
    dialog->priv->display_tab.toolbars_frame = xfce_gtk_frame_box_new_with_content(_("Toolbars"),
                                                                                 dialog->priv->display_tab.toolbars_vbox);
    gtk_box_pack_start (GTK_BOX (display_main_vbox), dialog->priv->display_tab.bgcolor_frame, FALSE, FALSE, 0);
    gtk_box_pack_start (GTK_BOX (display_main_vbox), dialog->priv->display_tab.toolbars_frame, FALSE, FALSE, 0);

    dialog->priv->display_tab.bgcolor_override_check_button = gtk_check_button_new_with_label (_("Override background color:"));
    dialog->priv->display_tab.bgcolor_hbox = gtk_hbox_new (FALSE, 4);
    dialog->priv->display_tab.bgcolor_color_button = gtk_color_button_new();

    gtk_box_pack_start (GTK_BOX (dialog->priv->display_tab.bgcolor_hbox), 
                        dialog->priv->display_tab.bgcolor_override_check_button, FALSE, FALSE, 0);
    gtk_box_pack_start (GTK_BOX (dialog->priv->display_tab.bgcolor_hbox),
                        dialog->priv->display_tab.bgcolor_color_button, FALSE, FALSE, 0);
    gtk_box_pack_start (GTK_BOX (dialog->priv->display_tab.bgcolor_vbox), 
                        dialog->priv->display_tab.bgcolor_hbox, FALSE, FALSE, 0);

    dialog->priv->display_tab.merge_toolbars_check_button = gtk_check_button_new_with_label (_("Merge toolbars"));
    gtk_box_pack_start (GTK_BOX (dialog->priv->display_tab.toolbars_vbox), 
                        dialog->priv->display_tab.merge_toolbars_check_button, FALSE, FALSE, 0);

    dialog->priv->display_tab.thumbnail_vbox = gtk_vbox_new(FALSE, 0);
    dialog->priv->display_tab.thumbnail_frame = xfce_gtk_frame_box_new_with_content(_("Thumbnails"), dialog->priv->display_tab.thumbnail_vbox);
    gtk_box_pack_start (GTK_BOX (display_main_vbox), dialog->priv->display_tab.thumbnail_frame, FALSE, FALSE, 0);

    dialog->priv->display_tab.hide_thumbnails_fullscreen_lbl = gtk_label_new(_("The thumbnailbar can be automatically hidden \nwhen the image-viewer is fullscreen."));
    gtk_misc_set_alignment(GTK_MISC(dialog->priv->display_tab.hide_thumbnails_fullscreen_lbl), 0, 0.5);
    dialog->priv->display_tab.hide_thumbnails_fullscreen_check_button = gtk_check_button_new_with_label (_("Hide thumbnailbar when fullscreen"));
    gtk_box_pack_start (GTK_BOX (dialog->priv->display_tab.thumbnail_vbox), dialog->priv->display_tab.hide_thumbnails_fullscreen_lbl, FALSE, FALSE, 0);
    gtk_box_pack_start (GTK_BOX (dialog->priv->display_tab.thumbnail_vbox), dialog->priv->display_tab.hide_thumbnails_fullscreen_check_button, FALSE, FALSE, 0);

    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (dialog->priv->display_tab.hide_thumbnails_fullscreen_check_button),
                                  bool_hide_thumbnailbar_fullscreen);

    g_signal_connect (G_OBJECT (dialog->priv->display_tab.hide_thumbnails_fullscreen_check_button), 
                      "toggled", (GCallback)cb_rstto_preferences_dialog_hide_thumbnails_fullscreen_check_button_toggled, dialog);



    /* set current value */
    gtk_color_button_set_color (GTK_COLOR_BUTTON (dialog->priv->display_tab.bgcolor_color_button),
                                bgcolor);

    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (dialog->priv->display_tab.bgcolor_override_check_button),
                                  bool_bgcolor_override);
    gtk_widget_set_sensitive (GTK_WIDGET (dialog->priv->display_tab.bgcolor_color_button),
                              bool_bgcolor_override);

    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (dialog->priv->display_tab.merge_toolbars_check_button),
                                  bool_merge_toolbars);
    
    /* connect signals */
    g_signal_connect (G_OBJECT (dialog->priv->display_tab.bgcolor_override_check_button), 
                      "toggled", (GCallback)cb_rstto_preferences_dialog_bgcolor_override_toggled, dialog);
    g_signal_connect (G_OBJECT (dialog->priv->display_tab.bgcolor_color_button), 
                      "color-set", G_CALLBACK (cb_rstto_preferences_dialog_bgcolor_color_set), dialog);
    g_signal_connect (G_OBJECT (dialog->priv->display_tab.merge_toolbars_check_button), 
                      "toggled", (GCallback)cb_rstto_preferences_dialog_merge_toolbars_toggled, dialog);

/*******************/
/** Slideshow tab **/
/*******************/
    slideshow_main_vbox = gtk_vbox_new(FALSE, 0);
    slideshow_main_lbl = gtk_label_new(_("Slideshow"));
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), slideshow_main_vbox, slideshow_main_lbl);

    dialog->priv->slideshow_tab.timeout_vbox = gtk_vbox_new(FALSE, 0);
    dialog->priv->slideshow_tab.timeout_frame = xfce_gtk_frame_box_new_with_content(_("Timeout"), dialog->priv->slideshow_tab.timeout_vbox);
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

    
/********************************************/
    control_main_vbox = gtk_vbox_new(FALSE, 0);
    control_main_lbl = gtk_label_new(_("Control"));
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), control_main_vbox, control_main_lbl);

    dialog->priv->control_tab.scroll_vbox = gtk_vbox_new(FALSE, 0);
    dialog->priv->control_tab.scroll_frame = xfce_gtk_frame_box_new_with_content(_("Scrollwheel"), dialog->priv->control_tab.scroll_vbox);
    gtk_box_pack_start (GTK_BOX (control_main_vbox), dialog->priv->control_tab.scroll_frame, FALSE, FALSE, 0);

    dialog->priv->control_tab.zoom_revert_check_button = gtk_check_button_new_with_label (_("Revert zoom direction"));
    gtk_container_add (GTK_CONTAINER (dialog->priv->control_tab.scroll_vbox), dialog->priv->control_tab.zoom_revert_check_button);
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (dialog->priv->control_tab.zoom_revert_check_button), bool_revert_zoom_direction);

    g_signal_connect (G_OBJECT (dialog->priv->control_tab.zoom_revert_check_button), "toggled", (GCallback)cb_rstto_preferences_dialog_zoom_revert_check_button_toggled, dialog);

/*******************/
/** Behaviour tab **/
/*******************/
    behaviour_main_vbox = gtk_vbox_new(FALSE, 0);
    behaviour_main_lbl = gtk_label_new(_("Behaviour"));
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), behaviour_main_vbox, behaviour_main_lbl);

    /********************************************/
    dialog->priv->behaviour_tab.startup_vbox = gtk_vbox_new(FALSE, 0);
    dialog->priv->behaviour_tab.startup_frame = xfce_gtk_frame_box_new_with_content(_("Startup"), dialog->priv->behaviour_tab.startup_vbox);
    gtk_box_pack_start (GTK_BOX (behaviour_main_vbox), dialog->priv->behaviour_tab.startup_frame, FALSE, FALSE, 0);
    dialog->priv->behaviour_tab.maximize_window_on_startup_check_button = gtk_check_button_new_with_label (_("Maximize window on startup when opening an image"));
    gtk_container_add (GTK_CONTAINER (dialog->priv->behaviour_tab.startup_vbox), dialog->priv->behaviour_tab.maximize_window_on_startup_check_button);
    gtk_toggle_button_set_active (
            GTK_TOGGLE_BUTTON (dialog->priv->behaviour_tab.maximize_window_on_startup_check_button),
            bool_maximize_on_startup);

    dialog->priv->behaviour_tab.wrap_images_check_button = gtk_check_button_new_with_label (_("Wrap around images"));
    gtk_container_add (GTK_CONTAINER (dialog->priv->behaviour_tab.startup_vbox), dialog->priv->behaviour_tab.wrap_images_check_button);
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (dialog->priv->behaviour_tab.wrap_images_check_button),
                                  bool_wrap_images);

    g_signal_connect (
            G_OBJECT (dialog->priv->behaviour_tab.wrap_images_check_button), 
            "toggled",
            (GCallback)cb_wrap_images_check_button_toggled,
            dialog);
    g_signal_connect (
            G_OBJECT
(dialog->priv->behaviour_tab.maximize_window_on_startup_check_button), 
            "toggled",
            (GCallback)cb_maximize_on_startup_check_button_toggled,
            dialog);
    /********************************************/
    dialog->priv->behaviour_tab.desktop_vbox = gtk_vbox_new(FALSE, 4);
    dialog->priv->behaviour_tab.desktop_frame = xfce_gtk_frame_box_new_with_content(
            _("Desktop"),
            dialog->priv->behaviour_tab.desktop_vbox);
    gtk_box_pack_start (
            GTK_BOX (behaviour_main_vbox),
            dialog->priv->behaviour_tab.desktop_frame,
            FALSE,
            FALSE,
            0);
    behaviour_desktop_lbl = gtk_label_new(NULL);
    gtk_label_set_markup (
            GTK_LABEL (behaviour_desktop_lbl),
            _("Configure which system is currently managing your desktop.\n"
              "This setting determines the method <i>Ristretto</i> will use\n"
              "to configure the desktop wallpaper."));
    gtk_misc_set_alignment(
            GTK_MISC(behaviour_desktop_lbl),
            0,
            0.5);
    gtk_box_pack_start (
            GTK_BOX (dialog->priv->behaviour_tab.desktop_vbox),
            behaviour_desktop_lbl,
            FALSE,
            FALSE,
            0);
    dialog->priv->behaviour_tab.choose_desktop_combo_box =
            gtk_combo_box_text_new();
    gtk_box_pack_start (
            GTK_BOX (dialog->priv->behaviour_tab.desktop_vbox),
            dialog->priv->behaviour_tab.choose_desktop_combo_box,
            FALSE,
            FALSE,
            0);
    gtk_combo_box_text_insert_text(
            GTK_COMBO_BOX_TEXT (dialog->priv->behaviour_tab.choose_desktop_combo_box),
            DESKTOP_TYPE_NONE,
            _("None"));
    gtk_combo_box_text_insert_text (
            GTK_COMBO_BOX_TEXT (dialog->priv->behaviour_tab.choose_desktop_combo_box),
            DESKTOP_TYPE_XFCE,
            _("Xfce"));
    gtk_combo_box_text_insert_text (
            GTK_COMBO_BOX_TEXT (dialog->priv->behaviour_tab.choose_desktop_combo_box),
            DESKTOP_TYPE_GNOME,
            _("GNOME"));

    if (str_desktop_type != NULL)
    {
        if (0 == g_strcasecmp (str_desktop_type, "xfce"))
        {
            gtk_combo_box_set_active (
                GTK_COMBO_BOX (dialog->priv->behaviour_tab.choose_desktop_combo_box),
                DESKTOP_TYPE_XFCE);
        }
        else
        {
            if (0 == g_strcasecmp (str_desktop_type, "gnome"))
            {
                gtk_combo_box_set_active (
                    GTK_COMBO_BOX (dialog->priv->behaviour_tab.choose_desktop_combo_box),
                    DESKTOP_TYPE_GNOME);
            }
            else
            {
                gtk_combo_box_set_active (
                    GTK_COMBO_BOX (dialog->priv->behaviour_tab.choose_desktop_combo_box),
                    DESKTOP_TYPE_NONE);
            }
        }
    }
    else
    {
        /* Default, set it to xfce */
        gtk_combo_box_set_active (
            GTK_COMBO_BOX (dialog->priv->behaviour_tab.choose_desktop_combo_box),
            DESKTOP_TYPE_XFCE);
    }

    g_signal_connect (
            G_OBJECT(dialog->priv->behaviour_tab.choose_desktop_combo_box),
            "changed",
            (GCallback)cb_choose_desktop_combo_box_changed,
            dialog);
    



/********************************************/
    gtk_container_add (GTK_CONTAINER (GTK_DIALOG (dialog)->vbox), notebook);
    gtk_widget_show_all (notebook);

    /* Window should not be resizable */
    gtk_window_set_resizable(GTK_WINDOW(dialog), FALSE);

    gtk_dialog_add_button (GTK_DIALOG (dialog), GTK_STOCK_CLOSE, GTK_RESPONSE_OK);

}

static void
rstto_preferences_dialog_class_init(GObjectClass *object_class)
{
    parent_class = g_type_class_peek_parent (RSTTO_PREFERENCES_DIALOG_CLASS (object_class));

    object_class->dispose = rstto_preferences_dialog_dispose;
}

static void
rstto_preferences_dialog_dispose (GObject *object)
{
    RsttoPreferencesDialog *dialog = RSTTO_PREFERENCES_DIALOG (object);
    if (dialog->priv->settings)
    {
        g_object_unref (dialog->priv->settings);
        dialog->priv->settings = NULL;
    }
    
    G_OBJECT_CLASS(parent_class)->dispose(object);
}

GtkWidget *
rstto_preferences_dialog_new (GtkWindow *parent)
{
    GtkWidget *dialog = g_object_new (RSTTO_TYPE_PREFERENCES_DIALOG,
                                      "title", _("Image Viewer Preferences"),
                                      "icon-name", "ristretto",
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
cb_rstto_preferences_dialog_merge_toolbars_toggled (
        GtkToggleButton *button,
        gpointer user_data)
{
    RsttoPreferencesDialog *dialog = RSTTO_PREFERENCES_DIALOG (user_data);
    gboolean merge_toolbars = gtk_toggle_button_get_active (button);

    rstto_settings_set_boolean_property (
            dialog->priv->settings,
            "merge-toolbars",
            merge_toolbars);
}

static void
cb_rstto_preferences_dialog_zoom_revert_check_button_toggled (GtkToggleButton *button, 
                                                              gpointer user_data)
{
    RsttoPreferencesDialog *dialog = RSTTO_PREFERENCES_DIALOG (user_data);

    rstto_settings_set_boolean_property (dialog->priv->settings, "revert-zoom-direction", gtk_toggle_button_get_active(button));
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
cb_wrap_images_check_button_toggled (GtkToggleButton *button, 
                                                      gpointer user_data)
{
    RsttoPreferencesDialog *dialog = RSTTO_PREFERENCES_DIALOG (user_data);

    rstto_settings_set_boolean_property (
            dialog->priv->settings,
            "wrap-images",
            gtk_toggle_button_get_active(button));
}

static void
cb_maximize_on_startup_check_button_toggled (
        GtkToggleButton *button, 
        gpointer user_data)
{
    RsttoPreferencesDialog *dialog = RSTTO_PREFERENCES_DIALOG (user_data);

    rstto_settings_set_boolean_property (
            dialog->priv->settings,
            "maximize-on-startup",
            gtk_toggle_button_get_active(button));
}

static void
cb_choose_desktop_combo_box_changed (
        GtkComboBox *combo_box,
        gpointer user_data)
{
    RsttoPreferencesDialog *dialog = RSTTO_PREFERENCES_DIALOG (user_data);
    switch (gtk_combo_box_get_active (GTK_COMBO_BOX (combo_box)))
    {
        case DESKTOP_TYPE_NONE:
            rstto_settings_set_string_property (
                    dialog->priv->settings,
                    "desktop-type",
                    "none");
            break;
        case DESKTOP_TYPE_XFCE:
            rstto_settings_set_string_property (
                    dialog->priv->settings,
                    "desktop-type",
                    "xfce");
            break;
        case DESKTOP_TYPE_GNOME:
            rstto_settings_set_string_property (
                    dialog->priv->settings,
                    "desktop-type",
                    "gnome");
            break;
    }
}
