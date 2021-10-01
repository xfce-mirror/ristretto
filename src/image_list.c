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

#include <string.h>

#include "util.h"
#include "image_list.h"
#include "thumbnailer.h"
#include "settings.h"
#include "main_window.h"


#define FILE_BLOCK_SIZE 10000

enum
{
    RSTTO_IMAGE_LIST_SIGNAL_REMOVE_IMAGE = 0,
    RSTTO_IMAGE_LIST_SIGNAL_REMOVE_ALL,
    RSTTO_IMAGE_LIST_SIGNAL_SORTED,
    RSTTO_IMAGE_LIST_SIGNAL_COUNT
};

enum
{
    RSTTO_IMAGE_LIST_ITER_SIGNAL_CHANGED = 0,
    RSTTO_IMAGE_LIST_ITER_SIGNAL_PREPARE_CHANGE,
    RSTTO_IMAGE_LIST_ITER_SIGNAL_COUNT
};

static gint rstto_image_list_signals[RSTTO_IMAGE_LIST_SIGNAL_COUNT];
static gint rstto_image_list_iter_signals[RSTTO_IMAGE_LIST_ITER_SIGNAL_COUNT];



static void
rstto_image_list_tree_model_init (GtkTreeModelIface *iface);
static void
rstto_image_list_tree_sortable_init (GtkTreeSortableIface *iface);


static void
rstto_image_list_finalize (GObject *object);

static void
cb_file_monitor_changed (GFileMonitor *monitor,
                         GFile *file,
                         GFile *other_file,
                         GFileMonitorEvent event_type,
                         gpointer user_data);


static void
rstto_image_list_iter_finalize (GObject *object);

static RsttoImageListIter *
rstto_image_list_iter_new (RsttoImageList *nav,
                           RsttoFile *r_file);

static void
cb_rstto_wrap_images_changed (GObject *settings,
                              GParamSpec *pspec,
                              gpointer user_data);
static void
cb_rstto_thumbnailer_ready (RsttoThumbnailer *thumbnailer,
                            RsttoFile *file,
                            gpointer user_data);
static void
rstto_image_list_monitor_dir (RsttoImageList *image_list,
                              GFile *dir);
static void
rstto_image_list_remove_all (RsttoImageList *image_list);
static gboolean
rstto_image_list_set_directory_idle (gpointer data);
static gboolean
iter_next (RsttoImageListIter *iter,
           gboolean sticky);
static gboolean
iter_previous (RsttoImageListIter *iter,
               gboolean sticky);
static void
iter_set_position (RsttoImageListIter *iter,
                   gint pos,
                   gboolean sticky);


/***************************************/
/*  Begin TreeModelIface Functions     */
/***************************************/
static GtkTreeModelFlags
image_list_model_get_flags (GtkTreeModel *tree_model);
static gint
image_list_model_get_n_columns (GtkTreeModel *tree_model);
static GType
image_list_model_get_column_type (GtkTreeModel *tree_model,
                                  gint index_);
static gboolean
image_list_model_get_iter (GtkTreeModel *tree_model,
                           GtkTreeIter *iter,
                           GtkTreePath *path);
static GtkTreePath *
image_list_model_get_path (GtkTreeModel *tree_model,
                           GtkTreeIter *iter);
static void
image_list_model_get_value (GtkTreeModel *tree_model,
                            GtkTreeIter *iter,
                            gint column,
                            GValue *value);
static gboolean
image_list_model_iter_children (GtkTreeModel *tree_model,
                                GtkTreeIter *iter,
                                GtkTreeIter *parent);
static gboolean
image_list_model_iter_has_child (GtkTreeModel *tree_model,
                                 GtkTreeIter *iter);
static gboolean
image_list_model_iter_parent (GtkTreeModel *tree_model,
                              GtkTreeIter *iter,
                              GtkTreeIter *child);
static gint
image_list_model_iter_n_children (GtkTreeModel *tree_model,
                                  GtkTreeIter *iter);
