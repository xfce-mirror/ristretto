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

#include <libexif/exif-data.h>

#include <libxfce4ui/libxfce4ui.h>
#include <libxfce4util/libxfce4util.h>

#include "settings.h"
#include "util.h"
#include "file.h"
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
static void
properties_dialog_set_file (
        RsttoPropertiesDialog *dialog,
        RsttoFile *file);

static GtkWidgetClass *parent_class = NULL;

enum
{
    PROP_0,
    PROP_FILE,
};

struct _RsttoPropertiesDialogPriv
{
    RsttoFile *file;
    RsttoSettings *settings;

    GtkWidget *image_thumbnail;
    GtkWidget *name_entry;
    GtkWidget *mime_content_label;
    GtkWidget *modified_content_label;
    GtkWidget *accessed_content_label;
    GtkWidget *size_content_label;
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
    GtkWidget *vbox;
    GtkWidget *notebook;
    GtkWidget *table;
    GtkWidget *general_label;
    GtkWidget *name_hbox = gtk_hbox_new (FALSE, 4);
    GtkWidget *name_label = gtk_label_new(NULL);
    GtkWidget *mime_label = gtk_label_new(NULL);

    GtkWidget *modified_label = gtk_label_new(NULL);
    GtkWidget *accessed_label = gtk_label_new(NULL);

    GtkWidget *size_label  = gtk_label_new(NULL);

    dialog->priv = g_new0 (RsttoPropertiesDialogPriv, 1);

    dialog->priv->settings = rstto_settings_new ();
    dialog->priv->image_thumbnail = gtk_image_new ();
    dialog->priv->name_entry = gtk_entry_new();
    dialog->priv->mime_content_label = gtk_label_new(NULL);
    dialog->priv->modified_content_label = gtk_label_new(NULL);
    dialog->priv->accessed_content_label = gtk_label_new(NULL);
    dialog->priv->size_content_label = gtk_label_new(NULL);

    gtk_misc_set_alignment (
            GTK_MISC (dialog->priv->mime_content_label),
            0.0,
            0.5);
    gtk_misc_set_alignment (
            GTK_MISC (dialog->priv->modified_content_label),
            0.0,
            0.5);
    gtk_misc_set_alignment (
            GTK_MISC (dialog->priv->accessed_content_label),
            0.0,
            0.5);
    gtk_misc_set_alignment (
            GTK_MISC (dialog->priv->size_content_label),
            0.0,
            0.5);

    vbox = gtk_dialog_get_content_area (
            GTK_DIALOG (dialog));
    notebook = gtk_notebook_new ();

    table = gtk_table_new (5, 2, FALSE);
    gtk_box_pack_start (
            GTK_BOX (name_hbox),
            dialog->priv->image_thumbnail,
            FALSE, TRUE, 0);
    gtk_box_pack_end (
            GTK_BOX (name_hbox),
            name_label,
            TRUE, TRUE, 0);
    gtk_label_set_markup (GTK_LABEL(name_label), _("<b>Name:</b>"));
    gtk_label_set_markup (GTK_LABEL(mime_label), _("<b>Kind:</b>"));
    gtk_label_set_markup (GTK_LABEL(modified_label), _("<b>Modified:</b>"));
    gtk_label_set_markup (GTK_LABEL(accessed_label), _("<b>Accessed:</b>"));
    gtk_label_set_markup (GTK_LABEL(size_label), _("<b>Size:</b>"));

    gtk_misc_set_alignment (GTK_MISC (name_label), 1.0, 0.5);
    gtk_misc_set_alignment (GTK_MISC (mime_label), 1.0, 0.5);
    gtk_misc_set_alignment (GTK_MISC (modified_label), 1.0, 0.5);
    gtk_misc_set_alignment (GTK_MISC (accessed_label), 1.0, 0.5);
    gtk_misc_set_alignment (GTK_MISC (size_label), 1.0, 0.5);

    gtk_table_attach (
            GTK_TABLE (table),
            name_hbox,
            0,
            1,
            0,
            1,
            GTK_SHRINK | GTK_FILL,
            GTK_SHRINK,
            4,
            4);

    gtk_table_attach (
            GTK_TABLE (table),
            dialog->priv->name_entry,
            1,
            2,
            0,
            1,
            GTK_EXPAND | GTK_FILL,
            GTK_SHRINK,
            4,
            4);

    gtk_table_attach (
            GTK_TABLE (table),
            mime_label,
            0,
            1,
            1,
            2,
            GTK_SHRINK | GTK_FILL,
            GTK_SHRINK,
            4,
            4);
    gtk_table_attach (
            GTK_TABLE (table),
            dialog->priv->mime_content_label,
            1,
            2,
            1,
            2,
            GTK_EXPAND | GTK_FILL,
            GTK_SHRINK,
            4,
            4);

    gtk_table_attach (
            GTK_TABLE (table),
            modified_label,
            0,
            1,
            2,
            3,
            GTK_SHRINK | GTK_FILL,
            GTK_SHRINK,
            4,
            4);
    gtk_table_attach (
            GTK_TABLE (table),
            dialog->priv->modified_content_label,
            1,
            2,
            2,
            3,
            GTK_EXPAND | GTK_FILL,
            GTK_SHRINK,
            4,
            4);

