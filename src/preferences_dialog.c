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

static void
rstto_preferences_dialog_init(RsttoPreferencesDialog *);
static void
rstto_preferences_dialog_class_init(RsttoPreferencesDialogClass *);


static void
cb_rstto_preferences_dialog_bgcolor_override_toggled (GtkToggleButton *, gpointer);
static void
cb_rstto_preferences_dialog_bgcolor_color_set (GtkColorButton *, gpointer);

static GtkWidgetClass *parent_class = NULL;

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
    RsttoSettings *settings_manager = rstto_settings_new ();
    GtkWidget *notebook = gtk_notebook_new ();
    GtkWidget *scroll_frame, *scroll_vbox;
    GtkWidget *timeout_frame, *timeout_vbox, *timeout_lbl, *timeout_hscale;
    GtkWidget *slideshow_bgcolor_frame, *slideshow_bgcolor_vbox, *slideshow_bgcolor_hbox, *slideshow_bgcolor_button;
    GtkWidget *bgcolor_frame, *bgcolor_vbox, *bgcolor_hbox, *bgcolor_button;
    GtkWidget *cache_frame, *cache_vbox;
    GtkWidget *scaling_frame, *scaling_vbox;
    GtkWidget *cache_hbox, *cache_adjustment, *cache_spin_button, *cache_preload_check_button;
    GtkWidget *toolbar_vbox, *toolbar_frame;

    GtkWidget *widget;

/********************************************/
    GtkWidget *display_main_vbox = gtk_vbox_new(FALSE, 0);
    GtkWidget *display_main_lbl = gtk_label_new(_("Display"));
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), display_main_vbox, display_main_lbl);

    bgcolor_vbox = gtk_vbox_new(FALSE, 0);
    bgcolor_frame = xfce_create_framebox_with_content (_("Background color"), bgcolor_vbox);
    gtk_box_pack_start (GTK_BOX (display_main_vbox), bgcolor_frame, FALSE, FALSE, 0);

    widget = gtk_check_button_new_with_label (_("Override background color:"));
    bgcolor_hbox = gtk_hbox_new(FALSE, 4);
    bgcolor_button = gtk_color_button_new();
    gtk_box_pack_start (GTK_BOX (bgcolor_hbox), widget, FALSE, FALSE, 0);
    gtk_box_pack_start (GTK_BOX (bgcolor_hbox), bgcolor_button, FALSE, FALSE, 0);
    gtk_box_pack_start (GTK_BOX (bgcolor_vbox), bgcolor_hbox, FALSE, FALSE, 0);
    
    /* connect signals */
    g_signal_connect (G_OBJECT (widget), "toggled", (GCallback)cb_rstto_preferences_dialog_bgcolor_override_toggled, bgcolor_button);
    g_signal_connect (G_OBJECT (bgcolor_button), "color-set", G_CALLBACK (cb_rstto_preferences_dialog_bgcolor_color_set), NULL);

    cache_vbox = gtk_vbox_new(FALSE, 0);
    cache_frame = xfce_create_framebox_with_content (_("Image cache"), cache_vbox);
    gtk_box_pack_start (GTK_BOX (display_main_vbox), cache_frame, FALSE, FALSE, 0);

    cache_adjustment = gtk_adjustment_new(1000, 0, 9999, 1, 0, 0);
    widget = gtk_label_new (_("Cache size"));
    cache_hbox = gtk_hbox_new(FALSE, 4);
    cache_spin_button = gtk_spin_button_new(GTK_ADJUSTMENT(cache_adjustment), 1.0, 0);
    cache_preload_check_button = gtk_check_button_new_with_label (_("Preload images"));
    gtk_box_pack_start (GTK_BOX (cache_hbox), widget, FALSE, FALSE, 0);
    gtk_box_pack_start (GTK_BOX (cache_hbox), cache_spin_button, FALSE, FALSE, 0);
    gtk_box_pack_start (GTK_BOX (cache_vbox), cache_hbox, FALSE, FALSE, 0);
    gtk_box_pack_start (GTK_BOX (cache_vbox), cache_preload_check_button, FALSE, FALSE, 0);

