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
 */

#include <config.h>

#include <glib.h>
#include <gio/gio.h>
#include <gtk/gtk.h>

#include <libexif/exif-data.h>

#include <libxfce4util/libxfce4util.h>

#include "util.h"
#include "file.h"
#include "thumbnailer.h"

static guint rstto_thumbnail_size[] =
{
    THUMBNAIL_SIZE_VERY_SMALL_SIZE,
    THUMBNAIL_SIZE_SMALLER_SIZE,
    THUMBNAIL_SIZE_SMALL_SIZE,
    THUMBNAIL_SIZE_NORMAL_SIZE,
    THUMBNAIL_SIZE_LARGE_SIZE,
    THUMBNAIL_SIZE_LARGER_SIZE,
    THUMBNAIL_SIZE_VERY_LARGE_SIZE
};

enum
{
    RSTTO_FILE_SIGNAL_CHANGED = 0,
    RSTTO_FILE_SIGNAL_COUNT
};

static gint
rstto_file_signals[RSTTO_FILE_SIGNAL_COUNT];

static void
rstto_file_init (GObject *);
static void
rstto_file_class_init (GObjectClass *);

static void
rstto_file_dispose (GObject *object);
static void
rstto_file_finalize (GObject *object);

static void
rstto_file_set_property (
        GObject      *object,
        guint         property_id,
        const GValue *value,
        GParamSpec   *pspec );
static void
rstto_file_get_property (
        GObject    *object,
        guint       property_id,
        GValue     *value,
        GParamSpec *pspec );

static GObjectClass *parent_class = NULL;

static GList *open_files = NULL;

enum
{
    PROP_0,
};

GType
rstto_file_get_type (void)
{
    static GType rstto_file_type = 0;

    if (!rstto_file_type)
    {
        static const GTypeInfo rstto_file_info = 
        {
            sizeof (RsttoFileClass),
            (GBaseInitFunc) NULL,
            (GBaseFinalizeFunc) NULL,
            (GClassInitFunc) rstto_file_class_init,
            (GClassFinalizeFunc) NULL,
            NULL,
            sizeof (RsttoFile),
            0,
            (GInstanceInitFunc) rstto_file_init,
            NULL
        };

        rstto_file_type = g_type_register_static (
                G_TYPE_OBJECT,
                "RsttoFile",
                &rstto_file_info,
                0 );
    }
    return rstto_file_type;
}

struct _RsttoFilePriv
{
    GFile *file;

    gchar *display_name;
    gchar *content_type;

    gchar *uri;
    gchar *path;
    gchar *collate_key;

    gchar *thumbnail_path;
    GdkPixbuf *thumbnails[THUMBNAIL_SIZE_COUNT];

    ExifData *exif_data;
    RsttoImageOrientation orientation;
};


static void
rstto_file_init (GObject *object)
{
    RsttoFile *r_file = RSTTO_FILE (object);

    r_file->priv = g_new0 (RsttoFilePriv, 1);
}


