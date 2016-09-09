/*
 *  Copyright (c) Stephan Arts 2006-2012 <stephan@xfce.org>
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version 2
 *  of the License, or (at your option) any later version.
 * 
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 * 
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 *  02110-1301, USA.
 *
 *  Sorting-algorithm taken from the thunar filemanager. 
 *    Copyright (c) Benedict Meurer <benny@xfce.org>
 */

#include <config.h>

#include <gtk/gtk.h>

#include <stdlib.h>
#include <string.h>

#include <libexif/exif-data.h>

#include "util.h"
#include "file.h"
#include "image_list.h"
#include "thumbnailer.h"
#include "settings.h"

static void
rstto_image_list_tree_model_init (GtkTreeModelIface *iface);
static void
rstto_image_list_tree_sortable_init(GtkTreeSortableIface *iface);

static void 
rstto_image_list_init(RsttoImageList *);
static void
rstto_image_list_class_init(RsttoImageListClass *);
static void
rstto_image_list_dispose(GObject *object);

static void
cb_file_monitor_changed (
        GFileMonitor      *monitor,
        GFile             *file,
        GFile             *other_file,
        GFileMonitorEvent  event_type,
        gpointer           user_data );

static void 
rstto_image_list_iter_init(RsttoImageListIter *);
static void
rstto_image_list_iter_class_init(RsttoImageListIterClass *);
static void
rstto_image_list_iter_dispose(GObject *object);

static void
cb_rstto_wrap_images_changed (
        GObject *settings,
        GParamSpec *pspec,
        gpointer user_data);

static void
cb_rstto_thumbnailer_ready(
        RsttoThumbnailer *thumbnailer,
        RsttoFile *file,
        gpointer user_data);

static void
rstto_image_list_monitor_dir (
        RsttoImageList *image_list,
        GFile *dir );

static void
rstto_image_list_remove_all (
        RsttoImageList *image_list);

static gboolean
iter_next (
        RsttoImageListIter *iter,
        gboolean sticky);

static gboolean
iter_previous (
        RsttoImageListIter *iter,
        gboolean sticky);

static void 
iter_set_position (
        RsttoImageListIter *iter,
        gint pos,
        gboolean sticky);

/***************************************/
/*  Begin TreeModelIface Functions     */
/***************************************/

static GtkTreeModelFlags
image_list_model_get_flags ( GtkTreeModel *tree_model );

static gint
image_list_model_get_n_columns ( GtkTreeModel *tree_model );

static GType
image_list_model_get_column_type (
        GtkTreeModel *tree_model,
        gint index_ );

static gboolean
image_list_model_get_iter (
        GtkTreeModel *tree_model,
        GtkTreeIter *iter,
        GtkTreePath *path );

static GtkTreePath *
image_list_model_get_path (
        GtkTreeModel *tree_model,
        GtkTreeIter *iter );

static void 
image_list_model_get_value (
        GtkTreeModel *tree_model,
        GtkTreeIter *iter,
        gint column,
        GValue *value );

static gboolean
image_list_model_iter_children (
        GtkTreeModel *tree_model,
        GtkTreeIter *iter,
        GtkTreeIter *parent );

static gboolean
image_list_model_iter_has_child (
        GtkTreeModel *tree_model,
        GtkTreeIter *iter );

static gboolean
image_list_model_iter_parent (
        GtkTreeModel *tree_model,
        GtkTreeIter *iter,
        GtkTreeIter *child );

static gint
image_list_model_iter_n_children (
        GtkTreeModel *tree_model,
        GtkTreeIter *iter );

static gboolean 
image_list_model_iter_nth_child (
        GtkTreeModel *tree_model,
        GtkTreeIter *iter,
        GtkTreeIter *parent,
        gint n );

static gboolean
image_list_model_iter_next (
        GtkTreeModel *tree_model,
        GtkTreeIter *iter );

/***************************************/
/*  End TreeModelIface Functions       */
/***************************************/

static RsttoImageListIter * rstto_image_list_iter_new ();

static gint
cb_rstto_image_list_image_name_compare_func (RsttoFile *a, RsttoFile *b);
static gint
cb_rstto_image_list_exif_date_compare_func (RsttoFile *a, RsttoFile *b);

static GObjectClass *parent_class = NULL;
static GObjectClass *iter_parent_class = NULL;

enum
{
    RSTTO_IMAGE_LIST_SIGNAL_REMOVE_IMAGE = 0,
    RSTTO_IMAGE_LIST_SIGNAL_REMOVE_ALL,
    RSTTO_IMAGE_LIST_SIGNAL_COUNT
};

enum
{
    RSTTO_IMAGE_LIST_ITER_SIGNAL_CHANGED = 0,
    RSTTO_IMAGE_LIST_ITER_SIGNAL_PREPARE_CHANGE,
    RSTTO_IMAGE_LIST_ITER_SIGNAL_COUNT
};

struct _RsttoImageListIterPriv
{
    RsttoImageList *image_list;
    RsttoFile      *r_file;
    
    /* This is set if the iter-position is chosen by the user */
    gboolean        sticky; 
};

struct _RsttoImageListPriv
{
    gint           stamp;
    GFileMonitor  *dir_monitor;
    gint           directory_loader;
    RsttoSettings *settings;
    RsttoThumbnailer *thumbnailer;
    GtkFileFilter *filter;

    GList        *image_monitors;
    GList        *images;
    gint          n_images;

    GSList       *iterators;
    GCompareFunc  cb_rstto_image_list_compare_func;

    gboolean      wrap_images;
};

typedef struct _RsttoFileLoader RsttoFileLoader;

struct _RsttoFileLoader
{
    GFile           *dir;
    RsttoImageList  *image_list;
    GFileEnumerator *file_enum;

