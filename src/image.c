/*
 *  Copyright (c) Stephan Arts 2009-2010 <stephan@xfce.org>
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

#include <string.h>

#include "image.h"
#include "image_cache.h"

#ifndef RSTTO_IMAGE_BUFFER_SIZE
/* #define RSTTO_IMAGE_BUFFER_SIZE 1024 */
#define RSTTO_IMAGE_BUFFER_SIZE 131072
#endif

enum
{
    RSTTO_IMAGE_SIGNAL_UPDATED= 0,
    RSTTO_IMAGE_SIGNAL_PREPARED,
    RSTTO_IMAGE_SIGNAL_COUNT    
};

static void
rstto_image_init (GObject *);
static void
rstto_image_class_init (GObjectClass *);

static void
rstto_image_dispose (GObject *object);

static void
cb_rstto_image_area_prepared (GdkPixbufLoader *loader, RsttoImage *image);
static void
cb_rstto_image_size_prepared (GdkPixbufLoader *loader, gint width, gint height, RsttoImage *image);
static void
cb_rstto_image_closed (GdkPixbufLoader *loader, RsttoImage *image);
static gboolean
cb_rstto_image_update(RsttoImage *image);

static void
cb_rstto_image_read_file_ready (GObject *source_object, GAsyncResult *result, gpointer user_data);
static void
cb_rstto_image_read_input_stream_ready (GObject *source_object, GAsyncResult *result, gpointer user_data);

static GObjectClass *parent_class = NULL;

static gint rstto_image_signals[RSTTO_IMAGE_SIGNAL_COUNT];

GType
rstto_image_get_type (void)
{
    static GType rstto_image_type = 0;

    if (!rstto_image_type)
    {
        static const GTypeInfo rstto_image_info = 
        {
            sizeof (RsttoImageClass),
            (GBaseInitFunc) NULL,
            (GBaseFinalizeFunc) NULL,
            (GClassInitFunc) rstto_image_class_init,
            (GClassFinalizeFunc) NULL,
            NULL,
            sizeof (RsttoImage),
            0,
            (GInstanceInitFunc) rstto_image_init,
            NULL
        };

        rstto_image_type = g_type_register_static (G_TYPE_OBJECT, "RsttoImage", &rstto_image_info, 0);
    }
    return rstto_image_type;
}

struct _RsttoImagePriv
{
    /* File data */
    /*************/
    GFile *file;
    GFileMonitor *monitor;
    GCancellable *cancellable;

    /* File I/O data */
    /*****************/
    guchar *buffer;

    /* Image data */
    /**************/
    RsttoImageOrientation orientation;
    GdkPixbufLoader *loader;
    ExifData *exif_data;
    GdkPixbuf *thumbnail;
    GdkPixbuf *pixbuf;
    gint       width;
    gint       height;
    guint      max_size;

    /* Animation data for animated images (like .gif/.mng) */
    /*******************************************************/
    GdkPixbufAnimation  *animation;
    GdkPixbufAnimationIter *iter;
    gint    animation_timeout_id;
};


static void
rstto_image_init (GObject *object)
{
    RsttoImage *image = RSTTO_IMAGE (object);

    image->priv = g_new0 (RsttoImagePriv, 1);

    /* Initialize buffer for image-loading */
    image->priv->buffer = g_new0 (guchar, RSTTO_IMAGE_BUFFER_SIZE);
    image->priv->cancellable = g_cancellable_new();

}


static void
rstto_image_class_init (GObjectClass *object_class)
{
    RsttoImageClass *image_class = RSTTO_IMAGE_CLASS (object_class);

    parent_class = g_type_class_peek_parent (image_class);

    object_class->dispose = rstto_image_dispose;

    /* The 'updated' signal is emitted when the contents
     * of the root pixbuf is changed. This can happen when:
     *    1) The image-loading is complete, this can be caused by:
     *       a) The initial load of the image in memory
     *       b) The image is updated on disk, the monitor has 
     *          triggered a reload, and the loading is complete.
     *       c) A reload is issued with a different scale
     *    2) The next frame in an animated image is ready
     */
    rstto_image_signals[RSTTO_IMAGE_SIGNAL_UPDATED] = g_signal_new("updated",
            G_TYPE_FROM_CLASS (image_class),
            G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
            0,
            NULL,
            NULL,
            g_cclosure_marshal_VOID__VOID,
            G_TYPE_NONE,
            0,
            NULL);

    /* The 'prepared' signal is emitted when the size of the image is known
     * and the initial pixbuf is loaded. - Usually this is still missing the data.
     */
    rstto_image_signals[RSTTO_IMAGE_SIGNAL_PREPARED] = g_signal_new("prepared",
            G_TYPE_FROM_CLASS (image_class),
            G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
            0,
            NULL,
            NULL,
            g_cclosure_marshal_VOID__VOID,
            G_TYPE_NONE,
            0,
            NULL);

}

