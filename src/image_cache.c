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

void
rstto_image_cache_push_image (RsttoImageCache *cache, RsttoImage *image, gboolean last)
{
    RsttoSettings *settings = rstto_settings_new();
    GValue val = {0, };

    g_value_init (&val, G_TYPE_BOOLEAN);
    g_object_get_property (G_OBJECT (settings), "enable-cache", &val);

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

    if (g_value_get_boolean (&val) == FALSE)
    {
        if (g_list_length (cache->cache_list) > 1)
        {
            RsttoImage *c_image = g_list_last (cache->cache_list)->data;
            rstto_image_unload (c_image);
            cache->cache_list = g_list_remove (cache->cache_list, c_image);
            g_object_unref (image);
        }
    }
    else
    {
        /**
         * TODO:
         * Fix the cache-size calculation
         */
        if (g_list_length (cache->cache_list) > 3)
        {
            RsttoImage *c_image = g_list_last (cache->cache_list)->data;
            rstto_image_unload (c_image);
            cache->cache_list = g_list_remove (cache->cache_list, c_image);
            g_object_unref (image);
        }
    }
    g_value_unset (&val);
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