    guint            n_files;
    RsttoFile      **files;
};

static gboolean
cb_rstto_read_file ( gpointer user_data );

static gint rstto_image_list_signals[RSTTO_IMAGE_LIST_SIGNAL_COUNT];
static gint rstto_image_list_iter_signals[RSTTO_IMAGE_LIST_ITER_SIGNAL_COUNT];

GType
rstto_image_list_get_type (void)
{
    static const GInterfaceInfo tree_model_info =
    {
        (GInterfaceInitFunc) rstto_image_list_tree_model_init,
            NULL,
            NULL
    };
    static const GInterfaceInfo tree_sort_info =
    {
        (GInterfaceInitFunc) rstto_image_list_tree_sortable_init,
            NULL,
            NULL
    };
    static GType rstto_image_list_type = 0;

    if (!rstto_image_list_type)
    {
        static const GTypeInfo rstto_image_list_info = 
        {
            sizeof (RsttoImageListClass),
            (GBaseInitFunc) NULL,
            (GBaseFinalizeFunc) NULL,
            (GClassInitFunc) rstto_image_list_class_init,
            (GClassFinalizeFunc) NULL,
            NULL,
            sizeof (RsttoImageList),
            0,
            (GInstanceInitFunc) rstto_image_list_init,
            NULL
        };

        rstto_image_list_type = g_type_register_static (G_TYPE_OBJECT, "RsttoImageList", &rstto_image_list_info, 0);

        g_type_add_interface_static (rstto_image_list_type, GTK_TYPE_TREE_MODEL, &tree_model_info);

        g_type_add_interface_static (rstto_image_list_type, GTK_TYPE_TREE_SORTABLE, &tree_sort_info);
    }


    return rstto_image_list_type;
}

static void
rstto_image_list_tree_model_init (GtkTreeModelIface *iface)
{
    iface->get_flags       = image_list_model_get_flags;
    iface->get_n_columns   = image_list_model_get_n_columns;
    iface->get_column_type = image_list_model_get_column_type;
    iface->get_iter        = image_list_model_get_iter;
    iface->get_path        = image_list_model_get_path;
    iface->get_value       = image_list_model_get_value;
    iface->iter_children   = image_list_model_iter_children;
    iface->iter_has_child  = image_list_model_iter_has_child;
    iface->iter_n_children = image_list_model_iter_n_children;
    iface->iter_nth_child  = image_list_model_iter_nth_child;
    iface->iter_parent     = image_list_model_iter_parent;
    iface->iter_next       = image_list_model_iter_next;
}

static void
rstto_image_list_tree_sortable_init(GtkTreeSortableIface *iface)
{
#if 0 
    iface->get_sort_column_id    = sq_archive_store_get_sort_column_id;
    iface->set_sort_column_id    = sq_archive_store_set_sort_column_id;
    iface->set_sort_func         = sq_archive_store_set_sort_func;            /*NOT SUPPORTED*/
    iface->set_default_sort_func = sq_archive_store_set_default_sort_func;    /*NOT SUPPORTED*/
    iface->has_default_sort_func = sq_archive_store_has_default_sort_func;
#endif 
}

static void
rstto_image_list_init(RsttoImageList *image_list)
{

    image_list->priv = g_new0 (RsttoImageListPriv, 1);
    image_list->priv->stamp = g_random_int();
    image_list->priv->settings = rstto_settings_new ();
    image_list->priv->thumbnailer = rstto_thumbnailer_new();
    image_list->priv->filter = gtk_file_filter_new ();
    g_object_ref_sink (image_list->priv->filter);
    gtk_file_filter_add_pixbuf_formats (image_list->priv->filter);

    image_list->priv->cb_rstto_image_list_compare_func = (GCompareFunc)cb_rstto_image_list_image_name_compare_func;

    image_list->priv->wrap_images = rstto_settings_get_boolean_property (
            image_list->priv->settings,
            "wrap-images");

    g_signal_connect (
            G_OBJECT(image_list->priv->settings),
            "notify::wrap-images",
            G_CALLBACK (cb_rstto_wrap_images_changed),
            image_list);

    g_signal_connect (
            G_OBJECT(image_list->priv->thumbnailer),
            "ready",
            G_CALLBACK (cb_rstto_thumbnailer_ready),
            image_list);

}

static void
rstto_image_list_class_init(RsttoImageListClass *nav_class)
{
    GObjectClass *object_class = G_OBJECT_CLASS(nav_class);

    parent_class = g_type_class_peek_parent(nav_class);

    object_class->dispose = rstto_image_list_dispose;

    rstto_image_list_signals[RSTTO_IMAGE_LIST_SIGNAL_REMOVE_IMAGE] = g_signal_new("remove-image",
            G_TYPE_FROM_CLASS(nav_class),
            G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
            0,
            NULL,
            NULL,
            g_cclosure_marshal_VOID__OBJECT,
            G_TYPE_NONE,
            1,
            G_TYPE_OBJECT,
            NULL);

    rstto_image_list_signals[RSTTO_IMAGE_LIST_SIGNAL_REMOVE_ALL] = g_signal_new("remove-all",
            G_TYPE_FROM_CLASS(nav_class),
            G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
            0,
            NULL,
            NULL,
            g_cclosure_marshal_VOID__VOID,
            G_TYPE_NONE,
            0,
            NULL);
}