/**
 * rstto_image_dispose:
 * @object:
 *
 */
static void
rstto_image_dispose (GObject *object)
{
    RsttoImage *image = RSTTO_IMAGE (object);
    RsttoImageCache *cache;

    if(image->priv->cancellable)
    {
        g_cancellable_cancel (image->priv->cancellable);
        g_object_unref (image->priv->cancellable);
        image->priv->cancellable = NULL;
    }

    if(image->priv->thumbnail)
    {
        g_object_unref (image->priv->thumbnail);
        image->priv->thumbnail = NULL;
    }

    if (image->priv->animation_timeout_id)
    {
        g_source_remove (image->priv->animation_timeout_id);
        image->priv->animation_timeout_id = 0;
    }

    if (image->priv->loader)
    {
        g_signal_handlers_disconnect_by_func (image->priv->loader , cb_rstto_image_area_prepared, image);
        gdk_pixbuf_loader_close (image->priv->loader, NULL);
        image->priv->loader = NULL;
    }

    if (image->priv->animation)
    {
        g_object_unref (image->priv->animation);
        image->priv->animation = NULL;
    }

    if (image->priv->pixbuf)
    {
        g_object_unref (image->priv->pixbuf);
        image->priv->pixbuf = NULL;
        cache = rstto_image_cache_new ();
        rstto_image_cache_pop_image (cache, image);
    }

    if (image->priv->buffer)
    {
        g_free (image->priv->buffer);
        image->priv->buffer = NULL;
    }

    if (image->priv->exif_data)
    {
        exif_data_free (image->priv->exif_data);
        image->priv->exif_data = NULL;
    }
}




/**
 * rstto_image_new:
 * @file : The file which contains the image.
 *
 */
RsttoImage *
rstto_image_new (GFile *file)
{
    RsttoImage *image = g_object_new (RSTTO_TYPE_IMAGE, NULL);
    gchar *file_path = g_file_get_path (file);
    ExifEntry *exif_entry = NULL;

    g_object_ref (file);

    image->priv->file = file;
    image->priv->exif_data = exif_data_new_from_file (file_path);
    image->priv->thumbnail = NULL;
    image->priv->pixbuf = NULL;

    if (image->priv->exif_data) {
        exif_entry = exif_data_get_entry (image->priv->exif_data, EXIF_TAG_ORIENTATION);
    }
    /* Check if the image has exif-data available */
    if (exif_entry && exif_entry->data != NULL)
    {
        /* Get the image-orientation from EXIF data */
        image->priv->orientation = exif_get_short (exif_entry->data, exif_data_get_byte_order (exif_entry->parent->parent));
        if (image->priv->orientation == 0)
            /* Default orientation */
            image->priv->orientation = RSTTO_IMAGE_ORIENT_NONE;
    }
    else
    {
        /* Default orientation */
        image->priv->orientation = RSTTO_IMAGE_ORIENT_NONE;
    }

    return image;
}



/**
 * rstto_image_load:
 * @image       : The image to load from disk.
 * @empty_cache : if it should empty the cache (eg, perform a reload)
 * @error       : return location for errors or %NULL.
 *
 * If the image is not yet loaded in memory, load the image. 
 * A reload can be forced with @empty_cache set to %TRUE.
 * On failure, returns %FALSE. And @error will be set to 
 * point to a #GError describing the cause of the failure.
 * Warning: this function initializes a load, it is an a-sync call and
 * does not guarantee that the image will be loaded afterwards.
 * the 'image-loaded' signal will indicate that this operation 
 * has finished.
 *
 * Return value: TRUE on success.
 */
