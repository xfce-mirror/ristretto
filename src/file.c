/*
 *  Copyright (c) Stephan Arts 2011 <stephan@xfce.org>
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

#include <libexif/exif-data.h>

#include <libxfce4util/libxfce4util.h>

#include "util.h"
#include "file.h"

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

    ExifData *exif_data;
    RsttoImageOrientation orientation;
};


static void
rstto_file_init (GObject *object)
{
    RsttoFile *file = RSTTO_FILE (object);

    file->priv = g_new0 (RsttoFilePriv, 1);
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
}

/**
 * rstto_file_dispose:
 * @object:
 *
 */
static void
rstto_file_dispose (GObject *object)
{
    RsttoFile *file = RSTTO_FILE (object);

    if (file->priv)
    {
        if (file->priv->file)
        {
            g_object_unref (file->priv->file);
            file->priv->file = NULL;
        }
        if (file->priv->display_name)
        {
            g_free (file->priv->display_name);
            file->priv->display_name = NULL;
        }
        if (file->priv->content_type)
        {
            g_free (file->priv->content_type);
            file->priv->content_type = NULL;
        }
        if (file->priv->path)
        {
            g_free (file->priv->path);
            file->priv->path = NULL;
        }
        if (file->priv->uri)
        {
            g_free (file->priv->uri);
            file->priv->uri = NULL;
        }

        g_free (file->priv);
        file->priv = NULL;

        open_files = g_list_remove_all (open_files, file);
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
    RsttoFile *o_file = NULL;
    GList *iter = open_files;
    while ( NULL != iter )
    {
        /* Check if the file is already opened, if so
         * return that one.
         */
        o_file = RSTTO_FILE (iter->data);
        if ( TRUE == g_file_equal (
                o_file->priv->file,
                file) )
        {
            return (RsttoFile *)iter->data;
        }
        iter = g_list_next (iter);
    }

    o_file = g_object_new (RSTTO_TYPE_FILE, NULL);
    o_file->priv->file = file;
    g_object_ref (file);

    open_files = g_list_append (open_files, o_file);

    return o_file;
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
rstto_file_get_file ( RsttoFile *file )
{
    return file->priv->file;
}

gboolean
rstto_file_equal ( RsttoFile *a, RsttoFile *b )
{
    return g_file_equal (a->priv->file, b->priv->file);
}

const gchar *
rstto_file_get_display_name ( RsttoFile *file )
{
    GFileInfo *file_info = NULL;
    const gchar *display_name;

    if ( NULL == file->priv->display_name )
    {
        file_info = g_file_query_info (
                file->priv->file,
                G_FILE_ATTRIBUTE_STANDARD_DISPLAY_NAME,
                0,
                NULL,
                NULL );
        if ( NULL != file_info )
        {
            display_name = g_file_info_get_display_name (file_info);
            if ( NULL != display_name )
            {
                file->priv->display_name = g_strdup (display_name);
            }
            g_object_unref (file_info);
        }
    }

    return (const gchar *)file->priv->display_name;
}

const gchar *
rstto_file_get_path ( RsttoFile *file )
{
    g_return_val_if_fail (RSTTO_IS_FILE (file), NULL);
    g_return_val_if_fail (G_IS_FILE (file->priv->file), NULL);

    if ( NULL == file->priv->path )
    {
        file->priv->path = g_file_get_path (file->priv->file);
    }
    return (const gchar *)file->priv->path;
}

const gchar *
rstto_file_get_uri ( RsttoFile *file )
{
    if ( NULL == file->priv->uri )
    {
        file->priv->uri = g_file_get_uri (file->priv->file);
    }
    return (const gchar *)file->priv->uri;
}

const gchar *
rstto_file_get_content_type ( RsttoFile *file )
{
    GFileInfo *file_info = NULL;
    const gchar *content_type;

    if ( NULL == file->priv->content_type )
    {
        file_info = g_file_query_info (
                file->priv->file,
                "standard::content-type",
                0,
                NULL,
                NULL );
        if ( NULL != file_info )
        {
            content_type = g_file_info_get_content_type (file_info);
            if ( NULL != content_type )
            {
                file->priv->content_type = g_strdup (content_type);
            }
            g_object_unref (file_info);
        }
    }

    return (const gchar *)file->priv->content_type;
}

guint64
rstto_file_get_modified_time ( RsttoFile *file )
{
    guint64 time_ = 0;
    GFileInfo *file_info = g_file_query_info (file->priv->file, "time::modified", 0, NULL, NULL);

    time_ = g_file_info_get_attribute_uint64 ( file_info, "time::modified" );

    g_object_unref (file_info);

    return time_;
}

ExifEntry *
rstto_file_get_exif ( RsttoFile *file, ExifTag id )
{
    /* If there is no exif-data object, try to create it */
    if ( NULL == file->priv->exif_data )
    {
        file->priv->exif_data = exif_data_new_from_file ( rstto_file_get_path (file) );
    }
    if ( NULL != file->priv->exif_data )
    {
        return exif_data_get_entry (
                file->priv->exif_data,
                id );
    }
    return NULL;
}

RsttoImageOrientation
rstto_file_get_orientation ( RsttoFile *file )
{
    ExifEntry *exif_entry = NULL;
    if (file->priv->orientation == 0 )
    {
        exif_entry = rstto_file_get_exif (file, EXIF_TAG_ORIENTATION);
        if (NULL != exif_entry)
        {
            file->priv->orientation = exif_get_short (
                    exif_entry->data,
                    exif_data_get_byte_order (exif_entry->parent->parent));

            exif_entry_free (exif_entry);
        }
        if (file->priv->orientation == 0)
        {
            /* Default orientation */
            file->priv->orientation = RSTTO_IMAGE_ORIENT_NONE;
        }

    }
    return file->priv->orientation;
}

void
rstto_file_set_orientation (
        RsttoFile *file ,
        RsttoImageOrientation orientation )
{
    file->priv->orientation = orientation;
}

gboolean
rstto_file_has_exif ( RsttoFile *file )
{
    if ( NULL == file->priv->exif_data )
    {
        file->priv->exif_data = exif_data_new_from_file ( rstto_file_get_path (file) );
    }
    if ( NULL == file->priv->exif_data )
    {
        return FALSE;
    }
    return TRUE;
}
