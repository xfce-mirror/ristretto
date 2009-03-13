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

#include "settings.h"
#include "preferences_dialog.h"

static void
rstto_preferences_dialog_init(RsttoPreferencesDialog *);
static void
rstto_preferences_dialog_class_init(RsttoPreferencesDialogClass *);

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

    gtk_container_add (GTK_CONTAINER (GTK_DIALOG (dialog)->vbox), notebook);
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