gboolean
rstto_image_load (RsttoImage *image, gboolean empty_cache, guint max_size, gboolean preload, GError **error)
{
    RsttoImageCache *cache;

    g_return_val_if_fail (image != NULL, FALSE);

    cache = rstto_image_cache_new ();

    g_cancellable_reset (image->priv->cancellable);

    /* maximum size */
    /* TODO: replace by 'scale' */
    image->priv->max_size = max_size;

    /* Check if a GIOChannel is present, if so... the load is already in progress */
    /* The image needs to be loaded if:
     *   a) The image is already loaded but there is
     *      a reload required because the cache needs
     *      to be cleared.
     *   b) The image is not yet loaded.
     */
    if ((image->priv->loader == NULL) && ((empty_cache == TRUE ) || image->priv->pixbuf == NULL))
    {
        /* If the image has been loaded, free it */
        if (image->priv->pixbuf)
        {
            g_object_unref (image->priv->pixbuf);
            image->priv->pixbuf = NULL;
        }
        
        /* FIXME: should we check if the loader already exists? */
        image->priv->loader = gdk_pixbuf_loader_new();

        /* connect the signal-handlers */
        g_signal_connect(image->priv->loader, "area-prepared", G_CALLBACK(cb_rstto_image_area_prepared), image);
        g_signal_connect(image->priv->loader, "size-prepared", G_CALLBACK(cb_rstto_image_size_prepared), image);
        /*g_signal_connect(image->priv->loader, "area-updated", G_CALLBACK(cb_rstto_image_area_updated), image);*/
        g_signal_connect(image->priv->loader, "closed", G_CALLBACK(cb_rstto_image_closed), image);

        g_object_ref (image);
	    g_file_read_async (image->priv->file, 0, NULL, (GAsyncReadyCallback)cb_rstto_image_read_file_ready, image);
    }
    else
    {
        g_signal_emit(G_OBJECT(image), rstto_image_signals[RSTTO_IMAGE_SIGNAL_UPDATED], 0, image, NULL);
    }
    rstto_image_cache_push_image (cache, image, preload);
    return TRUE;
}


/**
 * rstto_image_unload:
 * @image       : The image to unload from memory.
 *
 * This function will unload the image-pixbuf from memory.
 */
void
rstto_image_unload (RsttoImage *image)
{
    g_return_if_fail (image != NULL);

    g_cancellable_cancel (image->priv->cancellable);

    if (image->priv->loader)
    {
        gdk_pixbuf_loader_close (image->priv->loader, NULL);
        image->priv->loader = NULL;
    }

    if (image->priv->pixbuf)
    {
        g_object_unref (image->priv->pixbuf);
        image->priv->pixbuf = NULL;
    }

    if (image->priv->thumbnail)
    {
        g_object_unref (image->priv->thumbnail);
        image->priv->thumbnail = NULL;
    }

    if (image->priv->animation_timeout_id)
    {
        g_source_remove (image->priv->animation_timeout_id);
        image->priv->animation_timeout_id = 0;
    }

    if (image->priv->animation)
    {
        g_object_unref (image->priv->animation);
        image->priv->animation = NULL;
    }

    if (image->priv->iter)
    {
        g_object_unref (image->priv->iter);
        image->priv->iter = NULL;
    }

}


/**
 * rstto_image_get_file:
 * @image       : The image to get the GFile object from.
 *
 * Return value: A GFile object representing the image file.
 */
GFile *
rstto_image_get_file (RsttoImage *image)
{
    g_return_val_if_fail (image != NULL, NULL);
    g_return_val_if_fail (image->priv != NULL, NULL);
    g_return_val_if_fail (image->priv->file != NULL, NULL);

    return image->priv->file;
}

/**
 * rstto_image_get_width:
 * @image:
 *
 * Return value: width of the image
 */
gint
rstto_image_get_width (RsttoImage *image)
{
    g_return_val_if_fail (image != NULL, 0);
    g_return_val_if_fail (image->priv != NULL, 0);

    return image->priv->width;
}

/**
 * rstto_image_get_height:
 * @image:
 *
 * Return value: height of the image
 */
gint
rstto_image_get_height (RsttoImage *image)
{
    g_return_val_if_fail (image != NULL, 0);
    g_return_val_if_fail (image->priv != NULL, 0);

    return image->priv->height;
}


/**
 * rstto_image_get_thumbnail:
 * @image       : 
 *
 * return value: a gdkpixbuf * referencing a pixbuf pointing to the thumbnail.
 */
GdkPixbuf *
rstto_image_get_thumbnail (RsttoImage *image)
{
    gchar *file_uri;
    gchar *file_uri_checksum;
    gchar *thumbnail_filename;
    gchar *thumbnail_path;

    g_return_val_if_fail (image != NULL, NULL);
    g_return_val_if_fail (image->priv != NULL, NULL);

    file_uri = g_file_get_uri (image->priv->file);
    file_uri_checksum = g_compute_checksum_for_string (G_CHECKSUM_MD5, file_uri, strlen (file_uri));
    thumbnail_filename = g_strconcat (file_uri_checksum, ".png", NULL);
    thumbnail_path = g_build_path ("/", g_get_home_dir(), ".thumbnails", "normal", thumbnail_filename, NULL);

    if (image->priv->thumbnail == NULL)
    {
        image->priv->thumbnail = gdk_pixbuf_new_from_file_at_scale (thumbnail_path, 128, 128, TRUE, NULL);
    }
    else
    {
        /* What else ?! */
    }

    g_free (file_uri);
    g_free (file_uri_checksum);
    g_free (thumbnail_filename);
    g_free (thumbnail_path);


    return image->priv->thumbnail;
}


