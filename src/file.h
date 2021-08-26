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

#ifndef __RISTRETTO_FILE_H__
#define __RISTRETTO_FILE_H__

#include <gtk/gtk.h>
#include <libexif/exif-data.h>

G_BEGIN_DECLS

typedef enum
{
    RSTTO_IMAGE_ORIENT_NONE = 1,
    RSTTO_IMAGE_ORIENT_FLIP_HORIZONTAL,
    RSTTO_IMAGE_ORIENT_180,
    RSTTO_IMAGE_ORIENT_FLIP_VERTICAL,
    RSTTO_IMAGE_ORIENT_FLIP_TRANSPOSE,
    RSTTO_IMAGE_ORIENT_90,
    RSTTO_IMAGE_ORIENT_FLIP_TRANSVERSE,
    RSTTO_IMAGE_ORIENT_270,
    RSTTO_IMAGE_ORIENT_NOT_DETERMINED,
} RsttoImageOrientation;

#define THUMBNAIL_SIZE_VERY_SMALL_SIZE   24
#define THUMBNAIL_SIZE_SMALLER_SIZE      32
#define THUMBNAIL_SIZE_SMALL_SIZE        48
#define THUMBNAIL_SIZE_NORMAL_SIZE       64
#define THUMBNAIL_SIZE_LARGE_SIZE        96
#define THUMBNAIL_SIZE_LARGER_SIZE      128
#define THUMBNAIL_SIZE_VERY_LARGE_SIZE  256

typedef enum {
    THUMBNAIL_SIZE_VERY_SMALL = 0,
    THUMBNAIL_SIZE_SMALLER,
    THUMBNAIL_SIZE_SMALL,
    THUMBNAIL_SIZE_NORMAL,
    THUMBNAIL_SIZE_LARGE,
    THUMBNAIL_SIZE_LARGER,
    THUMBNAIL_SIZE_VERY_LARGE,
    THUMBNAIL_SIZE_COUNT,
} RsttoThumbnailSize;




#define RSTTO_TYPE_FILE rstto_file_get_type()

#define RSTTO_FILE(obj)( \
        G_TYPE_CHECK_INSTANCE_CAST ((obj), \
                RSTTO_TYPE_FILE, \
                RsttoFile))

#define RSTTO_IS_FILE(obj)( \
        G_TYPE_CHECK_INSTANCE_TYPE ((obj), \
                RSTTO_TYPE_FILE))

#define RSTTO_FILE_CLASS(klass)( \
        G_TYPE_CHECK_CLASS_CAST ((klass), \
                RSTTO_TYPE_FILE, \
                RsttoFileClass))

#define RSTTO_IS_FILE_CLASS(klass)( \
        G_TYPE_CHECK_CLASS_TYPE ((klass), \
                RSTTO_TYPE_FILE()))


typedef struct _RsttoFile RsttoFile;
typedef struct _RsttoFilePriv RsttoFilePriv;

struct _RsttoFile
{
    GObject parent;

    RsttoFilePriv *priv;
};

typedef struct _RsttoFileClass RsttoFileClass;

struct _RsttoFileClass
{
    GObjectClass parent_class;
};

RsttoFile *
rstto_file_new ( GFile * );

GType
rstto_file_get_type ( void );

GFile *
rstto_file_get_file ( RsttoFile * );

gboolean
rstto_file_equal ( RsttoFile *, RsttoFile * );

const gchar *
rstto_file_get_display_name ( RsttoFile * );

const gchar *
rstto_file_get_path ( RsttoFile * );

const gchar *
rstto_file_get_uri ( RsttoFile * );

const gchar *
rstto_file_get_collate_key ( RsttoFile * );

const gchar *
rstto_file_get_content_type ( RsttoFile * );

const gchar *
rstto_file_get_thumbnail_path ( RsttoFile *);

const GdkPixbuf *
rstto_file_get_thumbnail ( RsttoFile *, RsttoThumbnailSize );

guint64
rstto_file_get_modified_time ( RsttoFile *);

goffset
rstto_file_get_size ( RsttoFile * );

ExifEntry *
rstto_file_get_exif ( RsttoFile *, ExifTag );

RsttoImageOrientation
rstto_file_get_orientation ( RsttoFile * );

void
rstto_file_set_orientation (
        RsttoFile * ,
        RsttoImageOrientation );

gboolean
rstto_file_has_exif ( RsttoFile * );

void
rstto_file_changed ( RsttoFile * );


G_END_DECLS

#endif /* __RISTRETTO_FILE_H__ */