static void
rstto_image_list_dispose(GObject *object)
{
    RsttoImageList *image_list = RSTTO_IMAGE_LIST(object);

    if (NULL != image_list->priv)
    {
        if (image_list->priv->settings)
        {
            g_object_unref (image_list->priv->settings);
            image_list->priv->settings = NULL;
        }

        if (image_list->priv->thumbnailer)
        {
            g_object_unref (image_list->priv->thumbnailer);
            image_list->priv->thumbnailer = NULL;
        }

        if (image_list->priv->filter)
        {
            g_object_unref (image_list->priv->filter);
            image_list->priv->filter= NULL;
        }
        
        g_free (image_list->priv);
        image_list->priv = NULL;
    }
}

RsttoImageList *
rstto_image_list_new (void)
{
    RsttoImageList *image_list;

    image_list = g_object_new(RSTTO_TYPE_IMAGE_LIST, NULL);

    return image_list;
}

gboolean
rstto_image_list_add_file (
        RsttoImageList *image_list,
        RsttoFile *r_file,
        GError **error )
{
    GtkFileFilterInfo filter_info;
    GList *image_iter = g_list_find (image_list->priv->images, r_file);
    GSList *iter = image_list->priv->iterators;
    gint i = 0;
    GtkTreePath *path = NULL;
    GtkTreeIter t_iter;
    GFileMonitor *monitor = NULL;

    g_return_val_if_fail ( NULL != r_file , FALSE);
    g_return_val_if_fail ( RSTTO_IS_FILE (r_file) , FALSE);

    if (!image_iter)
    {
        if (r_file)
        {
            filter_info.contains =  GTK_FILE_FILTER_MIME_TYPE | GTK_FILE_FILTER_URI;
            filter_info.uri = rstto_file_get_uri (r_file);
            filter_info.mime_type = rstto_file_get_content_type (r_file);

            if ( TRUE == gtk_file_filter_filter (image_list->priv->filter, &filter_info))
            {
                g_object_ref (G_OBJECT (r_file));

                image_list->priv->images = g_list_insert_sorted (
                        image_list->priv->images,
                        r_file,
                        rstto_image_list_get_compare_func (image_list));

                image_list->priv->n_images++;

                if (image_list->priv->dir_monitor == NULL)
                {
                    monitor = g_file_monitor_file (
                            rstto_file_get_file (r_file),
                            G_FILE_MONITOR_NONE,
                            NULL,
                            NULL);
                    g_signal_connect (
                            G_OBJECT(monitor),
                            "changed",
                            G_CALLBACK (cb_file_monitor_changed),
                            image_list);
                    image_list->priv->image_monitors = g_list_prepend (
                            image_list->priv->image_monitors, 
                            monitor);
                }
                i = g_list_index (image_list->priv->images, r_file);

                path = gtk_tree_path_new();
                gtk_tree_path_append_index (path, i);
                t_iter.stamp = image_list->priv->stamp;
                t_iter.user_data = r_file;
                t_iter.user_data3 = GINT_TO_POINTER(i);

                gtk_tree_model_row_inserted (
                        GTK_TREE_MODEL(image_list),
                        path,
                        &t_iter);

                gtk_tree_path_free (path);

                /** TODO: update all iterators */
                while (iter)
                {
                    if (FALSE == RSTTO_IMAGE_LIST_ITER(iter->data)->priv->sticky)
                    {
                        rstto_image_list_iter_find_file (iter->data, r_file);
                    }
                    iter = g_slist_next (iter);
                }

                return TRUE;
            }
            else
            {
                return FALSE;
            }
        }
        return FALSE;
    }

    return TRUE;
}

gint
rstto_image_list_get_n_images (RsttoImageList *image_list)
{
    return g_list_length (image_list->priv->images);
}

/**
 * rstto_image_list_get_iter:
 * @image_list:
 *
 * TODO: track iterators
 *
 * return iter;
 */
RsttoImageListIter *
rstto_image_list_get_iter (RsttoImageList *image_list)
{
    RsttoFile *r_file = NULL;
    RsttoImageListIter *iter = NULL;

    if (image_list->priv->images)
    {
        r_file = image_list->priv->images->data;
    }

    iter = rstto_image_list_iter_new (image_list, r_file);

    image_list->priv->iterators = g_slist_prepend (image_list->priv->iterators, iter);

    return iter;
}


void
rstto_image_list_remove_file (
        RsttoImageList *image_list,
        RsttoFile *r_file)
{
    GSList *iter = NULL;
    RsttoFile *r_file_a = NULL;
    GtkTreePath *path_ = NULL;
    gint index_ = g_list_index (image_list->priv->images, r_file);
    gint n_images = rstto_image_list_get_n_images (image_list);

    if (index_ != -1)
    {

        iter = image_list->priv->iterators;
        while (iter)
        {
            if (rstto_file_equal(rstto_image_list_iter_get_file (iter->data), r_file))
            {
                
                if (rstto_image_list_iter_get_position (iter->data) == n_images -1)
                {
                    iter_previous (iter->data, FALSE);
                }
                else
                {
                    iter_next (iter->data, FALSE);
                }
                /* If the image is still the same, 
                 * it's a single item list,
                 * and we should force the image in this iter to NULL
                 */
                if (rstto_file_equal (
                        rstto_image_list_iter_get_file (iter->data),
                        r_file ) )
                {

                    image_list->priv->images = g_list_remove (image_list->priv->images, r_file);
                    ((RsttoImageListIter *)(iter->data))->priv->r_file = NULL;
                    g_signal_emit (
                            G_OBJECT (iter->data),
                            rstto_image_list_iter_signals[RSTTO_IMAGE_LIST_ITER_SIGNAL_CHANGED],
                            0,
                            NULL);
                }
            }
            iter = g_slist_next (iter);
        }

        image_list->priv->images = g_list_remove (image_list->priv->images, r_file);

        path_ = gtk_tree_path_new();
        gtk_tree_path_append_index(path_,index_);

        gtk_tree_model_row_deleted(GTK_TREE_MODEL(image_list), path_);

        iter = image_list->priv->iterators;
        while (iter)
        {
            r_file_a = rstto_image_list_iter_get_file(iter->data);
            if (NULL != r_file_a)
            {
                if (rstto_file_equal(r_file_a, r_file))
                {
                    iter_next (iter->data, FALSE);
                }
            }
            iter = g_slist_next (iter);
        }

        g_signal_emit (
                G_OBJECT (image_list),
                rstto_image_list_signals[RSTTO_IMAGE_LIST_SIGNAL_REMOVE_IMAGE],
                0,
                r_file,
                NULL);
        g_object_unref (r_file);
    }
}

