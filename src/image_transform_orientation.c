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
#include "image_transformation.h"
#include "image_transform_orientation.h"

static void
rstto_image_transform_orientation_init  (GObject *);
static void
rstto_image_transform_orientation_class_init (GObjectClass *);

static void
rstto_image_transform_orientation_get_property (GObject    *object,
                                                guint       property_id,
                                                GValue     *value,
                                                GParamSpec *pspec);
static void
rstto_image_transform_orientation_set_property (GObject      *object,
                                                guint         property_id,
                                                const GValue *value,
                                                GParamSpec   *pspec);

static gboolean
rstto_image_transform_orientation_transform (RsttoImageTransformation *transformation, RsttoImage *image);
static gboolean
rstto_image_transform_orientation_revert (RsttoImageTransformation *transformation, RsttoImage *image);

enum
{
    PROP_0,
    PROP_TRANSFORM_FLIP_VERTICAL,
    PROP_TRANSFORM_FLIP_HORIZONTAL,
    PROP_TRANSFORM_ROTATION
};

GType
rstto_image_transform_orientation_get_type ()
{
    static GType rstto_image_transform_orientation_type = 0;

    if (!rstto_image_transform_orientation_type)
    {
        static const GTypeInfo rstto_image_transform_orientation_info = 
        {
            sizeof (RsttoImageTransformOrientationClass),
            (GBaseInitFunc) NULL,
            (GBaseFinalizeFunc) NULL,
            (GClassInitFunc) rstto_image_transform_orientation_class_init,
            (GClassFinalizeFunc) NULL,
            NULL,
            sizeof (RsttoImageTransformOrientation),
            0,
            (GInstanceInitFunc) rstto_image_transform_orientation_init,
            NULL
        };

        rstto_image_transform_orientation_type = g_type_register_static (RSTTO_TYPE_IMAGE_TRANSFORMATION, "RsttoImageTransformOrientation", &rstto_image_transform_orientation_info, 0);
    }
    return rstto_image_transform_orientation_type;
}


static void
rstto_image_transform_orientation_init (GObject *object)
{
    RsttoImageTransformation *transformation = RSTTO_IMAGE_TRANSFORMATION (object);
    
    transformation->transform = rstto_image_transform_orientation_transform;
    transformation->revert = rstto_image_transform_orientation_revert;
}

static void
rstto_image_transform_orientation_class_init (GObjectClass *object_class)
{
    GParamSpec *pspec;

    object_class->set_property = rstto_image_transform_orientation_set_property;
    object_class->get_property = rstto_image_transform_orientation_get_property;

    pspec = g_param_spec_boolean ("flip-vertical",
                                  "",
                                  "Indicates if this performs a vertical flip",
                                  FALSE /* default value */,
                                  G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE);

    g_object_class_install_property (object_class,
                                     PROP_TRANSFORM_FLIP_VERTICAL,
                                     pspec);

    pspec = g_param_spec_boolean ("flip-horizontal",
                                  "",
                                  "Indicates if this performs a horizontal flip",
                                  FALSE /* default value */,
                                  G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE);

    g_object_class_install_property (object_class,
                                     PROP_TRANSFORM_FLIP_HORIZONTAL,
                                     pspec);


    pspec = g_param_spec_uint ("rotation",
                               "",
                               "Indicates how it rotates",
                               0 /* minimum */,
                               360 /* maximum */,
                               0 /* default value */,
                               G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE);

    g_object_class_install_property (object_class,
                                     PROP_TRANSFORM_ROTATION,
                                     pspec);

}

RsttoImageTransformation *
rstto_image_transform_orientation_new (gboolean flip_vertical, 
                                       gboolean flip_horizontal,
                                       GdkPixbufRotation rotation)
{
    RsttoImageTransformation *transformation = g_object_new (RSTTO_TYPE_IMAGE_TRANSFORM_ORIENTATION, 
                                                             "flip-vertical", flip_vertical,
                                                             "flip-horizontal", flip_horizontal,
                                                             "rotation", rotation,
                                                             NULL);

    return transformation;
}

