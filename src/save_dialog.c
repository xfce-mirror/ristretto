/*
 *  Copyright (C) Stephan Arts 2008 <stephan@xfce.org>
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

#include <thunar-vfs/thunar-vfs.h>

#include <libexif/exif-data.h>

#include "navigator.h"
#include "save_dialog.h"

GtkWidget *
rstto_save_dialog_new (GtkWindow *parent, GList *entries)
{
    GtkTreeIter iter;
    GList *list_iter = entries;

    GtkWidget *dialog = gtk_dialog_new_with_buttons (
                                _("Save images"),
                                parent,
                                GTK_DIALOG_DESTROY_WITH_PARENT,
                                GTK_STOCK_CANCEL,
                                GTK_RESPONSE_CANCEL,
                                GTK_STOCK_OK,
                                GTK_RESPONSE_OK,
                                NULL);

    GtkListStore *store = gtk_list_store_new (3, GDK_TYPE_PIXBUF, G_TYPE_STRING, G_TYPE_BOOLEAN);
    GtkWidget *treeview = gtk_tree_view_new_with_model (GTK_TREE_MODEL(store));

    GtkCellRenderer *renderer = gtk_cell_renderer_pixbuf_new();
    gtk_tree_view_insert_column_with_attributes (GTK_TREE_VIEW(treeview), 0, "", renderer, "pixbuf", 0, NULL);

    renderer = gtk_cell_renderer_text_new();
    gtk_tree_view_insert_column_with_attributes (GTK_TREE_VIEW(treeview), 1, _("Filename"), renderer, "text", 1, NULL);

    renderer = gtk_cell_renderer_toggle_new();
    g_object_set (renderer, "mode", GTK_CELL_RENDERER_MODE_EDITABLE, NULL);
    gtk_tree_view_insert_column_with_attributes (GTK_TREE_VIEW(treeview), 2, _("Save"), renderer, "active", 2, NULL);

    while (list_iter)
    {
        gtk_list_store_append (store, &iter);
        gchar *path = thunar_vfs_path_dup_string (rstto_navigator_entry_get_info(((RsttoNavigatorEntry *)list_iter->data))->path);
        gtk_list_store_set (store, &iter, 0,rstto_navigator_entry_get_thumb (entries->data, 48),1, path, 2, FALSE, -1);
        g_free (path);

        list_iter = g_list_next(list_iter);
    }



    gtk_container_add (GTK_CONTAINER(GTK_DIALOG(dialog)->vbox), treeview);
    gtk_widget_show_all (treeview);
    return dialog;
}
