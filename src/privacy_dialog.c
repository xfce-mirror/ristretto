/*
 *  Copyright (c) Stephan Arts 2006-2011 <stephan@xfce.org>
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

#include "settings.h"
#include "privacy_dialog.h"

static void
rstto_privacy_dialog_init(RsttoPrivacyDialog *);
static void
rstto_privacy_dialog_class_init(GObjectClass *);
static void
rstto_recent_chooser_init (GtkRecentChooserIface *iface);

static void
rstto_privacy_dialog_dispose (GObject *object);

static void
rstto_privacy_dialog_set_property    (GObject      *object,
                                      guint         property_id,
                                      const GValue *value,
                                      GParamSpec   *pspec);
static void
rstto_privacy_dialog_get_property    (GObject    *object,
                                      guint       property_id,
                                      GValue     *value,
                                      GParamSpec *pspec);

static void
cb_rstto_privacy_dialog_combobox_timeframe_changed (GtkComboBox *, gpointer user_data);


static GtkWidgetClass *parent_class = NULL;

enum
{
    PROP_0,
    PROP_FILTER,
    PROP_LIMIT,
    PROP_LOCAL_ONLY,
    PROP_SELECT_MULTIPLE,
    PROP_RECENT_MANAGER,
    PROP_SORT_TYPE,
    PROP_SHOW_TIPS,
    PROP_SHOW_ICONS,
    PROP_SHOW_NOT_FOUND,
    PROP_SHOW_PRIVATE,
};

struct _RsttoPrivacyDialogPriv
{
    RsttoSettings *settings;

    GtkWidget *cleanup_frame;
    GtkWidget *cleanup_vbox;
    GtkWidget *cleanup_timeframe_combo;
};

GType
rstto_privacy_dialog_get_type (void)
{
    static GType rstto_privacy_dialog_type = 0;

    if (!rstto_privacy_dialog_type)
    {
        static const GTypeInfo rstto_privacy_dialog_info = 
        {
            sizeof (RsttoPrivacyDialogClass),
            (GBaseInitFunc) NULL,
            (GBaseFinalizeFunc) NULL,
            (GClassInitFunc) rstto_privacy_dialog_class_init,
            (GClassFinalizeFunc) NULL,
            NULL,
            sizeof (RsttoPrivacyDialog),
            0,
            (GInstanceInitFunc) rstto_privacy_dialog_init,
            NULL
        };

        rstto_privacy_dialog_type = g_type_register_static (XFCE_TYPE_TITLED_DIALOG, "RsttoPrivacyDialog", &rstto_privacy_dialog_info, 0);

        static const GInterfaceInfo recent_chooser_info =
        {
            (GInterfaceInitFunc) rstto_recent_chooser_init,
            NULL,
            NULL
        };

        g_type_add_interface_static (rstto_privacy_dialog_type, GTK_TYPE_RECENT_CHOOSER, &recent_chooser_info);
    }
    return rstto_privacy_dialog_type;
}

static void
rstto_privacy_dialog_init(RsttoPrivacyDialog *dialog)
{
    GtkWidget *display_main_hbox;
    GtkWidget *display_main_lbl;

    dialog->priv = g_new0 (RsttoPrivacyDialogPriv, 1);

    dialog->priv->settings = rstto_settings_new ();

    display_main_hbox = gtk_hbox_new(FALSE, 0);
    display_main_lbl = gtk_label_new(_("Timerange to clear:"));


    dialog->priv->cleanup_vbox = gtk_vbox_new(FALSE, 0);
    dialog->priv->cleanup_frame = xfce_gtk_frame_box_new_with_content(_("Cleanup"), dialog->priv->cleanup_vbox);
    dialog->priv->cleanup_timeframe_combo = gtk_combo_box_new_text();
    gtk_combo_box_insert_text(GTK_COMBO_BOX(dialog->priv->cleanup_timeframe_combo), 0, N_("Last Hour"));
    gtk_combo_box_insert_text(GTK_COMBO_BOX(dialog->priv->cleanup_timeframe_combo), 1, N_("Last Two Hours"));
    gtk_combo_box_insert_text(GTK_COMBO_BOX(dialog->priv->cleanup_timeframe_combo), 2, N_("Last Four Hours"));
    gtk_combo_box_insert_text(GTK_COMBO_BOX(dialog->priv->cleanup_timeframe_combo), 3, N_("Today"));
    gtk_combo_box_insert_text(GTK_COMBO_BOX(dialog->priv->cleanup_timeframe_combo), 4, N_("Everything"));
    g_signal_connect (G_OBJECT (dialog->priv->cleanup_timeframe_combo), 
                      "changed", (GCallback)cb_rstto_privacy_dialog_combobox_timeframe_changed, dialog);
    gtk_combo_box_set_active(GTK_COMBO_BOX(dialog->priv->cleanup_timeframe_combo), 0);

    gtk_box_pack_start (GTK_BOX (dialog->priv->cleanup_vbox), 
                        display_main_hbox, FALSE, FALSE, 0);
    gtk_box_pack_start (GTK_BOX (display_main_hbox), 
                        display_main_lbl, FALSE, FALSE, 0);
    gtk_box_pack_start (GTK_BOX (display_main_hbox), 
                        dialog->priv->cleanup_timeframe_combo, FALSE, FALSE, 0);

    gtk_container_add (GTK_CONTAINER (GTK_DIALOG (dialog)->vbox), 
                       dialog->priv->cleanup_frame);

    gtk_widget_show_all (dialog->priv->cleanup_frame);

    /* Window should not be resizable */
    gtk_window_set_resizable(GTK_WINDOW(dialog), FALSE);

    gtk_dialog_add_button (GTK_DIALOG (dialog), GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL);
    gtk_dialog_add_button (GTK_DIALOG (dialog), GTK_STOCK_APPLY, GTK_RESPONSE_OK);
}