static void
rstto_image_list_remove_all (RsttoImageList *image_list)
{
    GSList *iter = NULL;
    GList *image_iter = image_list->priv->images;
    GtkTreePath *path_ = NULL;
    gint i = g_list_length (image_iter);

    while (image_iter)
    {
        i--;
        path_ = gtk_tree_path_new();
        gtk_tree_path_append_index(path_, i);

        gtk_tree_model_row_deleted(GTK_TREE_MODEL(image_list), path_);

        
        image_iter = g_list_next (image_iter);     
    }

    g_list_foreach (image_list->priv->image_monitors, (GFunc)g_object_unref, NULL);
    g_list_free (image_list->priv->image_monitors);
    image_list->priv->image_monitors = NULL;

    g_list_foreach (image_list->priv->images, (GFunc)g_object_unref, NULL);
    g_list_free (image_list->priv->images);
    image_list->priv->images = NULL;

    iter = image_list->priv->iterators;
    while (iter)
    {
        iter_set_position (iter->data, -1, FALSE);
        iter = g_slist_next (iter);
    }
    g_signal_emit (G_OBJECT (image_list), rstto_image_list_signals[RSTTO_IMAGE_LIST_SIGNAL_REMOVE_ALL], 0, NULL);
}

gboolean
rstto_image_list_set_directory (
        RsttoImageList *image_list,
        GFile *dir,
        GError **error )
{
    /* Declare variables */
    GFileEnumerator *file_enumerator = NULL;
    RsttoFileLoader *loader = NULL;

    /* Source code block */
    if (image_list->priv->directory_loader != 0)
    {
        g_source_remove (image_list->priv->directory_loader);
        image_list->priv->directory_loader = 0;
    }

    rstto_image_list_remove_all (image_list);

    rstto_image_list_monitor_dir ( 
            image_list,
            dir );

    /* Allow all images to be removed by providing NULL to dir */
    if ( NULL != dir )
    {
        file_enumerator = g_file_enumerate_children (dir, "standard::*", 0, NULL, NULL);

        if (NULL != file_enumerator)
        {
            g_object_ref (dir);

            loader = g_new0 (RsttoFileLoader, 1);
            loader->dir = dir;
            loader->file_enum = file_enumerator;
            loader->image_list = image_list;

            image_list->priv->directory_loader = g_idle_add ( (GSourceFunc) cb_rstto_read_file, loader );
        }
    }

    return TRUE;
}

static gboolean
cb_rstto_read_file ( gpointer user_data )
{
    RsttoFileLoader *loader = user_data;
    GFileInfo       *file_info;
    const gchar     *content_type;
    const gchar     *filename;
    RsttoFile      **files;
    GFile           *child_file;
    guint            i;
    GSList          *iter;

    /* Check the inputs */
    g_return_val_if_fail ( NULL != loader, FALSE );
    g_return_val_if_fail ( NULL != loader->file_enum, FALSE );

    file_info = g_file_enumerator_next_file (
            loader->file_enum,
            NULL,
            NULL );
    if ( NULL != file_info )
    {
        /* Allow for 'progressive' loading */
        if (loader->n_files == 100)
        {
            for (i = 0; i < loader->n_files; ++i)
            {
                rstto_image_list_add_file (
                        loader->image_list,
                        loader->files[i],
                        NULL);

                g_object_unref (loader->files[i]);
            }

            iter = loader->image_list->priv->iterators;
            while (iter)
            {
                g_signal_emit (G_OBJECT (iter->data), rstto_image_list_iter_signals[RSTTO_IMAGE_LIST_ITER_SIGNAL_CHANGED], 0, NULL);
                iter = g_slist_next (iter);
            }

            g_free (loader->files);
            loader->files = NULL;
            loader->n_files = 0;
        }

        /* Add file to the list */
        content_type  = g_file_info_get_content_type (file_info);
        if (strncmp (content_type, "image/", 6) == 0)
        {
            filename = g_file_info_get_name (file_info);
            child_file = g_file_get_child (loader->dir, filename);
            files = g_new0 ( RsttoFile *, loader->n_files+1);
            files[0] = rstto_file_new (child_file);

            g_object_unref (child_file);

            for (i = 0; i < loader->n_files; ++i)
            {
                files[i+1] = loader->files[i];
            }

            if ( NULL != loader->files )
            {
                g_free (loader->files);
            }
            loader->files = files;
            loader->n_files++;
        }

        g_object_unref (file_info);
    }
    else
    {
        for (i = 0; i < loader->n_files; ++i)
        {
            rstto_image_list_add_file (
                    loader->image_list,
                    loader->files[i],
                    NULL);

            g_object_unref (loader->files[i]);
        }

        /* This is a hack, use a closure */
        if (loader->image_list->priv->directory_loader != 0)
        {
            g_source_remove (loader->image_list->priv->directory_loader);
            loader->image_list->priv->directory_loader = 0;
        }

        iter = loader->image_list->priv->iterators;
        while (iter)
        {
            g_signal_emit (G_OBJECT (iter->data), rstto_image_list_iter_signals[RSTTO_IMAGE_LIST_ITER_SIGNAL_CHANGED], 0, NULL);
            iter = g_slist_next (iter);
        }

        if (loader->n_files > 0)
        {
            g_free (loader->files);
        }
        g_object_unref (loader->file_enum);
        g_free (loader);

        return FALSE;
    }
    return TRUE;
}