static gboolean
image_list_model_iter_nth_child (GtkTreeModel *tree_model,
                                 GtkTreeIter *iter,
                                 GtkTreeIter *parent,
                                 gint n);
static gboolean
image_list_model_iter_next (GtkTreeModel *tree_model,
                            GtkTreeIter *iter);
/***************************************/
/*  End TreeModelIface Functions       */
/***************************************/


static gint
cb_rstto_image_list_image_name_compare_func (gconstpointer a,
                                             gconstpointer b);
static gint
cb_rstto_image_list_image_type_compare_func (gconstpointer a,
                                             gconstpointer b);
static gint
cb_rstto_image_list_exif_date_compare_func (gconstpointer a,
                                            gconstpointer b);



struct _RsttoImageListIterPrivate
{
    RsttoImageList *image_list;
    RsttoFile      *r_file;

    /* This is set if the iter-position is chosen by the user */
    gboolean        sticky;
};

struct _RsttoImageListPrivate
{
    gint           stamp;
    GFileMonitor  *dir_monitor;
    gboolean       is_busy;
    RsttoSettings *settings;
    RsttoThumbnailer *thumbnailer;

    GList        *image_monitors;
    GList        *images;
    gint          n_images;

    GSList       *iterators;
    GCompareFunc  cb_rstto_image_list_compare_func;

    gboolean      wrap_images;
};



G_DEFINE_TYPE_WITH_CODE (RsttoImageList, rstto_image_list, G_TYPE_OBJECT,
                         G_ADD_PRIVATE (RsttoImageList)
                         G_IMPLEMENT_INTERFACE (GTK_TYPE_TREE_MODEL,
                                                rstto_image_list_tree_model_init)
                         G_IMPLEMENT_INTERFACE (GTK_TYPE_TREE_SORTABLE,
                                                rstto_image_list_tree_sortable_init))

G_DEFINE_TYPE_WITH_PRIVATE (RsttoImageListIter, rstto_image_list_iter, G_TYPE_OBJECT)



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
rstto_image_list_tree_sortable_init (GtkTreeSortableIface *iface)
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
rstto_image_list_init (RsttoImageList *image_list)
{
    image_list->priv = rstto_image_list_get_instance_private (image_list);
    image_list->priv->stamp = g_random_int ();
    image_list->priv->is_busy = FALSE;
    image_list->priv->settings = rstto_settings_new ();
    image_list->priv->thumbnailer = rstto_thumbnailer_new ();

    image_list->priv->cb_rstto_image_list_compare_func = cb_rstto_image_list_image_name_compare_func;

    image_list->priv->wrap_images = rstto_settings_get_boolean_property (
            image_list->priv->settings,
            "wrap-images");

    g_signal_connect (image_list->priv->settings, "notify::wrap-images",
                      G_CALLBACK (cb_rstto_wrap_images_changed), image_list);

    g_signal_connect (image_list->priv->thumbnailer, "ready",
                      G_CALLBACK (cb_rstto_thumbnailer_ready), image_list);

}

static void
rstto_image_list_class_init (RsttoImageListClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = rstto_image_list_finalize;

    rstto_image_list_signals[RSTTO_IMAGE_LIST_SIGNAL_REMOVE_IMAGE] = g_signal_new ("remove-image",
            G_TYPE_FROM_CLASS (klass),
            G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
            0,
            NULL,
            NULL,
            g_cclosure_marshal_VOID__OBJECT,
            G_TYPE_NONE,
            1,
            G_TYPE_OBJECT,
            NULL);

    rstto_image_list_signals[RSTTO_IMAGE_LIST_SIGNAL_REMOVE_ALL] = g_signal_new ("remove-all",
            G_TYPE_FROM_CLASS (klass),
            G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
            0,
            NULL,
            NULL,
            g_cclosure_marshal_VOID__VOID,
            G_TYPE_NONE,
            0,
            NULL);

    rstto_image_list_signals[RSTTO_IMAGE_LIST_SIGNAL_SORTED] = g_signal_new ("sorted",
            G_TYPE_FROM_CLASS (klass),
            G_SIGNAL_RUN_LAST,
            0,
            NULL,
            NULL,
            g_cclosure_marshal_VOID__VOID,
            G_TYPE_NONE,
            0,
            NULL);
}