static void
rstto_privacy_dialog_class_init(GObjectClass *object_class)
{
    GParamSpec *pspec;

    parent_class = g_type_class_peek_parent (RSTTO_PRIVACY_DIALOG_CLASS (object_class));

    object_class->dispose = rstto_privacy_dialog_dispose;

    object_class->set_property = rstto_privacy_dialog_set_property;
    object_class->get_property = rstto_privacy_dialog_get_property;

    pspec = g_param_spec_object ("filter",
                                 "",
                                 "",
                                 GTK_TYPE_RECENT_FILTER,
                                 G_PARAM_READWRITE);
    g_object_class_install_property (object_class,
                                     PROP_FILTER,
                                     pspec);
    pspec = g_param_spec_object ("recent-manager",
                                 "",
                                 "",
                                 GTK_TYPE_RECENT_MANAGER,
                                 G_PARAM_READWRITE);
    g_object_class_install_property (object_class,
                                     PROP_RECENT_MANAGER,
                                     pspec);
    pspec = g_param_spec_int    ("limit",
                                 "",
                                 "",
                                 -1,
                                 100,
                                 -1,
                                 G_PARAM_READWRITE);
    g_object_class_install_property (object_class,
                                     PROP_LIMIT,
                                     pspec);
    pspec = g_param_spec_boolean ("local-only",
                                  "",
                                  "",
                                  FALSE,
                                  G_PARAM_READWRITE);
    g_object_class_install_property (object_class,
                                     PROP_LOCAL_ONLY,
                                     pspec);

    pspec = g_param_spec_boolean ("select-multiple",
                                  "",
                                  "",
                                  FALSE,
                                  G_PARAM_READWRITE);
    g_object_class_install_property (object_class,
                                     PROP_SELECT_MULTIPLE,
                                     pspec);
    pspec = g_param_spec_boolean ("show-tips",
                                  "",
                                  "",
                                  FALSE,
                                  G_PARAM_READWRITE);
    g_object_class_install_property (object_class,
                                     PROP_SHOW_TIPS,
                                     pspec);
    pspec = g_param_spec_boolean ("show-icons",
                                  "",
                                  "",
                                  FALSE,
                                  G_PARAM_READWRITE);
    g_object_class_install_property (object_class,
                                     PROP_SHOW_ICONS,
                                     pspec);
    pspec = g_param_spec_boolean ("show-not-found",
                                  "",
                                  "",
                                  FALSE,
                                  G_PARAM_READWRITE);
    g_object_class_install_property (object_class,
                                     PROP_SHOW_NOT_FOUND,
                                     pspec);
    pspec = g_param_spec_boolean ("show-private",
                                  "",
                                  "",
                                  FALSE,
                                  G_PARAM_READWRITE);
    g_object_class_install_property (object_class,
                                     PROP_SHOW_PRIVATE,
                                     pspec);
    pspec = g_param_spec_enum   ("sort-type",
                                 "",
                                 "",
                                 GTK_TYPE_RECENT_SORT_TYPE,
                                 GTK_RECENT_SORT_NONE,
                                 G_PARAM_READWRITE);
    g_object_class_install_property (object_class,
                                     PROP_SORT_TYPE,
                                     pspec);
}

static void
rstto_recent_chooser_init (GtkRecentChooserIface *iface)
{
}

static void
rstto_privacy_dialog_dispose (GObject *object)
{
    RsttoPrivacyDialog *dialog = RSTTO_PRIVACY_DIALOG (object);
    if (dialog->priv->settings)
    {
        g_object_unref (dialog->priv->settings);
        dialog->priv->settings = NULL;
    }
    
    G_OBJECT_CLASS(parent_class)->dispose(object);
}


GtkWidget *
rstto_privacy_dialog_new (GtkWindow *parent)
{
    GtkWidget *dialog = g_object_new (RSTTO_TYPE_PRIVACY_DIALOG,
                                      "title", _("Clear private data"),
                                      "icon-name", GTK_STOCK_CLEAR,
                                      NULL);
    gtk_window_set_transient_for (GTK_WINDOW (dialog), parent);

    return dialog;
}

static void
rstto_privacy_dialog_set_property    (GObject      *object,
                                      guint         property_id,
                                      const GValue *value,
                                      GParamSpec   *pspec)
{
}

static void
rstto_privacy_dialog_get_property    (GObject    *object,
                                      guint       property_id,
                                      GValue     *value,
                                      GParamSpec *pspec)
{
}
/***************/
/*  CALLBACKS  */
/***************/
static void
cb_rstto_privacy_dialog_combobox_timeframe_changed (GtkComboBox *combobox, gpointer user_data)
{
    RsttoPrivacyDialog *dialog = RSTTO_PRIVACY_DIALOG (user_data);
}
