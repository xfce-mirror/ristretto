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

#include <glib.h>
#include <gio/gio.h>
#include <gdk/gdk.h>

#include <libexif/exif-data.h>

#include "image.h"
#include "image_cache.h"
#include "settings.h"

static void
rstto_image_cache_init (GObject *);
static void
rstto_image_cache_class_init (GObjectClass *);

static RsttoImageCache *rstto_global_image_cache = NULL;

struct _RsttoImageCache
{
    GObject parent;
    GList *cache_list;
};

struct _RsttoImageCacheClass
{
    GObjectClass parent_class;
};


GType
rstto_image_cache_get_type ()
{
    static GType rstto_image_cache_type = 0;

    if (!rstto_image_cache_type)
    {
        static const GTypeInfo rstto_image_cache_info = 
        {
            sizeof (RsttoImageCacheClass),
            (GBaseInitFunc) NULL,
            (GBaseFinalizeFunc) NULL,
            (GClassInitFunc) rstto_image_cache_class_init,
            (GClassFinalizeFunc) NULL,
            NULL,
            sizeof (RsttoImageCache),
            0,
            (GInstanceInitFunc) rstto_image_cache_init,
            NULL
        };

        rstto_image_cache_type = g_type_register_static (G_TYPE_OBJECT, "RsttoImageCache", &rstto_image_cache_info, 0);
    }
    return rstto_image_cache_type;
}


static void
rstto_image_cache_init (GObject *object)
{

}

static void
rstto_image_cache_class_init (GObjectClass *object_class)
{

}

gboolean
rstto_image_cache_push_image (RsttoImageCache *cache, RsttoImage *image, gboolean last)
{
    gboolean retval = FALSE;
    RsttoSettings *settings = rstto_settings_new();
    GValue val = {0, }, val_cache_size = {0, };
    guint size = 0;
    guint cache_size = 0;
    RsttoImage *c_image;
    GList *iter = NULL;

    g_value_init (&val, G_TYPE_BOOLEAN);
    g_value_init (&val_cache_size, G_TYPE_UINT);
    g_object_get_property (G_OBJECT (settings), "enable-cache", &val);
    g_object_get_property (G_OBJECT (settings), "cache-size", &val_cache_size);

    cache_size = g_value_get_uint(&val_cache_size)*1000000;

    if (cache->cache_list)
    {
        cache->cache_list = g_list_remove_all (cache->cache_list, image);
    }

    g_object_ref (image);

    if (last)
    {
        cache->cache_list = g_list_append (cache->cache_list, image);
    }
    else
    {
        cache->cache_list = g_list_prepend (cache->cache_list, image);
    }

    /**
     * Check if we are keeping a cache
     */
    if (g_value_get_boolean (&val) == FALSE)
    {
        while (g_list_length (cache->cache_list) > 1)
        {
            c_image = g_list_last (cache->cache_list)->data;
            rstto_image_unload (c_image);
            cache->cache_list = g_list_remove (cache->cache_list, c_image);
            retval = TRUE;
        }
    }
    else
    {
        /* Calculate the cache-size, if it exceeds the defined maximum,
         * unload the the images that exceed that.
         */
        for (iter = cache->cache_list->next; iter != NULL; iter = g_list_next (iter))
        {
            c_image = iter->data;
            if (size > cache_size)
            {
                rstto_image_unload (c_image);
                cache->cache_list = g_list_remove (cache->cache_list, c_image);
                iter = g_list_previous(iter);
                retval = TRUE;
            } 
            else
            {
                size += rstto_image_get_size (c_image);
                if (rstto_image_get_size (c_image) == 0)
                {
                    rstto_image_unload (c_image);
                    cache->cache_list = g_list_remove (cache->cache_list, c_image);
                    iter = g_list_previous(iter);
                }
            }
        }
    }
    g_object_unref (settings);
    g_value_unset (&val);
    return retval;
}

/**
 * rstto_image_cache_new:
 *
 * Singleton
 *
 * Return value: 
 */
RsttoImageCache *
rstto_image_cache_new ()
{
    if (rstto_global_image_cache == NULL)
    {
        rstto_global_image_cache = g_object_new (RSTTO_TYPE_IMAGE_CACHE, NULL);
    }

    return rstto_global_image_cache;
}