static void
rstto_image_list_finalize (GObject *object)
{
    RsttoImageList *image_list = RSTTO_IMAGE_LIST (object);

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

        if (image_list->priv->image_monitors)
        {
            g_list_free_full (image_list->priv->image_monitors, g_object_unref);
            image_list->priv->image_monitors = NULL;
        }

        if (image_list->priv->images)
        {
            g_list_free_full (image_list->priv->images, g_object_unref);
            image_list->priv->images = NULL;
        }
    }

    G_OBJECT_CLASS (rstto_image_list_parent_class)->finalize (object);
}

RsttoImageList *
rstto_image_list_new (void)
{
    RsttoImageList *image_list;

    image_list = g_object_new (RSTTO_TYPE_IMAGE_LIST, NULL);

    return image_list;
}

gboolean
rstto_image_list_add_file (
        RsttoImageList *image_list,
        RsttoFile *r_file,
        GError **error)
{
    GList *image_iter = g_list_find (image_list->priv->images, r_file);
    GSList *iter = image_list->priv->iterators;
    gint i = 0;
    GtkTreePath *path = NULL;
    GtkTreeIter t_iter;
    GFileMonitor *monitor = NULL;

    g_return_val_if_fail (NULL != r_file, FALSE);
    g_return_val_if_fail (RSTTO_IS_FILE (r_file), FALSE);

    if (!image_iter)
    {
        if (r_file)
        {
            if (rstto_file_is_valid (r_file))
            {
                g_object_ref (r_file);

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
                    g_signal_connect (monitor, "changed",
                                      G_CALLBACK (cb_file_monitor_changed), image_list);
                    image_list->priv->image_monitors = g_list_prepend (
                            image_list->priv->image_monitors,
                            monitor);
                }
                i = g_list_index (image_list->priv->images, r_file);

                path = gtk_tree_path_new ();
                gtk_tree_path_append_index (path, i);
                t_iter.stamp = image_list->priv->stamp;
                t_iter.user_data = r_file;
                t_iter.user_data3 = GINT_TO_POINTER (i);

                gtk_tree_model_row_inserted (
                        GTK_TREE_MODEL (image_list),
                        path,
                        &t_iter);

                gtk_tree_path_free (path);

                /** TODO: update all iterators */
                while (iter)
                {
                    if (! RSTTO_IMAGE_LIST_ITER (iter->data)->priv->sticky)
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
            if (rstto_file_equal (rstto_image_list_iter_get_file (iter->data), r_file))
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
                        r_file))
                {

                    image_list->priv->images = g_list_remove (image_list->priv->images, r_file);
                    RSTTO_IMAGE_LIST_ITER (iter->data)->priv->r_file = NULL;
                    g_signal_emit (iter->data,
                            rstto_image_list_iter_signals[RSTTO_IMAGE_LIST_ITER_SIGNAL_CHANGED],
                            0, NULL);
                }
            }
            iter = g_slist_next (iter);
        }

        image_list->priv->images = g_list_remove (image_list->priv->images, r_file);

        path_ = gtk_tree_path_new ();
        gtk_tree_path_append_index (path_, index_);

        gtk_tree_model_row_deleted (GTK_TREE_MODEL (image_list), path_);

        iter = image_list->priv->iterators;
        while (iter)
        {
            r_file_a = rstto_image_list_iter_get_file (iter->data);
            if (NULL != r_file_a)
            {
                if (rstto_file_equal (r_file_a, r_file))
                {
                    iter_next (iter->data, FALSE);
                }
            }
            iter = g_slist_next (iter);
        }

        g_signal_emit (image_list,
                       rstto_image_list_signals[RSTTO_IMAGE_LIST_SIGNAL_REMOVE_IMAGE],
                       0, r_file, NULL);

        g_object_unref (r_file);
        gtk_tree_path_free (path_);
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
        path_ = gtk_tree_path_new ();
        gtk_tree_path_append_index (path_, i);

        gtk_tree_model_row_deleted (GTK_TREE_MODEL (image_list), path_);
        gtk_tree_path_free (path_);

        image_iter = g_list_next (image_iter);
    }

    g_list_free_full (image_list->priv->image_monitors, g_object_unref);
    image_list->priv->image_monitors = NULL;

    g_list_free_full (image_list->priv->images, g_object_unref);
    image_list->priv->images = NULL;

    iter = image_list->priv->iterators;
    while (iter)
    {
        iter_set_position (iter->data, -1, FALSE);
        iter = g_slist_next (iter);
    }
    g_signal_emit (image_list,
                   rstto_image_list_signals[RSTTO_IMAGE_LIST_SIGNAL_REMOVE_ALL],
                   0, NULL);
}

