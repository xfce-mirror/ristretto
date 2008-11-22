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

static void
cb_rstto_save_row_toggled (GtkCellRendererToggle *cell, gchar *path, gpointer user_data);

GtkWidget *
rstto_save_dialog_new (GtkWindow *parent, GList *entries)
{
    GtkTreeIter iter;
    GtkTreeViewColumn *column = NULL;
    GList *list_iter = entries;
    GtkCellRenderer *renderer;
    GtkListStore *store;
    GtkWidget *treeview, *s_window;
    GtkWidget *dialog = gtk_dialog_new_with_buttons (
                                _("Save images"),
                                parent,
                                GTK_DIALOG_DESTROY_WITH_PARENT,
                                GTK_STOCK_CANCEL,
                                GTK_RESPONSE_CANCEL,
                                GTK_STOCK_SAVE,
                                GTK_RESPONSE_OK,
                                NULL);

    store = gtk_list_store_new (4, GDK_TYPE_PIXBUF, G_TYPE_STRING, G_TYPE_BOOLEAN, G_TYPE_BOOLEAN);
    treeview = gtk_tree_view_new_with_model (GTK_TREE_MODEL(store));

    renderer = gtk_cell_renderer_pixbuf_new();
    column = gtk_tree_view_column_new_with_attributes ( "", renderer, "pixbuf", 0, NULL);
    gtk_tree_view_insert_column (GTK_TREE_VIEW(treeview), column, -1);

    renderer = gtk_cell_renderer_text_new();
    g_object_set (renderer, "ellipsize", PANGO_ELLIPSIZE_MIDDLE, NULL);
    column = gtk_tree_view_column_new_with_attributes ( _("Filename"), renderer, "text", 1, NULL);
    gtk_tree_view_column_set_expand (column, TRUE);
    gtk_tree_view_insert_column (GTK_TREE_VIEW(treeview), column, -1);

    renderer = gtk_cell_renderer_toggle_new();
    g_object_set (renderer, "mode", GTK_CELL_RENDERER_MODE_ACTIVATABLE, NULL);
    g_signal_connect (renderer, "toggled", (GCallback)cb_rstto_save_row_toggled, store);

    column = gtk_tree_view_column_new_with_attributes ( _("Save"), renderer, "active", 2, NULL);
    gtk_tree_view_insert_column (GTK_TREE_VIEW(treeview), column, -1);

    while (list_iter)
    {
        gtk_list_store_append (store, &iter);
        gchar *path = thunar_vfs_path_dup_string (rstto_navigator_entry_get_info(((RsttoNavigatorEntry *)list_iter->data))->path);
        gtk_list_store_set (store, &iter, 0,rstto_navigator_entry_get_thumb (entries->data, 48),1, path, 2, FALSE, -1);
        g_free (path);

        list_iter = g_list_next(list_iter);
    }


    s_window = gtk_scrolled_window_new (NULL, NULL);
    gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (s_window), GTK_POLICY_NEVER, GTK_POLICY_ALWAYS);
    gtk_scrolled_window_add_with_viewport (GTK_SCROLLED_WINDOW (s_window), treeview);

    gtk_container_add (GTK_CONTAINER(GTK_DIALOG(dialog)->vbox), s_window);
    gtk_widget_show_all (s_window);
    return dialog;
}

static void
cb_rstto_save_row_toggled (GtkCellRendererToggle *cell, gchar *path, gpointer user_data)
{
    GtkTreeModel *model = GTK_TREE_MODEL(user_data);
    GtkTreeIter iter;

    gtk_tree_model_get_iter_from_string (model, &iter, path);
    gtk_list_store_set (GTK_LIST_STORE(model), &iter, 2, !gtk_cell_renderer_toggle_get_active (cell), -1);
}