static void
rstto_file_class_init (GObjectClass *object_class)
{
    RsttoFileClass *file_class = RSTTO_FILE_CLASS (object_class);

    parent_class = g_type_class_peek_parent (file_class);

    object_class->dispose = rstto_file_dispose;
    object_class->finalize = rstto_file_finalize;

    object_class->set_property = rstto_file_set_property;
    object_class->get_property = rstto_file_get_property;

    rstto_file_signals[RSTTO_FILE_SIGNAL_CHANGED] = g_signal_new("changed",
            G_TYPE_FROM_CLASS(object_class),
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
 * rstto_file_dispose:
 * @object:
 *
 */
static void
rstto_file_dispose (GObject *object)
{
    RsttoFile *r_file = RSTTO_FILE (object);
    gint i = 0;

    if (r_file->priv)
    {
        if (r_file->priv->file)
        {
            g_object_unref (r_file->priv->file);
            r_file->priv->file = NULL;
        }
        if (r_file->priv->display_name)
        {
            g_free (r_file->priv->display_name);
            r_file->priv->display_name = NULL;
        }
        if (r_file->priv->content_type)
        {
            g_free (r_file->priv->content_type);
            r_file->priv->content_type = NULL;
        }
        if (r_file->priv->path)
        {
            g_free (r_file->priv->path);
            r_file->priv->path = NULL;
        }
        if (r_file->priv->thumbnail_path)
        {
            g_free (r_file->priv->thumbnail_path);
            r_file->priv->thumbnail_path = NULL;
        }
        if (r_file->priv->uri)
        {
            g_free (r_file->priv->uri);
            r_file->priv->uri = NULL;
        }
        if (r_file->priv->collate_key)
        {
            g_free (r_file->priv->collate_key);
            r_file->priv->collate_key = NULL;
        }

        for (i = 0; i < THUMBNAIL_SIZE_COUNT; ++i)
        {
            if (r_file->priv->thumbnails[i])
            {
                g_object_unref (r_file->priv->thumbnails[i]);
                r_file->priv->thumbnails[i] = NULL;
            }
        }

        g_free (r_file->priv);
        r_file->priv = NULL;

        open_files = g_list_remove_all (open_files, r_file);
    }
}

/**
 * rstto_file_finalize:
 * @object:
 *
 */
static void
rstto_file_finalize (GObject *object)
{
    /*RsttoFile *file = RSTTO_FILE (object);*/
}



/**
 * rstto_file_new:
 *
 *
 * Singleton
 */
RsttoFile *
rstto_file_new ( GFile *file )
{
    RsttoFile *r_file = NULL;
    GList *iter = open_files;

    while ( NULL != iter )
    {
        /* Check if the file is already opened, if so
         * return that one.
         */
        r_file = RSTTO_FILE (iter->data);
        if ( TRUE == g_file_equal (
                r_file->priv->file,
                file) )
        {
            g_object_ref (G_OBJECT (iter->data));
            return (RsttoFile *)iter->data;
        }
        iter = g_list_next (iter);
    }

    r_file = g_object_new (RSTTO_TYPE_FILE, NULL);
    r_file->priv->file = file;
    g_object_ref (file);

    open_files = g_list_append (open_files, r_file);

    return r_file;
}


static void
rstto_file_set_property (
        GObject *object,
        guint property_id,
        const GValue *value,
        GParamSpec *pspec )
{
}

static void
rstto_file_get_property (
        GObject *object,
        guint property_id,
        GValue *value,
        GParamSpec *pspec )
{
}

GFile *
rstto_file_get_file ( RsttoFile *r_file )
{
    return r_file->priv->file;
}

gboolean
rstto_file_equal ( RsttoFile *r_file_a, RsttoFile *r_file_b )
{
    return r_file_a == r_file_b;
}

const gchar *
rstto_file_get_display_name ( RsttoFile *r_file )
{
    GFileInfo *file_info = NULL;
    const gchar *display_name;

    if ( NULL == r_file->priv->display_name )
    {
        file_info = g_file_query_info (
                r_file->priv->file,
                G_FILE_ATTRIBUTE_STANDARD_DISPLAY_NAME,
                0,
                NULL,
                NULL );
        if ( NULL != file_info )
        {
            display_name = g_file_info_get_display_name (file_info);
            if ( NULL != display_name )
            {
                r_file->priv->display_name = g_strdup (display_name);
            }
            g_object_unref (file_info);
        }
    }

    return (const gchar *)r_file->priv->display_name;
}

const gchar *
rstto_file_get_path ( RsttoFile *r_file )
{
    g_return_val_if_fail (RSTTO_IS_FILE (r_file), NULL);
    g_return_val_if_fail (G_IS_FILE (r_file->priv->file), NULL);

    if ( NULL == r_file->priv->path )
    {
        r_file->priv->path = g_file_get_path (r_file->priv->file);
    }
    return (const gchar *)r_file->priv->path;
}

const gchar *
rstto_file_get_uri ( RsttoFile *r_file )
{
    if ( NULL == r_file->priv->uri )
    {
        r_file->priv->uri = g_file_get_uri (r_file->priv->file);
    }
    return (const gchar *)r_file->priv->uri;
}

const gchar *
rstto_file_get_collate_key ( RsttoFile *r_file )
{
    if ( NULL == r_file->priv->collate_key )
    {
        gchar *basename = g_file_get_basename (rstto_file_get_file (r_file));
        if ( NULL != basename )
        {
            /* If we can use casefold for case insenstivie sorting, then
             * do so */
            gchar *casefold = g_utf8_casefold (basename, -1);
            if ( NULL != casefold )
            {
                r_file->priv->collate_key = g_utf8_collate_key_for_filename (casefold, -1);
                g_free (casefold);
            }
            else
            {
                r_file->priv->collate_key = g_utf8_collate_key_for_filename (basename, -1);
            }
            g_free (basename);
        }
    }
    return (const gchar *)r_file->priv->collate_key;
}

const gchar *
rstto_file_get_content_type ( RsttoFile *r_file )
{
    GFileInfo *file_info = NULL;
    const gchar *content_type;

    if ( NULL == r_file->priv->content_type )
    {
        file_info = g_file_query_info (
                r_file->priv->file,
                "standard::content-type",
                0,
                NULL,
                NULL );
        if ( NULL != file_info )
        {
            content_type = g_file_info_get_content_type (file_info);
            if ( NULL != content_type )
            {
                r_file->priv->content_type = g_strdup (content_type);
            }
            g_object_unref (file_info);
        }
    }

    return (const gchar *)r_file->priv->content_type;
}

guint64
rstto_file_get_modified_time ( RsttoFile *r_file )
{
    guint64 time_ = 0;
    GFileInfo *file_info = g_file_query_info (r_file->priv->file, "time::modified", 0, NULL, NULL);

    time_ = g_file_info_get_attribute_uint64 ( file_info, "time::modified" );

    g_object_unref (file_info);

    return time_;
}

ExifEntry *
rstto_file_get_exif ( RsttoFile *r_file, ExifTag id )
{
    /* If there is no exif-data object, try to create it */
    if ( NULL == r_file->priv->exif_data )
    {
        r_file->priv->exif_data = exif_data_new_from_file (
                rstto_file_get_path (r_file) );
    }

    if ( NULL != r_file->priv->exif_data )
    {
        return exif_data_get_entry (
                r_file->priv->exif_data,
                id );
    }

    /* If there is no exif-data, return NULL */
    return NULL;
}

RsttoImageOrientation
rstto_file_get_orientation ( RsttoFile *r_file )
{
    ExifEntry *exif_entry = NULL;
    if (r_file->priv->orientation == 0 )
    {
        /* Try to get the default orientation from the EXIF tag */
        exif_entry = rstto_file_get_exif (r_file, EXIF_TAG_ORIENTATION);
        if (NULL != exif_entry)
        {
            r_file->priv->orientation = exif_get_short (
                    exif_entry->data,
                    exif_data_get_byte_order (exif_entry->parent->parent));

            exif_entry_free (exif_entry);
        }

        /* If the orientation-tag is not set, default to NONE */
        if (r_file->priv->orientation == 0)
        {
            /* Default orientation */
            r_file->priv->orientation = RSTTO_IMAGE_ORIENT_NONE;
        }

    }
    return r_file->priv->orientation;
}

void
rstto_file_set_orientation (
        RsttoFile *r_file ,
        RsttoImageOrientation orientation )
{
    r_file->priv->orientation = orientation;
}

gboolean
rstto_file_has_exif ( RsttoFile *r_file )
{
    if ( NULL == r_file->priv->exif_data )
    {
        r_file->priv->exif_data = exif_data_new_from_file ( rstto_file_get_path (r_file) );
    }
    if ( NULL == r_file->priv->exif_data )
    {
        return FALSE;
    }
    return TRUE;
}

const gchar *
rstto_file_get_thumbnail_path ( RsttoFile *r_file)
{
    const gchar *uri;
    gchar *checksum;
    gchar *filename;

    if (NULL == r_file->priv->thumbnail_path)
    {
        uri = rstto_file_get_uri (r_file);
        checksum = g_compute_checksum_for_string (G_CHECKSUM_MD5, uri, strlen (uri));
        filename = g_strconcat (checksum, ".png", NULL);

        /* build and check if the thumbnail is in the new location */
        r_file->priv->thumbnail_path = g_build_path ("/", g_get_user_cache_dir(), "thumbnails", "normal", filename, NULL);

        if(!g_file_test (r_file->priv->thumbnail_path, G_FILE_TEST_EXISTS))
        {
            /* Fallback to old version */
            g_free (r_file->priv->thumbnail_path);

            r_file->priv->thumbnail_path = g_build_path ("/", g_get_home_dir(), ".thumbnails", "normal", filename, NULL);
            if(!g_file_test (r_file->priv->thumbnail_path, G_FILE_TEST_EXISTS))
            {
                /* Thumbnail doesn't exist in either spot */
                g_free (r_file->priv->thumbnail_path);
                r_file->priv->thumbnail_path = NULL;
            }
        }

        g_free (checksum);
        g_free (filename);
    }

    return r_file->priv->thumbnail_path;
}

const GdkPixbuf *
rstto_file_get_thumbnail (
        RsttoFile *r_file,
        RsttoThumbnailSize size )
{
    const gchar *thumbnail_path;
    RsttoThumbnailer *thumbnailer;

    if (r_file->priv->thumbnails[size])
        return r_file->priv->thumbnails[size];

    thumbnail_path = rstto_file_get_thumbnail_path (r_file);

    thumbnailer = rstto_thumbnailer_new();
    rstto_thumbnailer_queue_file (thumbnailer, r_file);

    if (NULL == thumbnail_path)
    {
        /* No thumbnail to return at this time */
        return NULL;
    }

    /* FIXME:
     *
     * The thumbnail should be updated on the "ready" signal
     * of the thumbnailer, to account for changed thumbnails
     * aswell as missing ones.
     */
    r_file->priv->thumbnails[size] = gdk_pixbuf_new_from_file_at_scale (
            thumbnail_path,
            rstto_thumbnail_size[size],
            rstto_thumbnail_size[size],
            TRUE,
            NULL);

    g_object_unref (thumbnailer);

    return r_file->priv->thumbnails[size];
}

void
rstto_file_changed ( RsttoFile *r_file )
{
    g_signal_emit (
            G_OBJECT (r_file),
            rstto_file_signals[RSTTO_FILE_SIGNAL_CHANGED],
            0,
            NULL);
}
