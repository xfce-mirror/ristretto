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

#ifndef __RISTRETTO_FILE_H__
#define __RISTRETTO_FILE_H__

G_BEGIN_DECLS

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
rstto_file_get_content_type ( RsttoFile * );

guint64
rstto_file_get_modified_time ( RsttoFile *);

gchar *
rstto_file_get_exif ( RsttoFile *, ExifTag );


G_END_DECLS

#endif /* __RISTRETTO_FILE_H__ */
