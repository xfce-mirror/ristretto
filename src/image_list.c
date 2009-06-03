/*
 *  Copyright (c) 2009 Stephan Arts <stephan@xfce.org>
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
#include <gtk/gtkmarshal.h>

#include <string.h>

#include <libexif/exif-data.h>

#include "image.h"
#include "image_list.h"

static void 
rstto_image_list_init(RsttoImageList *);
static void
rstto_image_list_class_init(RsttoImageListClass *);
static void
rstto_image_list_dispose(GObject *object);

static void 
rstto_image_list_iter_init(RsttoImageListIter *);
static void
rstto_image_list_iter_class_init(RsttoImageListIterClass *);
static void
rstto_image_list_iter_dispose(GObject *object);

static RsttoImageListIter * rstto_image_list_iter_new ();

static gint
cb_rstto_image_list_image_name_compare_func (RsttoImage *a, RsttoImage *b);

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
    RSTTO_IMAGE_LIST_ITER_SIGNAL_COUNT
};

struct _RsttoImageListIterPriv
{
    RsttoImageList *image_list;
    RsttoImage *image;
};

struct _RsttoImageListPriv
{
    GList *images;
    gint n_images;

    GSList *iterators;
};

static gint rstto_image_list_signals[RSTTO_IMAGE_LIST_SIGNAL_COUNT];
static gint rstto_image_list_iter_signals[RSTTO_IMAGE_LIST_ITER_SIGNAL_COUNT];

GType
rstto_image_list_get_type ()
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
    /*RsttoImageList *image_list = RSTTO_IMAGE_LIST(object);*/
}

RsttoImageList *
rstto_image_list_new ()
{
    RsttoImageList *image_list;

    image_list = g_object_new(RSTTO_TYPE_IMAGE_LIST, NULL);

    return image_list;
}

gboolean
rstto_image_list_add_file (RsttoImageList *image_list, GFile *file, GError **error)
{
    RsttoImage *image = rstto_image_new (file);
    if (image)
    {
        image_list->priv->images = g_list_insert_sorted (image_list->priv->images, image, (GCompareFunc)cb_rstto_image_list_image_name_compare_func);
        image_list->priv->n_images++;

        g_signal_emit (G_OBJECT (image_list), rstto_image_list_signals[RSTTO_IMAGE_LIST_SIGNAL_NEW_IMAGE], 0, image, NULL);
        if (image_list->priv->n_images == 1)
        {
            /** TODO: update all iterators */
            GSList *iter = image_list->priv->iterators;
            while (iter)
            {
                g_signal_emit (G_OBJECT (iter->data), rstto_image_list_iter_signals[RSTTO_IMAGE_LIST_ITER_SIGNAL_CHANGED], 0, NULL);
                iter = g_slist_next (iter);
            }
        }
        return TRUE;
    }
    return FALSE;
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
    RsttoImage *image = NULL;
    if (image_list->priv->images)
        image = image_list->priv->images->data;

    RsttoImageListIter *iter = rstto_image_list_iter_new (image_list, image);

    image_list->priv->iterators = g_slist_prepend (image_list->priv->iterators, iter);

    return iter;
}


void
rstto_image_list_remove_image (RsttoImageList *image_list, RsttoImage *image)
{
    if (g_list_find (image_list->priv->images, image))
    {
        image_list->priv->images = g_list_remove (image_list->priv->images, image);

        GSList *iter = image_list->priv->iterators;
        while (iter)
        {
            if (rstto_image_list_iter_get_image (iter->data) == image)
            {
                rstto_image_list_iter_previous (iter->data);
            }
            iter = g_slist_next (iter);
        }
        g_signal_emit (G_OBJECT (image_list), rstto_image_list_signals[RSTTO_IMAGE_LIST_SIGNAL_REMOVE_IMAGE], 0, image, NULL);
        g_object_unref (image);
    }
}