static void
rstto_image_list_monitor_dir (
        RsttoImageList *image_list,
        GFile *dir )
{
    GFileMonitor *monitor = NULL;

    if ( NULL != image_list->priv->dir_monitor )
    {
        g_object_unref (image_list->priv->dir_monitor);
        image_list->priv->dir_monitor = NULL;
    }

    /* Allow a monitor to be removed by providing NULL to dir */
    if ( NULL != dir )
    {
        monitor = g_file_monitor_directory (
                dir,
                G_FILE_MONITOR_NONE,
                NULL,
                NULL);

        g_signal_connect (
                G_OBJECT(monitor),
                "changed",
                G_CALLBACK (cb_file_monitor_changed),
                image_list);
    }

    if (image_list->priv->image_monitors)
    {
        g_list_foreach (image_list->priv->image_monitors, (GFunc)g_object_unref, NULL);
        g_list_free (image_list->priv->image_monitors);
        image_list->priv->image_monitors = NULL;
    }

    image_list->priv->dir_monitor = monitor;
}

static void
cb_file_monitor_changed (
        GFileMonitor      *monitor,
        GFile             *file,
        GFile             *other_file,
        GFileMonitorEvent  event_type,
        gpointer           user_data )
{
    RsttoImageList *image_list = RSTTO_IMAGE_LIST (user_data);
    RsttoFile *r_file = rstto_file_new (file);

    switch ( event_type )
    {
        case G_FILE_MONITOR_EVENT_DELETED:
            rstto_image_list_remove_file ( image_list, r_file );
            if (image_list->priv->dir_monitor == NULL)
            {
                image_list->priv->image_monitors = g_list_remove (
                        image_list->priv->image_monitors,
                        monitor);   
                g_object_unref (monitor);
                monitor = NULL;
            }
            break;
        case G_FILE_MONITOR_EVENT_CREATED:
            rstto_image_list_add_file (image_list, r_file, NULL);
            break;
        case G_FILE_MONITOR_EVENT_MOVED:
            rstto_image_list_remove_file ( image_list, r_file );

            /* Remove our reference, reusing pointer */
            g_object_unref (r_file);

            r_file = rstto_file_new (other_file);
            rstto_image_list_add_file (image_list, r_file, NULL);

            if (image_list->priv->dir_monitor == NULL)
            {
                image_list->priv->image_monitors = g_list_remove (
                        image_list->priv->image_monitors,
                        monitor);   
                g_object_unref (monitor);
                monitor = g_file_monitor_file (
                        other_file,
                        G_FILE_MONITOR_NONE,
                        NULL,
                        NULL);
                g_signal_connect (
                        G_OBJECT(monitor),
                        "changed",
                        G_CALLBACK (cb_file_monitor_changed),
                        image_list);
                image_list->priv->image_monitors = g_list_prepend (
                        image_list->priv->image_monitors, 
                        monitor);
            }
            break;
        case G_FILE_MONITOR_EVENT_CHANGED:
            rstto_file_changed (r_file);
            break;
        default:
            break;
    }

    if ( NULL != r_file )
    {
        g_object_unref (r_file);
    }
}



GType
rstto_image_list_iter_get_type (void)
{
    static GType rstto_image_list_iter_type = 0;

    if (!rstto_image_list_iter_type)
    {
        static const GTypeInfo rstto_image_list_iter_info = 
        {
            sizeof (RsttoImageListIterClass),
            (GBaseInitFunc) NULL,
            (GBaseFinalizeFunc) NULL,
            (GClassInitFunc) rstto_image_list_iter_class_init,
            (GClassFinalizeFunc) NULL,
            NULL,
            sizeof (RsttoImageListIter),
            0,
            (GInstanceInitFunc) rstto_image_list_iter_init,
            NULL
        };

        rstto_image_list_iter_type = g_type_register_static (G_TYPE_OBJECT, "RsttoImageListIter", &rstto_image_list_iter_info, 0);
    }
    return rstto_image_list_iter_type;
}

static void
rstto_image_list_iter_init (RsttoImageListIter *iter)
{
    iter->priv = g_new0 (RsttoImageListIterPriv, 1);
}

static void
rstto_image_list_iter_class_init(RsttoImageListIterClass *iter_class)
{
    GObjectClass *object_class = G_OBJECT_CLASS(iter_class);

    iter_parent_class = g_type_class_peek_parent(iter_class);

    object_class->dispose = rstto_image_list_iter_dispose;

    rstto_image_list_iter_signals[RSTTO_IMAGE_LIST_ITER_SIGNAL_PREPARE_CHANGE] = g_signal_new("prepare-change",
            G_TYPE_FROM_CLASS(iter_class),
            G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
            0,
            NULL,
            NULL,
            g_cclosure_marshal_VOID__VOID,
            G_TYPE_NONE,
            0,
            NULL);

    rstto_image_list_iter_signals[RSTTO_IMAGE_LIST_ITER_SIGNAL_CHANGED] = g_signal_new("changed",
            G_TYPE_FROM_CLASS(iter_class),
            G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
            0,
            NULL,
            NULL,
            g_cclosure_marshal_VOID__VOID,
            G_TYPE_NONE,
            0,
            NULL);
}