static void
rstto_image_list_set_directory_finish (GObject *source_object,
                                       GAsyncResult *res,
                                       gpointer user_data)
{
    RsttoImageList *image_list = user_data;
    GFileEnumerator *file_enum = G_FILE_ENUMERATOR (source_object);
    GList *info_list, *li;
    GFile *file, *loaded_file;
    GError *error = NULL;
    const gchar *content_type;

    if (rstto_main_window_get_app_exited ())
    {
        g_object_unref (file_enum);
        return;
    }

    info_list = g_file_enumerator_next_files_finish (file_enum, res, &error);
    if (info_list == NULL)
    {
        /* we're done, inform the others */
        image_list->priv->is_busy = FALSE;
        rstto_image_list_set_compare_func (image_list,
                                           image_list->priv->cb_rstto_image_list_compare_func);

        if (error != NULL)
        {
            /* TODO: show error dialog */
            g_error_free (error);
        }

        return;
    }

    loaded_file = rstto_file_get_file (rstto_object_get_data (file_enum, "loaded-file"));
    for (li = info_list; li != NULL; li = li->next)
    {
        content_type  = g_file_info_get_content_type (li->data);
        if (content_type != NULL && g_str_has_prefix (content_type, "image/"))
        {
            file = g_file_enumerator_get_child (file_enum, li->data);
            if (loaded_file != NULL && g_file_equal (file, loaded_file))
            {
                loaded_file = NULL;
                rstto_object_set_data (file_enum, "loaded-file", NULL);
                continue;
            }

            /* no filtering here, it will be done when requesting thumbnails */
            image_list->priv->images = g_list_prepend (image_list->priv->images,
                                                       rstto_file_new (file));
            g_object_unref (file);
        }
    }

    /* schedule next file block, if any */
    if (g_list_length (info_list) == FILE_BLOCK_SIZE)
        g_idle_add (rstto_image_list_set_directory_idle, file_enum);
    else
    {
        /* we're done, inform the others */
        image_list->priv->is_busy = FALSE;
        rstto_image_list_set_compare_func (image_list,
                                           image_list->priv->cb_rstto_image_list_compare_func);

        g_object_unref (file_enum);
    }

    g_list_free_full (info_list, g_object_unref);
}

static gboolean
rstto_image_list_set_directory_idle (gpointer data)
{
    g_file_enumerator_next_files_async (data, FILE_BLOCK_SIZE, G_PRIORITY_DEFAULT,
                                        NULL, rstto_image_list_set_directory_finish,
                                        rstto_object_get_data (data, "image-list"));

    return FALSE;
}