/**
 * rstto_image_get_pixbuf:
 * @image       : 
 *
 * return value: a gdkpixbuf * referencing a pixbuf pointing to the image.
 */
GdkPixbuf *
rstto_image_get_pixbuf (RsttoImage *image)
{
    g_return_val_if_fail (image != NULL, NULL);
    g_return_val_if_fail (image->priv != NULL, NULL);


    return image->priv->pixbuf;
}

/**
 * rstto_image_get_size;
 * @image: Image object
 *
 * Returns the image-size in bytes (approx)
 *
 * Returns: Image-size (bytes) 
 */
guint64
rstto_image_get_size (RsttoImage *image)
{
    GdkPixbuf *pixbuf = rstto_image_get_pixbuf (image);
    if (pixbuf)
    {
        gint rowstride = gdk_pixbuf_get_rowstride (pixbuf);
        gint height = gdk_pixbuf_get_height (pixbuf);
        /* HACK HACK HACK HACK */
        /* multiplied by 2 since it is unclear why the nr of bytes
         * in memory is twice what is calculated here, based on the dimensions
         */
        //return rowstride * height * 2;
        return (guint64)rowstride * (guint64)height;
    }
    return 0;
}

/**
 * rstto_image_get_orientation;
 * @image:
 *
 * Returns: Image orientation
 */
RsttoImageOrientation
rstto_image_get_orientation (RsttoImage *image)
{
    return image->priv->orientation;
}

/**
 * rstto_image_set_orientation;
 * @image:
 * @orientation:
 *
 */
void
rstto_image_set_orientation (RsttoImage *image, RsttoImageOrientation orientation)
{
    image->priv->orientation = orientation;
    g_signal_emit (G_OBJECT(image), rstto_image_signals[RSTTO_IMAGE_SIGNAL_UPDATED], 0, image, NULL);
}

/**
 * PRIVATE CALLBACKS 
 */

/**
 * cb_rstto_image_read_file_ready:
 * @source_object:
 * @result:
 * @user_data:
 * 
 *
 */
static void
cb_rstto_image_read_file_ready (GObject *source_object, GAsyncResult *result, gpointer user_data)
{
    GFile *file = G_FILE (source_object);
    RsttoImage *image = RSTTO_IMAGE (user_data);
    GFileInputStream *file_input_stream = g_file_read_finish (file, result, NULL);
    
    if (g_cancellable_is_cancelled (image->priv->cancellable))
    {
        g_object_unref (image);
        return;
    }


    g_input_stream_read_async (G_INPUT_STREAM (file_input_stream),
                               image->priv->buffer,
                               RSTTO_IMAGE_BUFFER_SIZE,
                               G_PRIORITY_DEFAULT,
                               NULL,
                               (GAsyncReadyCallback) cb_rstto_image_read_input_stream_ready,
                               image);
}

/**
 * cb_rstto_image_read_input_stream_ready:
 * @source_object:
 * @result:
 * @user_data:
 * 
 *
 */
static void
cb_rstto_image_read_input_stream_ready (GObject *source_object, GAsyncResult *result, gpointer user_data)
{
    RsttoImage *image = RSTTO_IMAGE (user_data);
    gssize read_bytes = g_input_stream_read_finish (G_INPUT_STREAM (source_object), result, NULL);
    GError *error = NULL;

    if (g_cancellable_is_cancelled (image->priv->cancellable))
    {
        g_object_unref (image);
        return;
    }

    if (image->priv->loader == NULL)
        return;


    if (read_bytes > 0)
    {
        if(gdk_pixbuf_loader_write (image->priv->loader, (const guchar *)image->priv->buffer, read_bytes, &error) == FALSE)
        {
            g_input_stream_close (G_INPUT_STREAM (source_object), NULL, NULL);
            g_object_unref (image);
        }
        else
        {
            g_input_stream_read_async (G_INPUT_STREAM (source_object),
                                       image->priv->buffer,
                                       RSTTO_IMAGE_BUFFER_SIZE,
                                       G_PRIORITY_DEFAULT,
                                       NULL,
                                       (GAsyncReadyCallback) cb_rstto_image_read_input_stream_ready,
                                       image);
        }
    }
    else
    if (read_bytes == 0)
    {
        if (read_bytes == 0)
        {
            /* OK */
            g_input_stream_close (G_INPUT_STREAM (source_object), NULL, NULL);
            gdk_pixbuf_loader_close (image->priv->loader, NULL);
            g_object_unref (image);
        }
        else
        {
            /* I/O ERROR */
            g_input_stream_close (G_INPUT_STREAM (source_object), NULL, NULL);
            gdk_pixbuf_loader_close (image->priv->loader, NULL);
            g_object_unref (image);
        }
    }
}