static void
rstto_image_list_iter_dispose (GObject *object)
{
    RsttoImageListIter *iter = RSTTO_IMAGE_LIST_ITER(object);

    if (iter->priv)
    {
        if (iter->priv->r_file)
        {
            iter->priv->r_file = NULL;
        }

        if (iter->priv->image_list)
        {
            iter->priv->image_list->priv->iterators = g_slist_remove (iter->priv->image_list->priv->iterators, iter);
            iter->priv->image_list= NULL;
        }

        g_free (iter->priv);
        iter->priv = NULL;
    }
}

static RsttoImageListIter *
rstto_image_list_iter_new (
        RsttoImageList *nav,
        RsttoFile *r_file)
{
    RsttoImageListIter *iter;

    iter = g_object_new(RSTTO_TYPE_IMAGE_LIST_ITER, NULL);
    iter->priv->r_file = r_file;
    iter->priv->image_list = nav;

    return iter;
}

gboolean
rstto_image_list_iter_find_file (
        RsttoImageListIter *iter,
        RsttoFile *r_file)
{
    gint pos = g_list_index (iter->priv->image_list->priv->images, r_file);

    if (pos > -1)
    {
        g_signal_emit (
                G_OBJECT (iter),
                rstto_image_list_iter_signals[RSTTO_IMAGE_LIST_ITER_SIGNAL_PREPARE_CHANGE],
                0,
                NULL);

        if (iter->priv->r_file)
        {
            iter->priv->r_file = NULL;
        }
        iter->priv->r_file = r_file;
        iter->priv->sticky = TRUE;

        g_signal_emit (
                G_OBJECT (iter),
                rstto_image_list_iter_signals[RSTTO_IMAGE_LIST_ITER_SIGNAL_CHANGED],
                0,
                NULL);

        return TRUE;
    }
    return FALSE;
}

gint
rstto_image_list_iter_get_position (RsttoImageListIter *iter)
{
    if ( NULL == iter->priv->r_file )
    {
        return -1;
    }
    return g_list_index (iter->priv->image_list->priv->images, iter->priv->r_file);
}

RsttoFile *
rstto_image_list_iter_get_file (RsttoImageListIter *iter)
{
    return iter->priv->r_file;
}

static void
iter_set_position (
        RsttoImageListIter *iter,
        gint pos,
        gboolean sticky )
{
    g_signal_emit (
            G_OBJECT (iter),
            rstto_image_list_iter_signals[RSTTO_IMAGE_LIST_ITER_SIGNAL_PREPARE_CHANGE],
            0,
            NULL);

    if (iter->priv->r_file)
    {
        iter->priv->r_file = NULL;
    }

    if (pos >= 0)
    {
        iter->priv->r_file = g_list_nth_data (iter->priv->image_list->priv->images, pos); 
    }

    g_signal_emit (
            G_OBJECT (iter),
            rstto_image_list_iter_signals[RSTTO_IMAGE_LIST_ITER_SIGNAL_CHANGED],
            0,
            NULL);
}

void
rstto_image_list_iter_set_position (
        RsttoImageListIter *iter,
        gint pos)
{
    iter_set_position ( iter, pos, TRUE );
}

static gboolean
iter_next (
        RsttoImageListIter *iter,
        gboolean sticky)
{
    GList *position = NULL;
    RsttoImageList *image_list = iter->priv->image_list;
    RsttoFile *r_file = iter->priv->r_file;
    gboolean ret_val = FALSE;

    g_signal_emit (
            G_OBJECT (iter),
            rstto_image_list_iter_signals[RSTTO_IMAGE_LIST_ITER_SIGNAL_PREPARE_CHANGE],
            0,
            NULL);

    if (r_file)
    {
        position = g_list_find (iter->priv->image_list->priv->images, r_file);
        iter->priv->r_file = NULL;
    }

    iter->priv->sticky = sticky;

    position = g_list_next (position);
    if (position)
    {
        iter->priv->r_file = position->data; 

        /* We could move forward, set ret_val to TRUE */
        ret_val = TRUE;
    }
    else
    {

        if (TRUE == image_list->priv->wrap_images)
        {
            position = g_list_first (iter->priv->image_list->priv->images);

            /* We could move forward, wrapped back to the start of the
             * list, set ret_val to TRUE
             */
            ret_val = TRUE;
        }
        else
        {
            position = g_list_last (iter->priv->image_list->priv->images);
        }

        if (position)
        {
            iter->priv->r_file = position->data; 
        }
        else
        {
            iter->priv->r_file = NULL;
        }
    }

    if (r_file != iter->priv->r_file)
    {
        g_signal_emit (
                G_OBJECT (iter),
                rstto_image_list_iter_signals[RSTTO_IMAGE_LIST_ITER_SIGNAL_CHANGED],
                0,
                NULL);
    }

    return ret_val;
}

gboolean
rstto_image_list_iter_next (RsttoImageListIter *iter)
{
    return iter_next (iter, TRUE);
}

gboolean
rstto_image_list_iter_has_next (RsttoImageListIter *iter)
{
    RsttoImageList *image_list = iter->priv->image_list;

    if (image_list->priv->wrap_images)
    {
        return TRUE;
    }
    else
    {
        if (rstto_image_list_iter_get_position (iter) ==
            (rstto_image_list_get_n_images (image_list) -1))
        {
            return FALSE;
        }
        else
        {
            return TRUE;
        }
    }
}