gboolean
rstto_image_list_set_directory (RsttoImageList *image_list,
                                GFile *dir,
                                RsttoFile *file,
                                GError **error)
{
    GFileEnumerator *file_enum;

    rstto_image_list_remove_all (image_list);
    rstto_image_list_monitor_dir (image_list, dir);

    /* allow all images to be removed by providing NULL to dir */
    if (dir == NULL)
        return TRUE;

    if (file != NULL && ! rstto_image_list_add_file (image_list, file, error))
        return FALSE;

    /* this is transfer full but this ref will be released in
     * rstto_image_list_set_directory_finish() when everything is done */
    file_enum = g_file_enumerate_children (dir, "standard::content-type",
                                           G_FILE_QUERY_INFO_NONE, NULL, error);
    if (file_enum == NULL)
        return FALSE;

    image_list->priv->is_busy = TRUE;
    rstto_object_set_data (file_enum, "loaded-file", file);
    rstto_object_set_data (file_enum, "image-list", image_list);
    g_idle_add (rstto_image_list_set_directory_idle, rstto_util_source_autoremove (file_enum));

    return TRUE;
}

static void
rstto_image_list_monitor_dir (
        RsttoImageList *image_list,
        GFile *dir)
{
    GFileMonitor *monitor = NULL;

    if (NULL != image_list->priv->dir_monitor)
    {
        g_object_unref (image_list->priv->dir_monitor);
        image_list->priv->dir_monitor = NULL;
    }

    /* Allow a monitor to be removed by providing NULL to dir */
    if (NULL != dir)
    {
        monitor = g_file_monitor_directory (
                dir,
                G_FILE_MONITOR_NONE,
                NULL,
                NULL);

        if (monitor != NULL)
        {
            g_signal_connect (monitor, "changed",
                              G_CALLBACK (cb_file_monitor_changed), image_list);
        }
    }

    if (image_list->priv->image_monitors)
    {
        g_list_free_full (image_list->priv->image_monitors, g_object_unref);
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
        gpointer           user_data)
{
    RsttoImageList *image_list = user_data;
    RsttoFile *r_file;
    GList *list;

    static RsttoFile *s_r_file = NULL;

    /* see if the file is already open */
    for (list = image_list->priv->images; list != NULL; list = list->next)
        if (g_file_equal (file, list->data))
        {
            r_file = g_object_ref (list->data);
            break;
        }

    if (list == NULL)
        r_file = rstto_file_new (file);

    switch (event_type)
    {
        case G_FILE_MONITOR_EVENT_DELETED:
            rstto_image_list_remove_file (image_list, r_file);
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
            /* should never happen here but just in case, to prevent any memory leak */
            if (s_r_file != NULL)
                g_object_unref (s_r_file);

            /* wait for DONE_HINT to add the image, so that we can get its mime type */
            s_r_file = g_object_ref (r_file);
            break;
        case G_FILE_MONITOR_EVENT_CHANGES_DONE_HINT:
            if (s_r_file != NULL)
            {
                rstto_image_list_add_file (image_list, s_r_file, NULL);
                g_object_unref (s_r_file);
                s_r_file = NULL;
            }
            break;
        case G_FILE_MONITOR_EVENT_CHANGED:
            rstto_file_changed (r_file);
            break;
        default:
            break;
    }

    if (NULL != r_file)
    {
        g_object_unref (r_file);
    }
}



static void
rstto_image_list_iter_init (RsttoImageListIter *iter)
{
    iter->priv = rstto_image_list_iter_get_instance_private (iter);
}

static void
rstto_image_list_iter_class_init (RsttoImageListIterClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = rstto_image_list_iter_finalize;

    rstto_image_list_iter_signals[RSTTO_IMAGE_LIST_ITER_SIGNAL_PREPARE_CHANGE] = g_signal_new ("prepare-change",
            G_TYPE_FROM_CLASS (klass),
            G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
            0,
            NULL,
            NULL,
            g_cclosure_marshal_VOID__VOID,
            G_TYPE_NONE,
            0,
            NULL);

    rstto_image_list_iter_signals[RSTTO_IMAGE_LIST_ITER_SIGNAL_CHANGED] = g_signal_new ("changed",
            G_TYPE_FROM_CLASS (klass),
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
rstto_image_list_iter_finalize (GObject *object)
{
    RsttoImageListIter *iter = RSTTO_IMAGE_LIST_ITER (object);

    if (iter->priv->r_file)
    {
        iter->priv->r_file = NULL;
    }

    if (iter->priv->image_list)
    {
        iter->priv->image_list->priv->iterators = g_slist_remove (iter->priv->image_list->priv->iterators, iter);
        iter->priv->image_list = NULL;
    }

    G_OBJECT_CLASS (rstto_image_list_iter_parent_class)->finalize (object);
}

static RsttoImageListIter *
rstto_image_list_iter_new (
        RsttoImageList *nav,
        RsttoFile *r_file)
{
    RsttoImageListIter *iter;

    iter = g_object_new (RSTTO_TYPE_IMAGE_LIST_ITER, NULL);
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
        g_signal_emit (iter,
                       rstto_image_list_iter_signals[RSTTO_IMAGE_LIST_ITER_SIGNAL_PREPARE_CHANGE],
                       0, NULL);

        if (iter->priv->r_file)
        {
            iter->priv->r_file = NULL;
        }
        iter->priv->r_file = r_file;
        iter->priv->sticky = TRUE;

        g_signal_emit (iter,
                       rstto_image_list_iter_signals[RSTTO_IMAGE_LIST_ITER_SIGNAL_CHANGED],
                       0, NULL);

        return TRUE;
    }
    return FALSE;
}

gint
rstto_image_list_iter_get_position (RsttoImageListIter *iter)
{
    if (NULL == iter->priv->r_file)
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
        gboolean sticky)
{
    g_signal_emit (iter,
                   rstto_image_list_iter_signals[RSTTO_IMAGE_LIST_ITER_SIGNAL_PREPARE_CHANGE],
                   0, NULL);

    if (iter->priv->r_file)
    {
        iter->priv->r_file = NULL;
    }

    if (pos >= 0)
    {
        iter->priv->r_file = g_list_nth_data (iter->priv->image_list->priv->images, pos);
    }

    g_signal_emit (iter,
                   rstto_image_list_iter_signals[RSTTO_IMAGE_LIST_ITER_SIGNAL_CHANGED],
                   0, NULL);
}

void
rstto_image_list_iter_set_position (
        RsttoImageListIter *iter,
        gint pos)
{
    iter_set_position (iter, pos, TRUE);
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

    g_signal_emit (iter,
                   rstto_image_list_iter_signals[RSTTO_IMAGE_LIST_ITER_SIGNAL_PREPARE_CHANGE],
                   0, NULL);

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

        if (image_list->priv->wrap_images)
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
        g_signal_emit (iter,
                       rstto_image_list_iter_signals[RSTTO_IMAGE_LIST_ITER_SIGNAL_CHANGED],
                       0, NULL);
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

    g_signal_emit (iter,
                   rstto_image_list_iter_signals[RSTTO_IMAGE_LIST_ITER_SIGNAL_PREPARE_CHANGE],
                   0, NULL);

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
        if (image_list->priv->wrap_images)
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
        g_signal_emit (iter,
                       rstto_image_list_iter_signals[RSTTO_IMAGE_LIST_ITER_SIGNAL_CHANGED],
                       0, NULL);
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
            rstto_image_list_iter_get_position (iter));

    return new_iter;
}

GCompareFunc
rstto_image_list_get_compare_func (RsttoImageList *image_list)
{
    return image_list->priv->cb_rstto_image_list_compare_func;
}

void
rstto_image_list_set_compare_func (RsttoImageList *image_list, GCompareFunc func)
{
    RsttoFile *files[g_slist_length (image_list->priv->iterators)];
    GSList *iter;
    gint n;

    image_list->priv->cb_rstto_image_list_compare_func = func;

    /* store iter files before sorting */
    for (iter = image_list->priv->iterators, n = 0; iter != NULL; iter = iter->next, n++)
        files[n] = rstto_image_list_iter_get_file (iter->data);

    image_list->priv->images = g_list_sort (image_list->priv->images, func);

    /* reposition iters on their file */
    for (iter = image_list->priv->iterators, n = 0; iter != NULL; iter = iter->next, n++)
        rstto_image_list_iter_find_file (iter->data, files[n]);

    g_signal_emit (image_list, rstto_image_list_signals[RSTTO_IMAGE_LIST_SIGNAL_SORTED], 0, NULL);
}

/***********************/
/*  Compare Functions  */
/***********************/

void
rstto_image_list_set_sort_by_name (RsttoImageList *image_list)
{
    rstto_image_list_set_compare_func (image_list, cb_rstto_image_list_image_name_compare_func);
}

void
rstto_image_list_set_sort_by_type (RsttoImageList *image_list)
{
    rstto_image_list_set_compare_func (image_list, cb_rstto_image_list_image_type_compare_func);
}

void
rstto_image_list_set_sort_by_date (RsttoImageList *image_list)
{
    rstto_image_list_set_compare_func (image_list, cb_rstto_image_list_exif_date_compare_func);
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
cb_rstto_image_list_image_name_compare_func (gconstpointer a,
                                             gconstpointer b)
{
    const gchar *a_collate_key = rstto_file_get_collate_key ((RsttoFile *) a);
    const gchar *b_collate_key = rstto_file_get_collate_key ((RsttoFile *) b);

    return g_strcmp0 (a_collate_key, b_collate_key);
}

/**
 * cb_rstto_image_list_image_type_compare_func:
 * @a:
 * @b:
 *
 *
 * Return value: (see strcmp)
 */
static gint
cb_rstto_image_list_image_type_compare_func (gconstpointer a,
                                             gconstpointer b)
{
    const gchar *a_content_type = rstto_file_get_content_type ((RsttoFile *) a);
    const gchar *b_content_type = rstto_file_get_content_type ((RsttoFile *) b);

    return g_strcmp0 (a_content_type, b_content_type);
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
cb_rstto_image_list_exif_date_compare_func (gconstpointer a,
                                            gconstpointer b)
{
    guint64 a_t = rstto_file_get_modified_time ((RsttoFile *) a);
    guint64 b_t = rstto_file_get_modified_time ((RsttoFile *) b);

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
    RsttoImageList *image_list = user_data;

    g_object_get (settings, "wrap-images", &(image_list->priv->wrap_images), NULL);
}

/***************************************/
/*      TreeModelIface Functions       */
/***************************************/

static GtkTreeModelFlags
image_list_model_get_flags (GtkTreeModel *tree_model)
{
    g_return_val_if_fail (RSTTO_IS_IMAGE_LIST (tree_model), 0);

    return (GTK_TREE_MODEL_LIST_ONLY | GTK_TREE_MODEL_ITERS_PERSIST);
}

static gint
image_list_model_get_n_columns (GtkTreeModel *tree_model)
{
    g_return_val_if_fail (RSTTO_IS_IMAGE_LIST (tree_model), 0);

    return 2;
}

static GType
image_list_model_get_column_type (
        GtkTreeModel *tree_model,
        gint index_)
{
    g_return_val_if_fail (RSTTO_IS_IMAGE_LIST (tree_model), G_TYPE_INVALID);

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
        GtkTreePath *path)
{
    gint *indices;
    gint depth;
    gint index_;
    RsttoImageList *image_list;
    RsttoFile *file = NULL;

    g_return_val_if_fail (RSTTO_IS_IMAGE_LIST (tree_model), FALSE);

    image_list = RSTTO_IMAGE_LIST (tree_model);

    indices = gtk_tree_path_get_indices (path);
    depth = gtk_tree_path_get_depth (path) - 1;

    /* only support list: depth is always 0 */
    g_return_val_if_fail (depth == 0, FALSE);

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
    iter->user_data3 = GINT_TO_POINTER (index_);
    return TRUE;
}

static GtkTreePath *
image_list_model_get_path (
        GtkTreeModel *tree_model,
        GtkTreeIter *iter)
{
    GtkTreePath *path = NULL;
    gint pos;

    g_return_val_if_fail (RSTTO_IS_IMAGE_LIST (tree_model), NULL);

    pos = GPOINTER_TO_INT (iter->user_data3);

    path = gtk_tree_path_new ();
    gtk_tree_path_append_index (path, pos);

    return path;
}

static gboolean
image_list_model_iter_children (
        GtkTreeModel *tree_model,
        GtkTreeIter *iter,
        GtkTreeIter *parent)
{
    RsttoFile *file = NULL;
    RsttoImageList *image_list;

    g_return_val_if_fail (RSTTO_IS_IMAGE_LIST (tree_model), FALSE);

    /* only support lists: parent is always NULL */
    g_return_val_if_fail (parent == NULL, FALSE);

    image_list = RSTTO_IMAGE_LIST (tree_model);

    file = g_list_nth_data (image_list->priv->images, 0);

    if (NULL == file)
    {
        return FALSE;
    }

    iter->stamp = image_list->priv->stamp;
    iter->user_data = file;
    iter->user_data3 = GINT_TO_POINTER (0);

    return TRUE;
}

static gboolean
image_list_model_iter_has_child (
        GtkTreeModel *tree_model,
        GtkTreeIter *iter)
{
    return FALSE;
}

static gboolean
image_list_model_iter_parent (
        GtkTreeModel *tree_model,
        GtkTreeIter *iter,
        GtkTreeIter *child)
{
    return FALSE;
}

static gint
image_list_model_iter_n_children (
        GtkTreeModel *tree_model,
        GtkTreeIter *iter)
{
    return rstto_image_list_get_n_images (RSTTO_IMAGE_LIST (tree_model));
}

static gboolean
image_list_model_iter_nth_child (
        GtkTreeModel *tree_model,
        GtkTreeIter *iter,
        GtkTreeIter *parent,
        gint n)
{
    return FALSE;
}

static gboolean
image_list_model_iter_next (
        GtkTreeModel *tree_model,
        GtkTreeIter *iter)
{
    RsttoImageList *image_list;
    RsttoFile *file = NULL;
    gint pos = 0;

    g_return_val_if_fail (RSTTO_IS_IMAGE_LIST (tree_model), FALSE);

    image_list = RSTTO_IMAGE_LIST (tree_model);

    pos = GPOINTER_TO_INT (iter->user_data3);
    pos++;

    file = g_list_nth_data (image_list->priv->images, pos);

    if (NULL == file)
    {
        return FALSE;
    }

    iter->stamp = image_list->priv->stamp;
    iter->user_data = file;
    iter->user_data3 = GINT_TO_POINTER (pos);

    return TRUE;
}

static void
image_list_model_get_value (
        GtkTreeModel *tree_model,
        GtkTreeIter *iter,
        gint column,
        GValue *value)
{
    RsttoFile *file = iter->user_data;

    switch (column)
    {
        case 0:
            g_value_init (value, RSTTO_TYPE_FILE);
            g_value_set_object (value, file);
            break;
    }
}

static void
cb_rstto_thumbnailer_ready (
        RsttoThumbnailer *thumbnailer,
        RsttoFile *file,
        gpointer user_data)
{
    RsttoImageList *image_list = user_data;
    GtkTreePath *path_ = NULL;
    GtkTreeIter iter;
    gint index_;

    index_ = g_list_index (image_list->priv->images, file);
    if (index_ >= 0)
    {
        path_ = gtk_tree_path_new ();
        gtk_tree_path_append_index (path_, index_);
        gtk_tree_model_get_iter (GTK_TREE_MODEL (image_list), &iter, path_);

        gtk_tree_model_row_changed (GTK_TREE_MODEL (image_list), path_, &iter);
        gtk_tree_path_free (path_);
    }
}

gboolean
rstto_image_list_is_busy (RsttoImageList *list)
{
    return list->priv->is_busy;
}
