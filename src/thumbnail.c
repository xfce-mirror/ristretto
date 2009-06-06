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

#include <config.h>
#include <gtk/gtk.h>
#include <gtk/gtkmarshal.h>
#include <string.h>

#include <libexif/exif-data.h>

#include "image.h"
#include "image_list.h"
#include "thumbnail.h"

struct _RsttoThumbnailPriv
{
    RsttoImage          *image;
};

static GtkWidgetClass *parent_class = NULL;

static void
rstto_thumbnail_init(RsttoThumbnail *);
static void
rstto_thumbnail_class_init(RsttoThumbnailClass *);
static void
rstto_thumbnail_finalize(GObject *object);

static void
rstto_thumbnail_size_request(GtkWidget *, GtkRequisition *);
static void
rstto_thumbnail_size_allocate(GtkWidget *, GtkAllocation *);
static gboolean 
rstto_thumbnail_expose(GtkWidget *, GdkEventExpose *);

static void
rstto_thumbnail_paint(RsttoThumbnail *thumb);

static void
rstto_thumbnail_clicked(GtkButton *);

GType
rstto_thumbnail_get_type (void)
{
    static GType rstto_thumbnail_type = 0;

    if (!rstto_thumbnail_type)
    {
        static const GTypeInfo rstto_thumbnail_info = 
        {
            sizeof (RsttoThumbnailClass),
            (GBaseInitFunc) NULL,
            (GBaseFinalizeFunc) NULL,
            (GClassInitFunc) rstto_thumbnail_class_init,
            (GClassFinalizeFunc) NULL,
            NULL,
            sizeof (RsttoThumbnail),
            0,
            (GInstanceInitFunc) rstto_thumbnail_init,
            NULL
        };

        rstto_thumbnail_type = g_type_register_static (GTK_TYPE_BUTTON, "RsttoThumbnail", &rstto_thumbnail_info, 0);
    }
    return rstto_thumbnail_type;
}

static void
rstto_thumbnail_init(RsttoThumbnail *thumb)
{
    thumb->priv = g_new0(RsttoThumbnailPriv, 1);

    gtk_widget_set_redraw_on_allocate(GTK_WIDGET(thumb), TRUE);
    gtk_widget_set_events (GTK_WIDGET(thumb),
                           GDK_POINTER_MOTION_MASK);
}

static void
rstto_thumbnail_class_init(RsttoThumbnailClass *thumb_class)
{
    GtkWidgetClass *widget_class;
    GObjectClass *object_class;
    GtkButtonClass *button_class;

    widget_class = (GtkWidgetClass*)thumb_class;
    object_class = (GObjectClass*)thumb_class;
    button_class = (GtkButtonClass*)thumb_class;

    parent_class = g_type_class_peek_parent(thumb_class);

    widget_class->expose_event = rstto_thumbnail_expose;

    widget_class->size_request = rstto_thumbnail_size_request;
    widget_class->size_allocate = rstto_thumbnail_size_allocate;

    button_class->clicked = rstto_thumbnail_clicked;

    object_class->finalize = rstto_thumbnail_finalize;
}

static void
rstto_thumbnail_size_request(GtkWidget *widget, GtkRequisition *requisition)
{
    requisition->height = 70;
    requisition->width = 70;
}

static void
rstto_thumbnail_size_allocate(GtkWidget *widget, GtkAllocation *allocation)
{
    widget->allocation = *allocation;

    parent_class->size_allocate(widget, allocation);
}

static gboolean
rstto_thumbnail_expose(GtkWidget *widget, GdkEventExpose *event)
{
    RsttoThumbnail *thumb = RSTTO_THUMBNAIL(widget);

    if (GTK_WIDGET_REALIZED (widget))
    {
        GdkRegion *region = event->region;

        gdk_window_begin_paint_region(widget->window, region);
        rstto_thumbnail_paint(thumb);
        gdk_window_end_paint(widget->window);
    }

    return FALSE;
}

static void
rstto_thumbnail_finalize(GObject *object)
{
    RsttoThumbnail *thumb = RSTTO_THUMBNAIL(object);
    if (thumb->priv->image)
    {
        g_object_unref (thumb->priv->image);
        thumb->priv->image = NULL;
    }

}

static void
rstto_thumbnail_paint(RsttoThumbnail *thumb)
{
    GtkWidget *widget = GTK_WIDGET(thumb);

    GtkStateType state = GTK_WIDGET_STATE(widget);
    GdkPixbuf *pixbuf;
    guint pixbuf_height = 0;
    guint pixbuf_width = 0;

    if(thumb->priv->image)
    {

        if (GTK_WIDGET_STATE(widget) != GTK_STATE_PRELIGHT)
        {
        }

        pixbuf = rstto_image_get_thumbnail (
                                thumb->priv->image);
        if (pixbuf == NULL)
        {
            pixbuf = gtk_icon_theme_load_icon (gtk_icon_theme_get_default(),
                                               "image-missing",
                                               128,
                                               0,
                                               NULL);
        }

        gtk_paint_box(widget->style,
                      widget->window,
                      state,
                      GTK_SHADOW_ETCHED_IN,
                      NULL,
                      widget,
                      NULL,
                      widget->allocation.x, widget->allocation.y,
                      widget->allocation.width, widget->allocation.height);

        if(pixbuf)
        {
            pixbuf_height = gdk_pixbuf_get_height (pixbuf);
            pixbuf_width = gdk_pixbuf_get_width (pixbuf);

            if (pixbuf_height > pixbuf_width)
            {
                pixbuf_width = widget->allocation.height * pixbuf_width / pixbuf_height;
                pixbuf_height = widget->allocation.height;
            }
            else
            {
                pixbuf_height = widget->allocation.width * pixbuf_height / pixbuf_width;
                pixbuf_width = widget->allocation.width;
            }

            pixbuf = gdk_pixbuf_scale_simple (pixbuf, pixbuf_width, pixbuf_height, GDK_INTERP_BILINEAR);
            gdk_draw_pixbuf(GDK_DRAWABLE(widget->window),
                            NULL,
                            pixbuf,
                            0, 0,
                            (0.5 * (widget->allocation.width - gdk_pixbuf_get_width(pixbuf))) + widget->allocation.x,
                            (0.5 * (widget->allocation.height - gdk_pixbuf_get_height(pixbuf))) + widget->allocation.y,
                            -1, -1,
                            GDK_RGB_DITHER_NORMAL,
                            0, 0);
        }
    }
}

GtkWidget *
rstto_thumbnail_new (RsttoImage *image)
{
    gchar *path, *basename;
    GFile *file = NULL;
    RsttoThumbnail *thumb;

    g_return_val_if_fail (image != NULL, NULL);

    thumb = g_object_new(RSTTO_TYPE_THUMBNAIL, NULL);

    thumb->priv->image = image;
    g_object_ref (image);

    file = rstto_image_get_file (image);

    path = g_file_get_path (file);
    basename = g_path_get_basename (path);


    gtk_widget_set_tooltip_text(GTK_WIDGET(thumb), basename);

    g_free (basename);
    g_free (path);
    return GTK_WIDGET(thumb);
}

RsttoImage *
rstto_thumbnail_get_image (RsttoThumbnail *thumb)
{
    return thumb->priv->image;
}

/* CALLBACKS */
/*************/

static void
rstto_thumbnail_clicked (GtkButton *button)
{
    gtk_widget_queue_draw (GTK_WIDGET (button));
}
