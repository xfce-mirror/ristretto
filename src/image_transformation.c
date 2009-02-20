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

static void
rstto_image_transformation_init (GObject *);
static void
rstto_image_transformation_class_init (GObjectClass *);

GType
rstto_image_transformation_get_type ()
{
    static GType rstto_image_transformation_type = 0;

    if (!rstto_image_transformation_type)
    {
        static const GTypeInfo rstto_image_transformation_info = 
        {
            sizeof (RsttoImageTransformationClass),
            (GBaseInitFunc) NULL,
            (GBaseFinalizeFunc) NULL,
            (GClassInitFunc) rstto_image_transformation_class_init,
            (GClassFinalizeFunc) NULL,
            NULL,
            sizeof (RsttoImageTransformation),
            0,
            (GInstanceInitFunc) rstto_image_transformation_init,
            NULL
        };

        rstto_image_transformation_type = g_type_register_static (G_TYPE_OBJECT, "RsttoImageTransformation", &rstto_image_transformation_info, 0);
    }
    return rstto_image_transformation_type;
}


static void
rstto_image_transformation_init (GObject *object)
{

}

static void
rstto_image_transformation_class_init (GObjectClass *object_class)
{

}
