/*
 *  Copyright (C) Stephan Arts 2006-2010 <stephan@xfce.org>
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

#ifndef __RISTRETTO_THUMBNAIL_H__
#define __RISTRETTO_THUMBNAIL_H__

G_BEGIN_DECLS

#define RSTTO_TYPE_THUMBNAIL rstto_thumbnail_get_type()

#define RSTTO_THUMBNAIL(obj)( \
        G_TYPE_CHECK_INSTANCE_CAST ((obj), \
                RSTTO_TYPE_THUMBNAIL, \
                RsttoThumbnail))

#define RSTTO_IS_THUMBNAIL(obj)( \
        G_TYPE_CHECK_INSTANCE_TYPE ((obj), \
                RSTTO_TYPE_THUMBNAIL))

#define RSTTO_THUMBNAIL_CLASS(klass)( \
        G_TYPE_CHECK_CLASS_CAST ((klass), \
                RSTTO_TYPE_THUMBNAIL, \
                RsttoThumbnailClass))

#define RSTTO_IS_THUMBNAIL_CLASS(klass)( \
        G_TYPE_CHECK_CLASS_TYPE ((klass), \
                RSTTO_TYPE_THUMBNAIL()))

typedef struct _RsttoThumbnailPriv RsttoThumbnailPriv;

typedef struct _RsttoThumbnail RsttoThumbnail;

struct _RsttoThumbnail
{
    GtkButton     parent;
    RsttoThumbnailPriv *priv;
    gboolean            selected;
};

typedef struct _RsttoThumbnailClass RsttoThumbnailClass;

struct _RsttoThumbnailClass
{
    GtkButtonClass  parent_class;
};

GType      rstto_thumbnail_get_type();

GtkWidget  *rstto_thumbnail_new (RsttoImage *image);
RsttoImage *rstto_thumbnail_get_image (RsttoThumbnail *thumb);


void rstto_thumbnail_update (RsttoThumbnail *thumb);

G_END_DECLS

#endif /* __RISTRETTO_THUMBNAIL_H__ */