static gboolean
rstto_image_transform_orientation_transform (RsttoImageTransformation *transformation, RsttoImage *image)
{
    RsttoImageTransformOrientation *trans_orientation = RSTTO_IMAGE_TRANSFORM_ORIENTATION (transformation);
    GdkPixbuf *tmp_pixbuf = NULL;

    if (trans_orientation->flip_vertical)
    {
        tmp_pixbuf = rstto_image_get_pixbuf (image);       
        if (tmp_pixbuf)
        {
            g_object_ref (tmp_pixbuf);
            /* Flip vertically (pass FALSE to gdk_pixbuf_flip) */
            rstto_image_set_pixbuf (image, gdk_pixbuf_flip (tmp_pixbuf, FALSE));
            g_object_unref (tmp_pixbuf);
        }
    }   

    if (trans_orientation->flip_horizontal)
    {
        tmp_pixbuf = rstto_image_get_pixbuf (image);       
        if (tmp_pixbuf)
        {
            g_object_ref (tmp_pixbuf);
            /* Flip horizontally (pass TRUE to gdk_pixbuf_flip) */
            rstto_image_set_pixbuf (image, gdk_pixbuf_flip (tmp_pixbuf, TRUE));
            g_object_unref (tmp_pixbuf);
        }
    }   

    
    tmp_pixbuf = rstto_image_get_pixbuf (image);       
    if (tmp_pixbuf)
    {
        g_object_ref (tmp_pixbuf);
        rstto_image_set_pixbuf (image, gdk_pixbuf_rotate_simple (tmp_pixbuf, (360+(trans_orientation->rotation))%360));
        g_object_unref (tmp_pixbuf);
    }

    return TRUE;
}

static gboolean
rstto_image_transform_orientation_revert (RsttoImageTransformation *transformation, RsttoImage *image)
{
    RsttoImageTransformOrientation *trans_orientation = RSTTO_IMAGE_TRANSFORM_ORIENTATION (transformation);
    GdkPixbuf *tmp_pixbuf = NULL;

    if (trans_orientation->flip_vertical)
    {
        tmp_pixbuf = rstto_image_get_pixbuf (image);       
        if (tmp_pixbuf)
        {
            /* Flip vertically (pass FALSE to gdk_pixbuf_flip) */
            rstto_image_set_pixbuf (image, gdk_pixbuf_flip (tmp_pixbuf, FALSE));
            g_object_unref (tmp_pixbuf);
        }
    }   

    if (trans_orientation->flip_horizontal)
    {
        tmp_pixbuf = rstto_image_get_pixbuf (image);       
        if (tmp_pixbuf)
        {
            /* Flip horizontally (pass TRUE to gdk_pixbuf_flip) */
            rstto_image_set_pixbuf (image, gdk_pixbuf_flip (tmp_pixbuf, TRUE));
            g_object_unref (tmp_pixbuf);
        }
    }   

    tmp_pixbuf = rstto_image_get_pixbuf (image);       
    g_object_ref (tmp_pixbuf);
    rstto_image_set_pixbuf (image, gdk_pixbuf_rotate_simple (tmp_pixbuf, 360-((360+(trans_orientation->rotation))%360)));
    g_object_unref (tmp_pixbuf);

    return TRUE;
}

static void
rstto_image_transform_orientation_set_property (GObject      *object,
                                                guint         property_id,
                                                const GValue *value,
                                                GParamSpec   *pspec)
{
    RsttoImageTransformOrientation *transformation = RSTTO_IMAGE_TRANSFORM_ORIENTATION (object);

    switch (property_id)
    {
        case PROP_TRANSFORM_FLIP_VERTICAL:
            transformation->flip_vertical = g_value_get_boolean (value);
            break;
        case PROP_TRANSFORM_FLIP_HORIZONTAL:
            transformation->flip_horizontal = g_value_get_boolean (value);
            break;
        case PROP_TRANSFORM_ROTATION:
            transformation->rotation = g_value_get_uint (value);
            break;
        default:
            break;
    }
}


static void
rstto_image_transform_orientation_get_property (GObject    *object,
                                                guint       property_id,
                                                GValue     *value,
                                                GParamSpec *pspec)
{
    RsttoImageTransformOrientation *transformation = RSTTO_IMAGE_TRANSFORM_ORIENTATION (object);

    switch (property_id)
    {
        case PROP_TRANSFORM_FLIP_VERTICAL:
            g_value_set_boolean (value, transformation->flip_vertical);
            break;
        case PROP_TRANSFORM_FLIP_HORIZONTAL:
            g_value_set_boolean (value, transformation->flip_horizontal);
            break;
        case PROP_TRANSFORM_ROTATION:
            g_value_set_uint (value, transformation->rotation);
            break;
        default:
            break;
    }
}
