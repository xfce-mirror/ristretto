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
 *
 *  Sorting-algorithm taken from the thunar filemanager.
 */

#include <config.h>

#include <gtk/gtk.h>
#include <gtk/gtkmarshal.h>

#include <stdlib.h>
#include <string.h>

#include <libexif/exif-data.h>

#include "util.h"
#include "file.h"
#include "image_list.h"
#include "settings.h"

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

static RsttoImageListIter * rstto_image_list_iter_new ();

static gint
cb_rstto_image_list_image_name_compare_func (RsttoFile *a, RsttoFile *b);
static gint
cb_rstto_image_list_exif_date_compare_func (RsttoFile *a, RsttoFile *b);

static GObjectClass *parent_class = NULL;
static GObjectClass *iter_parent_class = NULL;

enum
{
    RSTTO_IMAGE_LIST_SIGNAL_NEW_IMAGE = 0,
    RSTTO_IMAGE_LIST_SIGNAL_REMOVE_IMAGE,
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
    RsttoFile      *file;
    
    /* This is set if the iter-position is chosen by the user */
    gboolean        sticky; 
};

struct _RsttoImageListPriv
{
    GFileMonitor  *dir_monitor;
    RsttoSettings *settings;
    GtkFileFilter *filter;

    GList        *image_monitors;
    GList        *images;
    gint          n_images;

    GSList       *iterators;
    GCompareFunc  cb_rstto_image_list_compare_func;

    gboolean      wrap_images;
};

static gint rstto_image_list_signals[RSTTO_IMAGE_LIST_SIGNAL_COUNT];
static gint rstto_image_list_iter_signals[RSTTO_IMAGE_LIST_ITER_SIGNAL_COUNT];

GType
rstto_image_list_get_type (void)
{
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
    }
    return rstto_image_list_type;
}

static void
rstto_image_list_init(RsttoImageList *image_list)
{

    image_list->priv = g_new0 (RsttoImageListPriv, 1);
    image_list->priv->settings = rstto_settings_new ();
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

}