static gboolean
iter_previous (
        RsttoImageListIter *iter,
        gboolean sticky)
{
    GList *position = NULL;
    RsttoImageList *image_list = iter->priv->image_list;
    RsttoFile *r_file = iter->priv->r_file;
    gboolean ret_val = FALSE;

    g_signal_emit (
            G_OBJECT (iter),
            rstto_image_list_iter_signals[RSTTO_IMAGE_LIST_ITER_SIGNAL_PREPARE_CHANGE],
            0,
            NULL);

    if (iter->priv->r_file)
    {
        position = g_list_find (iter->priv->image_list->priv->images, iter->priv->r_file);
        iter->priv->r_file = NULL;
    }

    iter->priv->sticky = sticky;

    position = g_list_previous (position);
    if (position)
    {
        iter->priv->r_file = position->data; 
    }
    else
    {
        if (TRUE == image_list->priv->wrap_images)
        {
            position = g_list_last (iter->priv->image_list->priv->images);
        }
        else
        {
            position = g_list_first (iter->priv->image_list->priv->images);
        }

        if (position)
        {
            iter->priv->r_file = position->data; 
        }
        else
        {
            iter->priv->r_file = NULL;
        }
    }

    if (r_file != iter->priv->r_file)
    {
        g_signal_emit (
                G_OBJECT (iter),
                rstto_image_list_iter_signals[RSTTO_IMAGE_LIST_ITER_SIGNAL_CHANGED],
                0,
                NULL);
    }

    return ret_val;

}
gboolean
rstto_image_list_iter_previous (RsttoImageListIter *iter)
{
    return iter_previous (iter, TRUE);
}


gboolean
rstto_image_list_iter_has_previous (RsttoImageListIter *iter)
{
    RsttoImageList *image_list = iter->priv->image_list;

    if (image_list->priv->wrap_images)
    {
        return TRUE;
    }
    else
    {
        if (rstto_image_list_iter_get_position (iter) == 0)
        {
            return FALSE;
        }
        else
        {
            return TRUE;
        }
    }
}


RsttoImageListIter *
rstto_image_list_iter_clone (RsttoImageListIter *iter)
{
    RsttoImageListIter *new_iter = rstto_image_list_iter_new (
            iter->priv->image_list,
            iter->priv->r_file);

    rstto_image_list_iter_set_position (
            new_iter,
            rstto_image_list_iter_get_position ( iter ) );

    return new_iter;
}

GCompareFunc
rstto_image_list_get_compare_func (RsttoImageList *image_list)
{
    return (GCompareFunc)image_list->priv->cb_rstto_image_list_compare_func;
}

void
rstto_image_list_set_compare_func (RsttoImageList *image_list, GCompareFunc func)
{
    GSList *iter = NULL;
    image_list->priv->cb_rstto_image_list_compare_func = func;
    image_list->priv->images = g_list_sort (image_list->priv->images,  func);

    for (iter = image_list->priv->iterators; iter != NULL; iter = g_slist_next (iter))
    {
        g_signal_emit (
                G_OBJECT (iter->data),
                rstto_image_list_iter_signals[RSTTO_IMAGE_LIST_ITER_SIGNAL_CHANGED],
                0,
                NULL);
    }
}

/***********************/
/*  Compare Functions  */
/***********************/

void
rstto_image_list_set_sort_by_name (RsttoImageList *image_list)
{
    rstto_image_list_set_compare_func (image_list, (GCompareFunc)cb_rstto_image_list_image_name_compare_func);
}

void
rstto_image_list_set_sort_by_date (RsttoImageList *image_list)
{
    rstto_image_list_set_compare_func (image_list, (GCompareFunc)cb_rstto_image_list_exif_date_compare_func);
}

/**
 * cb_rstto_image_list_image_name_compare_func:
 * @a:
 * @b:
 *
 *
 * Return value: (see strcmp)
 */
static gint
cb_rstto_image_list_image_name_compare_func (RsttoFile *a, RsttoFile *b)
{
    const gchar *a_collate_key = rstto_file_get_collate_key (a);
    const gchar *b_collate_key = rstto_file_get_collate_key (b);

    return g_strcmp0(a_collate_key, b_collate_key);
}

/**
 * cb_rstto_image_list_exif_date_compare_func:
 * @a:
 * @b:
 *
 * TODO: Use EXIF data if available, not the last-modification-time.
 *
 * Return value: (see strcmp)
 */
static gint
cb_rstto_image_list_exif_date_compare_func (RsttoFile *a, RsttoFile *b)
{
    guint64 a_t = rstto_file_get_modified_time (a);
    guint64 b_t = rstto_file_get_modified_time (b);

    if (a_t < b_t)
    {
        return -1;
    }
    return 1;
}

gboolean
rstto_image_list_iter_get_sticky (
        RsttoImageListIter *iter)
{
    return iter->priv->sticky;
}

static void
cb_rstto_wrap_images_changed (
        GObject *settings,
        GParamSpec *pspec,
        gpointer user_data)
{
    GValue val_wrap_images = { 0, };

    RsttoImageList *image_list = RSTTO_IMAGE_LIST (user_data);

    g_value_init (&val_wrap_images, G_TYPE_BOOLEAN);

    g_object_get_property (
            settings,
            "wrap-images",
            &val_wrap_images);

    image_list->priv->wrap_images = g_value_get_boolean (&val_wrap_images);
}

/***************************************/
/*      TreeModelIface Functions       */
/***************************************/

static GtkTreeModelFlags
image_list_model_get_flags ( GtkTreeModel *tree_model )
{
    g_return_val_if_fail(RSTTO_IS_IMAGE_LIST(tree_model), (GtkTreeModelFlags)0);

    return (GTK_TREE_MODEL_LIST_ONLY | GTK_TREE_MODEL_ITERS_PERSIST);
}

static gint
image_list_model_get_n_columns ( GtkTreeModel *tree_model )
{
    g_return_val_if_fail(RSTTO_IS_IMAGE_LIST(tree_model), 0);

    return 2;
}