void
rstto_image_list_remove_all (RsttoImageList *image_list)
{
    g_list_foreach (image_list->priv->images, (GFunc)g_object_unref, NULL);
    g_list_free (image_list->priv->images);
    image_list->priv->images = NULL;

    GSList *iter = image_list->priv->iterators;
    while (iter)
    {
        rstto_image_list_iter_set_position (iter->data, 0);
        iter = g_slist_next (iter);
    }
    g_signal_emit (G_OBJECT (image_list), rstto_image_list_signals[RSTTO_IMAGE_LIST_SIGNAL_REMOVE_ALL], 0, NULL);
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
cb_rstto_image_list_image_name_compare_func (RsttoImage *a, RsttoImage *b)
{
    gchar *a_base = g_file_get_basename (rstto_image_get_file (a));  
    gchar *b_base = g_file_get_basename (rstto_image_get_file (b));  
    gint result = 0;

    result = g_strcasecmp (a_base, b_base);

    g_free (a_base);
    g_free (b_base);
    return result;
}

GType
rstto_image_list_iter_get_type ()
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
    if (iter->priv->image)
    {
        g_object_unref (iter->priv->image);
        iter->priv->image = NULL;
    }

    if (iter->priv->image_list)
    {
        iter->priv->image_list->priv->iterators = g_slist_remove (iter->priv->image_list->priv->iterators, iter);
        iter->priv->image_list= NULL;
    }
}

static RsttoImageListIter *
rstto_image_list_iter_new (RsttoImageList *nav, RsttoImage *image)
{
    RsttoImageListIter *iter;

    iter = g_object_new(RSTTO_TYPE_IMAGE_LIST_ITER, NULL);
    iter->priv->image = image;
    iter->priv->image_list = nav;

    return iter;
}

gboolean
rstto_image_list_iter_find_image (RsttoImageListIter *iter, RsttoImage *image)
{
    gint pos = g_list_index (iter->priv->image_list->priv->images, image);
    if (pos > -1)
    {
        if (iter->priv->image)
        {
            iter->priv->image = NULL;
        }
        iter->priv->image = image;

        g_signal_emit (G_OBJECT (iter), rstto_image_list_iter_signals[RSTTO_IMAGE_LIST_ITER_SIGNAL_CHANGED], 0, NULL);

        return TRUE;
    }
    return FALSE;
}

gint
rstto_image_list_iter_get_position (RsttoImageListIter *iter)
{
    if (iter->priv->image == NULL)
    {
        return -1;
    }
    return g_list_index (iter->priv->image_list->priv->images, iter->priv->image);
}

RsttoImage *
rstto_image_list_iter_get_image (RsttoImageListIter *iter)
{
    return RSTTO_IMAGE (iter->priv->image);
}


void
rstto_image_list_iter_set_position (RsttoImageListIter *iter, gint pos)
{
    if (iter->priv->image)
    {
        iter->priv->image = NULL;
    }

    iter->priv->image = g_list_nth_data (iter->priv->image_list->priv->images, pos); 

    g_signal_emit (G_OBJECT (iter), rstto_image_list_iter_signals[RSTTO_IMAGE_LIST_ITER_SIGNAL_CHANGED], 0, NULL);
}

void
rstto_image_list_iter_next (RsttoImageListIter *iter)
{
    GList *position = NULL;
    if (iter->priv->image)
    {
        position = g_list_find (iter->priv->image_list->priv->images, iter->priv->image);
        iter->priv->image = NULL;
    }

    position = g_list_next (position);
    if (position)
        iter->priv->image = position->data; 
    else
    {
        position = g_list_first (iter->priv->image_list->priv->images);
        if (position)
            iter->priv->image = position->data; 
        else
            iter->priv->image = NULL;
    }

    g_signal_emit (G_OBJECT (iter), rstto_image_list_iter_signals[RSTTO_IMAGE_LIST_ITER_SIGNAL_CHANGED], 0, NULL);
}

void
rstto_image_list_iter_previous (RsttoImageListIter *iter)
{
    GList *position = NULL;
    if (iter->priv->image)
    {
        position = g_list_find (iter->priv->image_list->priv->images, iter->priv->image);
        iter->priv->image = NULL;
    }

    position = g_list_previous (position);
    if (position)
        iter->priv->image = position->data; 
    else
    {
        position = g_list_last (iter->priv->image_list->priv->images);
        if (position)
            iter->priv->image = position->data; 
        else
            iter->priv->image = NULL;
    }

    g_signal_emit (G_OBJECT (iter), rstto_image_list_iter_signals[RSTTO_IMAGE_LIST_ITER_SIGNAL_CHANGED], 0, NULL);
}

RsttoImageListIter *
rstto_image_list_iter_clone (RsttoImageListIter *iter)
{
    RsttoImageListIter *new_iter = rstto_image_list_iter_new (iter->priv->image_list, iter->priv->image);

    return new_iter;
}

GCompareFunc
rstto_image_list_get_compare_func (RsttoImageList *image_list)
{
    return cb_rstto_image_list_image_name_compare_func;
}