static void
rstto_image_list_class_init(RsttoImageListClass *nav_class)
{
    GObjectClass *object_class = G_OBJECT_CLASS(nav_class);

    parent_class = g_type_class_peek_parent(nav_class);

    object_class->dispose = rstto_image_list_dispose;

    rstto_image_list_signals[RSTTO_IMAGE_LIST_SIGNAL_NEW_IMAGE] = g_signal_new("new-image",
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
        RsttoFile *file,
        GError **error )
{
    GtkFileFilterInfo filter_info;
    GList *image_iter = g_list_find (image_list->priv->images, file);
    GSList *iter = image_list->priv->iterators;
    GFileMonitor *monitor = NULL;

    g_return_val_if_fail ( NULL != file , FALSE);
    g_return_val_if_fail ( RSTTO_IS_FILE (file) , FALSE);

    if (!image_iter)
    {
        if (file)
        {
            filter_info.contains =  GTK_FILE_FILTER_MIME_TYPE | GTK_FILE_FILTER_URI;
            filter_info.uri = rstto_file_get_uri (file);
            filter_info.mime_type = rstto_file_get_content_type (file);

            if ( TRUE == gtk_file_filter_filter (image_list->priv->filter, &filter_info))
            {
                g_object_ref (G_OBJECT (file));

                image_list->priv->images = g_list_insert_sorted (image_list->priv->images, file, rstto_image_list_get_compare_func (image_list));

                image_list->priv->n_images++;

                if (image_list->priv->dir_monitor == NULL)
                {
                    monitor = g_file_monitor_file (
                            rstto_file_get_file (file),
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

                g_signal_emit (G_OBJECT (image_list), rstto_image_list_signals[RSTTO_IMAGE_LIST_SIGNAL_NEW_IMAGE], 0, file, NULL);
                /** TODO: update all iterators */
                while (iter)
                {
                    if (FALSE == RSTTO_IMAGE_LIST_ITER(iter->data)->priv->sticky)
                    {
                        rstto_image_list_iter_find_file (iter->data, file);
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

    g_signal_emit (G_OBJECT (image_list), rstto_image_list_signals[RSTTO_IMAGE_LIST_SIGNAL_NEW_IMAGE], 0, image_iter->data, NULL);

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
    RsttoFile *file = NULL;
    RsttoImageListIter *iter = NULL;
    if (image_list->priv->images)
        file = image_list->priv->images->data;

    iter = rstto_image_list_iter_new (image_list, file);

    image_list->priv->iterators = g_slist_prepend (image_list->priv->iterators, iter);

    return iter;
}


void
rstto_image_list_remove_file (RsttoImageList *image_list, RsttoFile *file)
{
    GSList *iter = NULL;
    RsttoFile *afile = NULL;
    gint index = g_list_index (image_list->priv->images, file);

    if (index != -1)
    {

        iter = image_list->priv->iterators;
        while (iter)
        {
            if (rstto_file_equal(rstto_image_list_iter_get_file (iter->data), file))
            {
                if (rstto_image_list_iter_get_position (iter->data) == rstto_image_list_get_n_images (image_list)-1)
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
                if (rstto_file_equal(rstto_image_list_iter_get_file (iter->data), file))
                {
                    ((RsttoImageListIter *)(iter->data))->priv->file = NULL;
                    g_signal_emit (G_OBJECT (iter->data), rstto_image_list_iter_signals[RSTTO_IMAGE_LIST_ITER_SIGNAL_CHANGED], 0, NULL);
                }
            }
            iter = g_slist_next (iter);
        }

        image_list->priv->images = g_list_remove (image_list->priv->images, file);
        iter = image_list->priv->iterators;
        while (iter)
        {
            afile = rstto_image_list_iter_get_file(iter->data);
            if (NULL != afile)
            {
                if (rstto_file_equal(afile, file))
                {
                    iter_next (iter->data, FALSE);
                }
            }
            iter = g_slist_next (iter);
        }

        g_signal_emit (G_OBJECT (image_list), rstto_image_list_signals[RSTTO_IMAGE_LIST_SIGNAL_REMOVE_IMAGE], 0, file, NULL);
        g_object_unref(file);
    }
}

static void
rstto_image_list_remove_all (RsttoImageList *image_list)
{
    GSList *iter = NULL;

    if (image_list->priv->image_monitors)
    {
        g_list_foreach (image_list->priv->image_monitors, (GFunc)g_object_unref, NULL);
        g_list_free (image_list->priv->image_monitors);
        image_list->priv->image_monitors = NULL;
    }

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
    GFileInfo *file_info;
    const gchar *filename;
    const gchar *content_type;
    GFile *child_file;

    /* Source code block */
    rstto_image_list_remove_all (image_list);

    /* Allow all images to be removed by providing NULL to dir */
    if ( NULL != dir )
    {
        file_enumerator = g_file_enumerate_children (dir, "standard::*", 0, NULL, NULL);

        if (NULL != file_enumerator)
        {
            for(file_info = g_file_enumerator_next_file (file_enumerator, NULL, NULL);
                NULL != file_info;
                file_info = g_file_enumerator_next_file (file_enumerator, NULL, NULL))
            {
                filename = g_file_info_get_name (file_info);
                content_type  = g_file_info_get_content_type (file_info);
                child_file = g_file_get_child (dir, filename);
                if (strncmp (content_type, "image/", 6) == 0)
                {
                    rstto_image_list_add_file (image_list, rstto_file_new (child_file), NULL);
                }
            }
            g_object_unref (file_enumerator);
            file_enumerator = NULL;
        }
    }

    rstto_image_list_monitor_dir ( image_list, dir );

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
    if (iter->priv->file)
    {
        iter->priv->file = NULL;
    }

    if (iter->priv->image_list)
    {
        iter->priv->image_list->priv->iterators = g_slist_remove (iter->priv->image_list->priv->iterators, iter);
        iter->priv->image_list= NULL;
    }
}

static RsttoImageListIter *
rstto_image_list_iter_new (RsttoImageList *nav, RsttoFile *file)
{
    RsttoImageListIter *iter;

    iter = g_object_new(RSTTO_TYPE_IMAGE_LIST_ITER, NULL);
    iter->priv->file = file;
    iter->priv->image_list = nav;

    return iter;
}

gboolean
rstto_image_list_iter_find_file (RsttoImageListIter *iter, RsttoFile *file)
{
    gint pos = g_list_index (iter->priv->image_list->priv->images, file);
    if (pos > -1)
    {
        g_signal_emit (G_OBJECT (iter), rstto_image_list_iter_signals[RSTTO_IMAGE_LIST_ITER_SIGNAL_PREPARE_CHANGE], 0, NULL);

        if (iter->priv->file)
        {
            iter->priv->file = NULL;
        }
        iter->priv->file = file;
        iter->priv->sticky = TRUE;

        g_signal_emit (G_OBJECT (iter), rstto_image_list_iter_signals[RSTTO_IMAGE_LIST_ITER_SIGNAL_CHANGED], 0, NULL);

        return TRUE;
    }
    return FALSE;
}

gint
rstto_image_list_iter_get_position (RsttoImageListIter *iter)
{
    if ( NULL == iter->priv->file )
    {
        return -1;
    }
    return g_list_index (iter->priv->image_list->priv->images, iter->priv->file);
}

RsttoFile *
rstto_image_list_iter_get_file (RsttoImageListIter *iter)
{
    return iter->priv->file;
}

static void
iter_set_position (
        RsttoImageListIter *iter,
        gint pos,
        gboolean sticky )
{
    g_signal_emit (G_OBJECT (iter), rstto_image_list_iter_signals[RSTTO_IMAGE_LIST_ITER_SIGNAL_PREPARE_CHANGE], 0, NULL);

    if (iter->priv->file)
    {
        iter->priv->file = NULL;
    }

    if (pos >= 0)
    {
        iter->priv->file = g_list_nth_data (iter->priv->image_list->priv->images, pos); 
    }

    g_signal_emit (G_OBJECT (iter), rstto_image_list_iter_signals[RSTTO_IMAGE_LIST_ITER_SIGNAL_CHANGED], 0, NULL);
}

void
rstto_image_list_iter_set_position (RsttoImageListIter *iter, gint pos)
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
    RsttoFile *file = iter->priv->file;
    gboolean ret_val = FALSE;

    g_signal_emit (G_OBJECT (iter), rstto_image_list_iter_signals[RSTTO_IMAGE_LIST_ITER_SIGNAL_PREPARE_CHANGE], 0, NULL);

    if (iter->priv->file)
    {
        position = g_list_find (iter->priv->image_list->priv->images, iter->priv->file);
        iter->priv->file = NULL;
    }

    iter->priv->sticky = sticky;

    position = g_list_next (position);
    if (position)
    {
        iter->priv->file = position->data; 

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
            position = g_list_last (iter->priv->image_list->priv->images);

        if (position)
            iter->priv->file = position->data; 
        else
            iter->priv->file = NULL;
    }

    if (file != iter->priv->file)
    {
        g_signal_emit (G_OBJECT (iter), rstto_image_list_iter_signals[RSTTO_IMAGE_LIST_ITER_SIGNAL_CHANGED], 0, NULL);
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
    RsttoFile *file = iter->priv->file;
    gboolean ret_val = FALSE;

    g_signal_emit (G_OBJECT (iter), rstto_image_list_iter_signals[RSTTO_IMAGE_LIST_ITER_SIGNAL_PREPARE_CHANGE], 0, NULL);

    if (iter->priv->file)
    {
        position = g_list_find (iter->priv->image_list->priv->images, iter->priv->file);
        iter->priv->file = NULL;
    }

    iter->priv->sticky = sticky;

    position = g_list_previous (position);
    if (position)
    {
        iter->priv->file = position->data; 
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
            iter->priv->file = position->data; 
        }
        else
        {
            iter->priv->file = NULL;
        }
    }

    if (file != iter->priv->file)
    {
        g_signal_emit (G_OBJECT (iter), rstto_image_list_iter_signals[RSTTO_IMAGE_LIST_ITER_SIGNAL_CHANGED], 0, NULL);
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
    RsttoImageListIter *new_iter = rstto_image_list_iter_new (iter->priv->image_list, iter->priv->file);
    rstto_image_list_iter_set_position (new_iter, rstto_image_list_iter_get_position(iter));

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
        g_signal_emit (G_OBJECT (iter->data), rstto_image_list_iter_signals[RSTTO_IMAGE_LIST_ITER_SIGNAL_CHANGED], 0, NULL);
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
    const gchar *a_base = rstto_file_get_display_name (a);
    const gchar *b_base = rstto_file_get_display_name (b);
    guint  ac;
    guint  bc;
    const gchar *ap = a_base;
    const gchar *bp = b_base;

    gint result = 0;
    guint a_num = 0;
    guint b_num = 0;

    /* try simple (fast) ASCII comparison first */
    for (;; ++ap, ++bp)
    {
        /* check if the characters differ or we have a non-ASCII char
         */
        ac = *((const guchar *) ap);
        bc = *((const guchar *) bp);
        if (ac != bc || ac == 0 || ac > 127)
            break;
    }

    /* fallback to Unicode comparison */
    if (G_UNLIKELY (ac > 127 || bc > 127))
    {
        for (;; ap = g_utf8_next_char (ap), bp = g_utf8_next_char (bp))
        {
            /* check if characters differ or end of string */
            ac = g_utf8_get_char (ap);
            bc = g_utf8_get_char (bp);
            if (ac != bc || ac == 0)
                break;
        }
    }

    /* If both strings are equal, we're done */
    if (ac == bc)
    {
        return 0;
    }
    else
    {
        if (G_UNLIKELY (g_ascii_isdigit (ac) || g_ascii_isdigit (bc)))
        {
            /* if both strings differ in a digit, we use a smarter comparison
             * to get sorting 'file1', 'file5', 'file10' done the right way.
             */
            if (g_ascii_isdigit (ac) && g_ascii_isdigit (bc))
            {
                a_num = strtoul (ap, NULL, 10); 
                b_num = strtoul (bp, NULL, 10); 

                if (a_num < b_num)
                    result = -1;
                if (a_num > b_num)
                    result = 1;
            }

            if (ap > a_base &&
                bp > b_base &&
                g_ascii_isdigit (*(ap -1)) &&
                g_ascii_isdigit (*(bp -1)) )
            {
                a_num = strtoul (ap-1, NULL, 10); 
                b_num = strtoul (bp-1, NULL, 10); 

                if (a_num < b_num)
                    result = -1;
                if (a_num > b_num)
                    result = 1;
            }
        }
    }

    if (result == 0)
    {
        if (ac > bc)
            result = 1;
        if (ac < bc)
            result = -1;
    }


    return result;
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
