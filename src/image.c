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

#ifndef RSTTO_IMAGE_BUFFER_SIZE
/* #define RSTTO_IMAGE_BUFFER_SIZE 1024 */
#define RSTTO_IMAGE_BUFFER_SIZE 131072
#endif

#define STD_IMAGE_SIZE 1024

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
    GCancellable *cancellable;

    /* File I/O data */
    /*****************/
    guchar *buffer;

    /* Image data */
    /**************/
    RsttoImageOrientation orientation;
    ExifData *exif_data;
    GdkPixbuf *thumbnail;
    gint       width;
    gint       height;

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

    g_object_ref (file);

    image->priv->file = file;
    image->priv->exif_data = NULL;
    image->priv->thumbnail = NULL;
    image->priv->orientation = RSTTO_IMAGE_ORIENT_NOT_DETERMINED;

    return image;
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
 * rstto_image_get_orientation;
 * @image:
 *
 * Returns: Image orientation
 */
RsttoImageOrientation
rstto_image_get_orientation (RsttoImage *image)
{
	if (image->priv->orientation == RSTTO_IMAGE_ORIENT_NOT_DETERMINED){
	    gchar *file_path = g_file_get_path (image->priv->file);
		ExifEntry *exif_entry = NULL;

		image->priv->exif_data = exif_data_new_from_file (file_path);

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
	}

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