/********************************************/
    GtkWidget *slideshow_main_vbox = gtk_vbox_new(FALSE, 0);
    GtkWidget *slideshow_main_lbl = gtk_label_new(_("Slideshow"));
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), slideshow_main_vbox, slideshow_main_lbl);

    slideshow_bgcolor_vbox = gtk_vbox_new(FALSE, 0);
    slideshow_bgcolor_frame = xfce_create_framebox_with_content (_("Background color"), slideshow_bgcolor_vbox);
    gtk_box_pack_start (GTK_BOX (slideshow_main_vbox), slideshow_bgcolor_frame, FALSE, FALSE, 0);
    
    widget = gtk_radio_button_new_with_label (NULL, _("Black"));
    gtk_box_pack_start (GTK_BOX (slideshow_bgcolor_vbox), widget, FALSE, FALSE, 0);
    widget = gtk_radio_button_new_with_label_from_widget (widget, _("Colorify (no idea how to call this feature)"));
    gtk_box_pack_start (GTK_BOX (slideshow_bgcolor_vbox), widget, FALSE, FALSE, 0);

    widget = gtk_radio_button_new_with_label_from_widget (widget, _("Custom:"));
    slideshow_bgcolor_hbox = gtk_hbox_new(FALSE, 4);
    slideshow_bgcolor_button = gtk_color_button_new();
    gtk_box_pack_start (GTK_BOX (slideshow_bgcolor_hbox), widget, FALSE, FALSE, 0);
    gtk_box_pack_start (GTK_BOX (slideshow_bgcolor_hbox), slideshow_bgcolor_button, FALSE, FALSE, 0);
    gtk_box_pack_start (GTK_BOX (slideshow_bgcolor_vbox), slideshow_bgcolor_hbox, FALSE, FALSE, 0);

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

    widget = gtk_radio_button_new_with_label (NULL, _("No action"));
    gtk_container_add (GTK_CONTAINER (scroll_vbox), widget);
    widget = gtk_radio_button_new_with_label_from_widget (widget, _("Zoom in and out"));
    gtk_container_add (GTK_CONTAINER (scroll_vbox), widget);
    widget = gtk_radio_button_new_with_label_from_widget (widget, _("Switch images"));
    gtk_container_add (GTK_CONTAINER (scroll_vbox), widget);

    toolbar_vbox = gtk_vbox_new(FALSE, 0);
    toolbar_frame = xfce_create_framebox_with_content (_("Toolbar"), toolbar_vbox);
    gtk_box_pack_start (GTK_BOX (control_main_vbox), toolbar_frame, FALSE, FALSE, 0);

    widget = gtk_check_button_new_with_label (_("Open a folder from the toolbar 'open' button"));
    gtk_container_add (GTK_CONTAINER (toolbar_vbox), widget);

/********************************************/
    GtkWidget *behaviour_main_vbox = gtk_vbox_new(FALSE, 0);
    GtkWidget *behaviour_main_lbl = gtk_label_new(_("Behaviour"));
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), behaviour_main_vbox, behaviour_main_lbl);

    scaling_vbox = gtk_vbox_new(FALSE, 0);
    scaling_frame = xfce_create_framebox_with_content (_("Scaling"), scaling_vbox);
    gtk_box_pack_start (GTK_BOX (behaviour_main_vbox), scaling_frame, FALSE, FALSE, 0);

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
    GtkWidget *color_button = GTK_WIDGET (user_data);
    RsttoSettings *settings = rstto_settings_new();
    
    GValue bgcolor_override_val = {0, };
    g_value_init (&bgcolor_override_val, G_TYPE_BOOLEAN);

    if (gtk_toggle_button_get_active (button))
    {
        g_value_set_boolean (&bgcolor_override_val, TRUE);
        gtk_widget_set_sensitive (color_button, TRUE);
    }
    else
    {
        g_value_set_boolean (&bgcolor_override_val, FALSE);
        gtk_widget_set_sensitive (color_button, FALSE);
    }

    g_object_set_property (G_OBJECT (settings), "bgcolor-override", &bgcolor_override_val);
    
}

static void
cb_rstto_preferences_dialog_bgcolor_color_set (GtkColorButton *button, gpointer user_data)
{
    RsttoSettings *settings = rstto_settings_new();

    GValue bgcolor_val = {0, };
    g_value_init (&bgcolor_val, GDK_TYPE_COLOR);

    g_object_get_property (G_OBJECT(button), "color", &bgcolor_val);
    g_object_set_property (G_OBJECT(settings), "bgcolor", &bgcolor_val);
    
}