static GType
image_list_model_get_column_type (
        GtkTreeModel *tree_model,
        gint index_ )
{
    g_return_val_if_fail(RSTTO_IS_IMAGE_LIST(tree_model), G_TYPE_INVALID);

    switch (index_)
    {
        case 0: /* file */
            return RSTTO_TYPE_FILE;
        case 1:
            return GDK_TYPE_PIXBUF;
            break;
        default:
            return G_TYPE_INVALID;
            break;
    }
}

static gboolean
image_list_model_get_iter (
        GtkTreeModel *tree_model,
        GtkTreeIter *iter,
        GtkTreePath *path )
{
    gint *indices;
    gint depth;
    gint index_;
    RsttoImageList *image_list;
    RsttoFile *file = NULL;

    g_return_val_if_fail(RSTTO_IS_IMAGE_LIST(tree_model), FALSE);

    image_list = RSTTO_IMAGE_LIST (tree_model);

    indices = gtk_tree_path_get_indices(path);
    depth = gtk_tree_path_get_depth(path) - 1;

    /* only support list: depth is always 0 */
    g_return_val_if_fail(depth == 0, FALSE);

    index_ = indices[depth];

    if (index_ >= 0)
    {
        file = g_list_nth_data (image_list->priv->images, index_);
    }

    if (NULL == file)
    {
        return FALSE;
    }

    /* set the stamp, identify the iter as ours */
    iter->stamp = image_list->priv->stamp;

    iter->user_data = file;

    /* the index_ in the child list */
    iter->user_data3 = GINT_TO_POINTER(index_);
    return TRUE;
}

static GtkTreePath *
image_list_model_get_path (
        GtkTreeModel *tree_model,
        GtkTreeIter *iter )
{
    GtkTreePath *path = NULL;
    gint pos;

    g_return_val_if_fail(RSTTO_IS_IMAGE_LIST(tree_model), NULL);

    pos = GPOINTER_TO_INT (iter->user_data3);

    path = gtk_tree_path_new();
    gtk_tree_path_append_index(path, pos);

    return path;
}

static gboolean
image_list_model_iter_children (
        GtkTreeModel *tree_model,
        GtkTreeIter *iter,
        GtkTreeIter *parent )
{
    RsttoFile *file = NULL;
    RsttoImageList *image_list;

    g_return_val_if_fail(RSTTO_IS_IMAGE_LIST(tree_model), FALSE);

    /* only support lists: parent is always NULL */
    g_return_val_if_fail(parent == NULL, FALSE);

    image_list = RSTTO_IMAGE_LIST (tree_model);
    
    file = g_list_nth_data (image_list->priv->images, 0);

    if (NULL == file)
    {
        return FALSE;
    }

    iter->stamp = image_list->priv->stamp;
    iter->user_data = file;
    iter->user_data3 = GINT_TO_POINTER(0);

    return TRUE;
}

static gboolean
image_list_model_iter_has_child (
        GtkTreeModel *tree_model,
        GtkTreeIter *iter )
{
    return FALSE;
}

static gboolean
image_list_model_iter_parent (
        GtkTreeModel *tree_model,
        GtkTreeIter *iter,
        GtkTreeIter *child )
{
    return FALSE;
}

static gint
image_list_model_iter_n_children (
        GtkTreeModel *tree_model,
        GtkTreeIter *iter )
{
    return FALSE;
}

static gboolean 
image_list_model_iter_nth_child (
        GtkTreeModel *tree_model,
        GtkTreeIter *iter,
        GtkTreeIter *parent,
        gint n )
{
    return FALSE;
}

static gboolean
image_list_model_iter_next (
        GtkTreeModel *tree_model,
        GtkTreeIter *iter )
{
    RsttoImageList *image_list;
    RsttoFile *file = NULL;
    gint pos = 0;

    g_return_val_if_fail(RSTTO_IS_IMAGE_LIST(tree_model), FALSE);

    image_list = RSTTO_IMAGE_LIST (tree_model);

    file = iter->user_data;
    pos = GPOINTER_TO_INT(iter->user_data3);
    pos++;

    file = g_list_nth_data (image_list->priv->images, pos);

    if (NULL == file)
    {
        return FALSE;
    }

    iter->stamp = image_list->priv->stamp;
    iter->user_data = file;
    iter->user_data3 = GINT_TO_POINTER(pos);

    return TRUE;
}

static void 
image_list_model_get_value (
        GtkTreeModel *tree_model,
        GtkTreeIter *iter,
        gint column,
        GValue *value )
{
    RsttoFile *file = RSTTO_FILE(iter->user_data);

    switch (column)
    {
        case 0:
            g_value_init (value, RSTTO_TYPE_FILE);
            g_value_set_object (value, file);
            break;
    }
}

static void
cb_rstto_thumbnailer_ready(
        RsttoThumbnailer *thumbnailer,
        RsttoFile *file,
        gpointer user_data)
{
    RsttoImageList *image_list = RSTTO_IMAGE_LIST (user_data);
    GtkTreePath *path_ = NULL;
    GtkTreeIter iter;
    gint index_;

    index_ = g_list_index (image_list->priv->images, file);
    if (index_ >= 0)
    {
        path_ = gtk_tree_path_new();
        gtk_tree_path_append_index(path_,index_);
        gtk_tree_model_get_iter (GTK_TREE_MODEL (image_list), &iter, path_);

        gtk_tree_model_row_changed (GTK_TREE_MODEL(image_list), path_, &iter);
        gtk_tree_path_free (path_);
    }
}

gboolean
rstto_image_list_is_busy (
        RsttoImageList *list )
{
    if (list->priv->directory_loader == 0)
    {
        return FALSE;
    }

    return TRUE;
}