    gtk_table_attach (
            GTK_TABLE (table),
            accessed_label,
            0,
            1,
            3,
            4,
            GTK_SHRINK | GTK_FILL,
            GTK_SHRINK,
            4,
            4);
    gtk_table_attach (
            GTK_TABLE (table),
            dialog->priv->accessed_content_label,
            1,
            2,
            3,
            4,
            GTK_EXPAND | GTK_FILL,
            GTK_SHRINK,
            4,
            4);

    gtk_table_attach (
            GTK_TABLE (table),
            size_label,
            0,
            1,
            4,
            5,
            GTK_SHRINK | GTK_FILL,
            GTK_SHRINK,
            4,
            4);
    gtk_table_attach (
            GTK_TABLE (table),
            dialog->priv->size_content_label,
            1,
            2,
            4,
            5,
            GTK_EXPAND | GTK_FILL,
            GTK_SHRINK,
            4,
            4);

    general_label = gtk_label_new (_("General"));
    gtk_notebook_append_page (GTK_NOTEBOOK (notebook), table, general_label);

    gtk_box_pack_start (GTK_BOX(vbox), notebook, TRUE, TRUE, 3);

    gtk_widget_show_all (vbox);


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

    pspec = g_param_spec_object ("file",
                                 "",
                                 "",
                                 RSTTO_TYPE_FILE,
                                 G_PARAM_READWRITE);
    g_object_class_install_property (object_class,
                                     PROP_FILE,
                                     pspec);
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
        case PROP_FILE:
            properties_dialog_set_file (dialog, g_value_get_object (value));
            break;
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

static void
properties_dialog_set_file (
        RsttoPropertiesDialog *dialog,
        RsttoFile *file)
{
    gchar  *description;
    time_t  mtime;
    time_t  atime;
    gchar   buf[20];
    guint64 size;

    const gchar *file_uri;
    gchar *file_uri_checksum;
    gchar *filename;
    gchar *thumbnail_path;
    GdkPixbuf *pixbuf;

    GFile  *g_file;
    GFileInfo *file_info = NULL;

    dialog->priv->file = file;

    if (dialog->priv->file)
    {

        file_uri = rstto_file_get_uri (file);
        file_uri_checksum = g_compute_checksum_for_string (G_CHECKSUM_MD5, file_uri, strlen (file_uri));
        filename = g_strconcat (file_uri_checksum, ".png", NULL);

        thumbnail_path = g_build_path ("/", g_get_home_dir(), ".thumbnails", "normal", filename, NULL);
        pixbuf = gdk_pixbuf_new_from_file_at_scale (thumbnail_path, 96, 96, TRUE, NULL);
        if (NULL != pixbuf)
        {
            gtk_image_set_from_pixbuf (GTK_IMAGE(dialog->priv->image_thumbnail), pixbuf);
            g_object_unref (pixbuf);
        }

        g_file = rstto_file_get_file (file);
        file_info = g_file_query_info (
                g_file,
                "standard::content-type,standard::size,time::modified,time::access",
                0,
                NULL,
                NULL );
        description = g_content_type_get_description (g_file_info_get_content_type (file_info));
        mtime = (time_t)g_file_info_get_attribute_uint64 ( file_info, "time::modified" );
        atime = (time_t)g_file_info_get_attribute_uint64 ( file_info, "time::access" );
        size = g_file_info_get_attribute_uint64 (file_info, "standard::size");
        strftime (
                buf,
                20,
                "%Y/%m/%d",
                localtime (&mtime));
        gtk_label_set_text (
                GTK_LABEL (dialog->priv->modified_content_label),
                buf 
                );
        strftime (
                buf,
                20,
                "%Y/%m/%d",
                localtime (&atime));
        gtk_label_set_text (
                GTK_LABEL (dialog->priv->accessed_content_label),
                buf 
                );

        g_snprintf (
                buf,
                20,
                "%lu bytes",
                size);
        gtk_label_set_text (
                GTK_LABEL (dialog->priv->size_content_label),
                buf 
                );

        gtk_label_set_text (
                GTK_LABEL (dialog->priv->mime_content_label),
                description
                );
        gtk_entry_set_text (
                GTK_ENTRY (dialog->priv->name_entry),
                rstto_file_get_display_name (file)
                );
        g_free (description);
    }
}

/********************/
/* Public functions */
/********************/

GtkWidget *
rstto_properties_dialog_new (
        GtkWindow *parent,
        RsttoFile *file)
{
    gchar *title = g_strdup_printf (_("%s - Properties"), rstto_file_get_display_name (file));
    GtkWidget *dialog = g_object_new (RSTTO_TYPE_PROPERTIES_DIALOG,
                                      "title", title,
                                      "icon-name", GTK_STOCK_PROPERTIES,
                                      "file", file,
                                      NULL);

    gtk_window_set_transient_for (GTK_WINDOW (dialog), parent);

    return dialog;
}

