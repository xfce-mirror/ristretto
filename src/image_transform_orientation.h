/*
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

#ifndef __RISTRETTO_IMAGE_TRANSFORM_ORIENTATION_H__
#define __RISTRETTO_IMAGE_TRANSFORM_ORIENTATION_H__

G_BEGIN_DECLS

#define RSTTO_TYPE_IMAGE_TRANSFORM_ORIENTATION rstto_image_transform_orientation_get_type()

#define RSTTO_IMAGE_TRANSFORM_ORIENTATION(obj)( \
        G_TYPE_CHECK_INSTANCE_CAST ((obj), \
                RSTTO_TYPE_IMAGE_TRANSFORM_ORIENTATION, \
                RsttoImageTransformOrientation))

#define RSTTO_IS_IMAGE_TRANSFORM_ORIENTATION(obj)( \
        G_TYPE_CHECK_INSTANCE_TYPE ((obj), \
                RSTTO_TYPE_IMAGE_TRANSFORM_ORIENTATION))

#define RSTTO_IMAGE_TRANSFORM_ORIENTATION_CLASS(klass)( \
        G_TYPE_CHECK_CLASS_CAST ((klass), \
                RSTTO_TYPE_IMAGE_TRANSFORM_ORIENTATION, \
                RsttoImageTransformOrientationClass))

#define RSTTO_IS_IMAGE_TRANSFORM_ORIENTATION_CLASS(klass)( \
        G_TYPE_CHECK_CLASS_TYPE ((klass), \
                RSTTO_TYPE_IMAGE_TRANSFORM_ORIENTATION()))

typedef struct _RsttoImageTransformOrientation RsttoImageTransformOrientation;

struct _RsttoImageTransformOrientation
{
    RsttoImageTransformation parent;

    gboolean          flip_horizontal;
    gboolean          flip_vertical;
    GdkPixbufRotation rotation;
};

typedef struct _RsttoImageTransformOrientationClass RsttoImageTransformOrientationClass;

struct _RsttoImageTransformOrientationClass
{
    RsttoImageTransformationClass parent_class;
};

RsttoImageTransformation *
rstto_image_transform_orientation_new (gboolean flip_vertical, 
                                       gboolean flip_horizontal,
                                       GdkPixbufRotation rotation);

G_END_DECLS

#endif /* __RISTRETTO_IMAGE_TRANSFORM_ORIENTATION_H__ */