/**
 * cb_rstto_image_size_prepared:
 * @loader:
 * @width;
 * @height;
 * @image:
 *
 */
static void
cb_rstto_image_size_prepared (GdkPixbufLoader *loader, gint width, gint height, RsttoImage *image)
{
    image->priv->width = width;
    image->priv->height = height;

    if (image->priv->max_size > 0)
    {
        gdouble ratio = (gdouble)(image->priv->max_size)/(gdouble)(width * height);
        if (ratio < 1)
    	    gdk_pixbuf_loader_set_size (loader, width*ratio, height*ratio);
    }

    g_signal_emit(G_OBJECT(image), rstto_image_signals[RSTTO_IMAGE_SIGNAL_PREPARED], 0, image, NULL);
}

/**
 * cb_rstto_image_area_prepared:
 * @loader:
 * @image:
 *
 */
static void
cb_rstto_image_area_prepared (GdkPixbufLoader *loader, RsttoImage *image)
{
    gint timeout = 0;
    image->priv->animation = gdk_pixbuf_loader_get_animation (loader);
    image->priv->iter = gdk_pixbuf_animation_get_iter (image->priv->animation, NULL);
    if (image->priv->pixbuf)
    {
        g_object_unref(image->priv->pixbuf);
        image->priv->pixbuf = NULL;
    }

    g_object_ref (image->priv->animation);

    timeout = gdk_pixbuf_animation_iter_get_delay_time (image->priv->iter);

    if (timeout != -1)
    {
        /* fix borked stuff */
        if (timeout == 0)
        {
            g_warning("timeout == 0: defaulting to 40ms");
            timeout = 40;
        }

        image->priv->animation_timeout_id = g_timeout_add(timeout, (GSourceFunc)cb_rstto_image_update, image);
    }   
    else
    {
        image->priv->pixbuf = gdk_pixbuf_animation_iter_get_pixbuf (image->priv->iter);
        g_object_ref (image->priv->pixbuf);
    }
}

/**
 * cb_rstto_image_closed:
 * @loader:
 * @image:
 *
 * Image loading is complete
 *
 */
static void
cb_rstto_image_closed (GdkPixbufLoader *loader, RsttoImage *image)
{
    g_return_if_fail (image != NULL);
    g_return_if_fail (RSTTO_IS_IMAGE (image));
    g_return_if_fail (loader == image->priv->loader);

    g_object_unref (image->priv->loader);
    image->priv->loader = NULL;

   
    if (image->priv->pixbuf != NULL)
    {
        g_signal_emit(G_OBJECT(image), rstto_image_signals[RSTTO_IMAGE_SIGNAL_UPDATED], 0, image, NULL);
    }
}

/**
 * cb_rstto_image_update:
 * @image:
 * 
 * Used for advancing to the next frame in an animated image,
 * TODO: rename - to make it's purpose more clear.
 *
 * Return value:
 */
static gboolean
cb_rstto_image_update(RsttoImage *image)
{
    gint timeout = 0;

    if (image->priv->iter)
    {
        if(gdk_pixbuf_animation_iter_advance (image->priv->iter, NULL))
        {
            /* Cleanup old image */
            if (image->priv->pixbuf)
            {
                g_object_unref (image->priv->pixbuf);
                image->priv->pixbuf = NULL;
            }

            image->priv->pixbuf = gdk_pixbuf_copy (gdk_pixbuf_animation_iter_get_pixbuf (image->priv->iter));
        }

        timeout = gdk_pixbuf_animation_iter_get_delay_time (image->priv->iter);

        if (timeout != -1)
        {
            if (timeout == 0)
            {
                g_warning("timeout == 0: defaulting to 40ms");
                timeout = 40;
            }
            image->priv->animation_timeout_id = g_timeout_add(timeout, (GSourceFunc)cb_rstto_image_update, image);
        }
        g_signal_emit (G_OBJECT(image), rstto_image_signals[RSTTO_IMAGE_SIGNAL_UPDATED], 0, image, NULL);

        return FALSE;
    }
    return TRUE;
}

