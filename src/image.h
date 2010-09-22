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

#ifndef __RISTRETTO_IMAGE_H__
#define __RISTRETTO_IMAGE_H__

G_BEGIN_DECLS

typedef enum
{
  RSTTO_IMAGE_ORIENT_NONE = 1,
  RSTTO_IMAGE_ORIENT_FLIP_HORIZONTAL,
  RSTTO_IMAGE_ORIENT_180,
  RSTTO_IMAGE_ORIENT_FLIP_VERTICAL,
  RSTTO_IMAGE_ORIENT_TRANSPOSE,
  RSTTO_IMAGE_ORIENT_90,
  RSTTO_IMAGE_ORIENT_TRANSVERSE,
  RSTTO_IMAGE_ORIENT_270,
} RsttoImageOrientation;

#define RSTTO_TYPE_IMAGE rstto_image_get_type()

#define RSTTO_IMAGE(obj)( \
        G_TYPE_CHECK_INSTANCE_CAST ((obj), \
                RSTTO_TYPE_IMAGE, \
                RsttoImage))

#define RSTTO_IS_IMAGE(obj)( \
        G_TYPE_CHECK_INSTANCE_TYPE ((obj), \
                RSTTO_TYPE_IMAGE))

#define RSTTO_IMAGE_CLASS(klass)( \
        G_TYPE_CHECK_CLASS_CAST ((klass), \
                RSTTO_TYPE_IMAGE, \
                RsttoImageClass))

#define RSTTO_IS_IMAGE_CLASS(klass)( \
        G_TYPE_CHECK_CLASS_TYPE ((klass), \
                RSTTO_TYPE_IMAGE()))


typedef struct _RsttoImage RsttoImage;
typedef struct _RsttoImagePriv RsttoImagePriv;

struct _RsttoImage
{
    GObject parent;

    RsttoImagePriv *priv;
};

typedef struct _RsttoImageClass RsttoImageClass;

struct _RsttoImageClass
{
    GObjectClass parent_class;
};

RsttoImage *rstto_image_new (GFile *file);
GType       rstto_image_get_type ();

GdkPixbuf *rstto_image_get_thumbnail (RsttoImage *image);
GdkPixbuf *rstto_image_get_pixbuf (RsttoImage *image);
gint rstto_image_get_width (RsttoImage *image);
gint rstto_image_get_height (RsttoImage *image);

GFile *rstto_image_get_file (RsttoImage *image);
void rstto_image_unload (RsttoImage *image);
gboolean rstto_image_load (RsttoImage *image, gboolean empty_cache, gdouble scale, gboolean preload, GError **error);

guint64 rstto_image_get_size (RsttoImage *image);

void
rstto_image_set_orientation (RsttoImage *image, RsttoImageOrientation orientation);
RsttoImageOrientation
rstto_image_get_orientation (RsttoImage *image);

G_END_DECLS

#endif /* __RISTRETTO_IMAGE_H__ */
