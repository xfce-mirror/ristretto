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
    GdkPixbuf           *pixbuf;
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
rstto_thumbnail_clicked (GtkButton *);
static void
rstto_thumbnail_enter (GtkButton *);
static void
rstto_thumbnail_leave (GtkButton *);

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
    button_class->enter = rstto_thumbnail_enter;
    button_class->leave = rstto_thumbnail_leave;

    object_class->finalize = rstto_thumbnail_finalize;
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
rstto_thumbnail_size_request(GtkWidget *widget, GtkRequisition *requisition)
{
    requisition->height = 70;
    requisition->width = 70;
}

static void
rstto_thumbnail_size_allocate(GtkWidget *widget, GtkAllocation *allocation)
{
    RsttoThumbnail *thumb = RSTTO_THUMBNAIL(widget);
    widget->allocation = *allocation;
    parent_class->size_allocate(widget, allocation);

    if (thumb->priv->pixbuf)
    {
        g_object_unref (thumb->priv->pixbuf);
        thumb->priv->pixbuf = NULL;
    }

    thumb->priv->pixbuf = gdk_pixbuf_new (GDK_COLORSPACE_RGB,
                                          TRUE,
                                          8,
                                          allocation->width,
                                          allocation->height);
}

static gboolean
rstto_thumbnail_expose(GtkWidget *widget, GdkEventExpose *event)
{
    RsttoThumbnail *thumb = RSTTO_THUMBNAIL(widget);
    GdkPixbuf *thumb_pixbuf = NULL;

    if (GTK_WIDGET_REALIZED (widget))
    {
        if (thumb->priv->image)
        {
            thumb_pixbuf = rstto_image_get_thumbnail (thumb->priv->image);
        }

        if (thumb_pixbuf == NULL)
        {
            thumb_pixbuf = gtk_icon_theme_load_icon (gtk_icon_theme_get_default(),
                                                     "image-missing",
                                                     128,
                                                     0,
                                                     NULL);
        }
        else
        {
            g_object_ref (thumb_pixbuf);
        }

        if (thumb_pixbuf)
        {
            guint height = gdk_pixbuf_get_height (thumb->priv->pixbuf) - 10;
            guint width = gdk_pixbuf_get_width (thumb->priv->pixbuf) - 10;

            if (gdk_pixbuf_get_width (thumb_pixbuf) > gdk_pixbuf_get_height (thumb_pixbuf))
            {
                height = (guint)((gdouble)width / (gdouble)gdk_pixbuf_get_width (thumb_pixbuf) * (gdouble)gdk_pixbuf_get_height (thumb_pixbuf));
            }
            else
            {
                width = (guint)((gdouble)height / (gdouble)gdk_pixbuf_get_height (thumb_pixbuf) * (gdouble)gdk_pixbuf_get_width (thumb_pixbuf));
            }

            gdk_pixbuf_fill (thumb->priv->pixbuf, 0x00000000);
            gdk_pixbuf_scale (thumb_pixbuf, thumb->priv->pixbuf,
                              ((widget->allocation.width - width) / 2), ((widget->allocation.height - height) / 2), 
                              width,
                              height,
                              0, 0,
                              (gdouble)width / ((gdouble)gdk_pixbuf_get_width (thumb_pixbuf)),
                              (gdouble)height / ((gdouble)gdk_pixbuf_get_height (thumb_pixbuf)),
                              GDK_INTERP_BILINEAR);

            g_object_unref (thumb_pixbuf);
        }


        gdk_window_begin_paint_region(widget->window, event->region);
        rstto_thumbnail_paint(thumb);
        gdk_window_end_paint(widget->window);
    }

    return FALSE;
}

static void
rstto_thumbnail_paint(RsttoThumbnail *thumb)
{
    GtkWidget *widget = GTK_WIDGET(thumb);

    GtkStateType state = GTK_WIDGET_STATE(widget);

    if(thumb->priv->image)
    {

        if (GTK_WIDGET_STATE(widget) != GTK_STATE_PRELIGHT)
        {
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

        if (thumb->priv->pixbuf)
        {
            gdk_draw_pixbuf(GDK_DRAWABLE(widget->window),
                            NULL,
                            thumb->priv->pixbuf,
                            0, 0,
                            (0.5 * (widget->allocation.width - gdk_pixbuf_get_width(thumb->priv->pixbuf))) + widget->allocation.x,
                            (0.5 * (widget->allocation.height - gdk_pixbuf_get_height(thumb->priv->pixbuf))) + widget->allocation.y,
                            -1, -1,
                            GDK_RGB_DITHER_NORMAL,
                            0, 0);
        }

        /*
        gtk_paint_focus (widget->style,
                      widget->window,
                      state,
                      NULL,
                      widget,
                      NULL,
                      widget->allocation.x+3, widget->allocation.y+3,
                      widget->allocation.width-6, widget->allocation.height-6);
        */

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

static void
rstto_thumbnail_enter (GtkButton *button)
{
    gtk_widget_set_state (GTK_WIDGET (button), GTK_STATE_PRELIGHT);
    gtk_widget_queue_draw (GTK_WIDGET (button));
}

static void
rstto_thumbnail_leave (GtkButton *button)
{
    gtk_widget_set_state (GTK_WIDGET (button), GTK_STATE_NORMAL);
    gtk_widget_queue_draw (GTK_WIDGET (button));
}
