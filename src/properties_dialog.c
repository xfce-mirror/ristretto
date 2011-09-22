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
#include "properties_dialog.h"

static void
rstto_properties_dialog_init (RsttoPropertiesDialog *);
static void
rstto_properties_dialog_class_init (GObjectClass *);

static void
rstto_properties_dialog_dispose (GObject *object);

static void
rstto_properties_dialog_set_property (
        GObject      *object,
        guint         property_id,
        const GValue *value,
        GParamSpec   *pspec);
static void
rstto_properties_dialog_get_property (
        GObject    *object,
        guint       property_id,
        GValue     *value,
        GParamSpec *pspec);

static GtkWidgetClass *parent_class = NULL;

enum
{
    PROP_0,
};

struct _RsttoPropertiesDialogPriv
{
    GFile *file;
    RsttoSettings *settings;
};

GType
rstto_properties_dialog_get_type (void)
{
    static GType rstto_properties_dialog_type = 0;

    if (!rstto_properties_dialog_type)
    {
        static const GTypeInfo rstto_properties_dialog_info = 
        {
            sizeof (RsttoPropertiesDialogClass),
            (GBaseInitFunc) NULL,
            (GBaseFinalizeFunc) NULL,
            (GClassInitFunc) rstto_properties_dialog_class_init,
            (GClassFinalizeFunc) NULL,
            NULL,
            sizeof (RsttoPropertiesDialog),
            0,
            (GInstanceInitFunc) rstto_properties_dialog_init,
            NULL
        };

        rstto_properties_dialog_type = g_type_register_static (
                GTK_TYPE_DIALOG,
                "RsttoPropertiesDialog",
                &rstto_properties_dialog_info,
                0);
    }
    return rstto_properties_dialog_type;
}

static void
rstto_properties_dialog_init (RsttoPropertiesDialog *dialog)
{
    dialog->priv = g_new0 (RsttoPropertiesDialogPriv, 1);

    dialog->priv->settings = rstto_settings_new ();

    gtk_dialog_add_button (GTK_DIALOG (dialog), GTK_STOCK_CLOSE, GTK_RESPONSE_OK);
}

static void
rstto_properties_dialog_class_init (GObjectClass *object_class)
{
    GParamSpec *pspec;

    parent_class = g_type_class_peek_parent (
            RSTTO_PROPERTIES_DIALOG_CLASS (object_class));

    object_class->dispose = rstto_properties_dialog_dispose;

    object_class->set_property = rstto_properties_dialog_set_property;
    object_class->get_property = rstto_properties_dialog_get_property;
}

static void
rstto_properties_dialog_dispose (GObject *object)
{
    RsttoPropertiesDialog *dialog = RSTTO_PROPERTIES_DIALOG (object);
    if (dialog->priv->settings)
    {
        g_object_unref (dialog->priv->settings);
        dialog->priv->settings = NULL;
    }
    
    G_OBJECT_CLASS(parent_class)->dispose(object);
}


static void
rstto_properties_dialog_set_property (
        GObject      *object,
        guint         property_id,
        const GValue *value,
        GParamSpec   *pspec)
{
    RsttoPropertiesDialog *dialog = RSTTO_PROPERTIES_DIALOG (object);

    switch (property_id)
    {
        default:
            break;
    }

}

static void
rstto_properties_dialog_get_property (
        GObject    *object,
        guint       property_id,
        GValue     *value,
        GParamSpec *pspec)
{
}

/********************/
/* Public functions */
/********************/

GtkWidget *
rstto_properties_dialog_new (
        GtkWindow *parent,
        GFile *file)
{
    gchar *title = g_strdup_printf (_("%s - Properties"), g_file_get_basename(file));
    GtkWidget *dialog = g_object_new (RSTTO_TYPE_PROPERTIES_DIALOG,
                                      "title", title,
                                      "icon-name", GTK_STOCK_PROPERTIES,
                                      NULL);

    gtk_window_set_transient_for (GTK_WINDOW (dialog), parent);

    return dialog;
}
