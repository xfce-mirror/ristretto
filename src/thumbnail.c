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

#include <config.h>
#include <gtk/gtk.h>
#include <gtk/gtkmarshal.h>
#include <string.h>

#include <libexif/exif-data.h>

#include "util.h"
#include "file.h"
#include "image_list.h"
#include "thumbnail.h"

struct _RsttoThumbnailPriv
{
    RsttoFile   *file;
    GdkPixbuf   *pixbuf;
    GdkPixbuf   *thumb_pixbuf;
    gchar       *thumbnail_path;
};

static GtkWidgetClass *parent_class = NULL;
static GdkPixbuf *thumbnail_missing_icon = NULL;

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

    if (thumbnail_missing_icon == NULL)
    {
        thumbnail_missing_icon = gtk_icon_theme_load_icon (gtk_icon_theme_get_default(),
                                                     "image-missing",
                                                     128,
                                                     0,
                                                     NULL);
    }


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
    if (thumb->priv->file)
    {
        g_object_unref (thumb->priv->file);
        thumb->priv->file = NULL;
    }

    if (thumb->priv->pixbuf)
    {
        g_object_unref (thumb->priv->pixbuf);
        thumb->priv->pixbuf = NULL;
    }

    if (thumb->priv->thumbnail_path)
    {
        g_free (thumb->priv->thumbnail_path);
        thumb->priv->thumbnail_path = NULL;
    }

    if (thumb->priv->thumb_pixbuf)
    {
        g_object_unref (thumb->priv->thumb_pixbuf);
        thumb->priv->thumb_pixbuf = NULL;
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
    GdkPixbuf *tmp_pixbuf = NULL;

    if (GTK_WIDGET_REALIZED (widget))
    {
        if (NULL == thumb->priv->thumb_pixbuf)
        {
            thumb->priv->thumb_pixbuf = gdk_pixbuf_new_from_file_at_scale (thumb->priv->thumbnail_path, 128, 128, TRUE, NULL);
        }
        if (NULL != thumb->priv->thumb_pixbuf)
        {
            thumb_pixbuf = thumb->priv->thumb_pixbuf;
            g_object_ref (thumb_pixbuf);
        }
        /* TODO: perform a check if we can open the real thumbnail */
        if (thumb_pixbuf == NULL)
        {
            thumb_pixbuf = thumbnail_missing_icon;
            g_object_ref (thumb_pixbuf);
        }

        if (thumb_pixbuf)
        {
            gint height = gdk_pixbuf_get_height (thumb->priv->pixbuf) - 10;
            gint width = gdk_pixbuf_get_width (thumb->priv->pixbuf) - 10;
            GdkPixbuf *dst_thumb_pixbuf = NULL;

            switch (rstto_file_get_orientation (thumb->priv->file))
            {
                case RSTTO_IMAGE_ORIENT_90:
                    tmp_pixbuf = gdk_pixbuf_rotate_simple (
                            thumb_pixbuf,
                            GDK_PIXBUF_ROTATE_CLOCKWISE);
                    break;
                case RSTTO_IMAGE_ORIENT_180:
                    tmp_pixbuf = gdk_pixbuf_rotate_simple (
                            thumb_pixbuf,
                            GDK_PIXBUF_ROTATE_UPSIDEDOWN);
                    break;
                case RSTTO_IMAGE_ORIENT_270:
                    tmp_pixbuf = gdk_pixbuf_rotate_simple (
                            thumb_pixbuf,
                            GDK_PIXBUF_ROTATE_COUNTERCLOCKWISE);
                    break;
                default:
                    tmp_pixbuf = thumb_pixbuf;
                    g_object_ref (tmp_pixbuf);
                    break;
            }

            if (gdk_pixbuf_get_width (tmp_pixbuf) > gdk_pixbuf_get_height (tmp_pixbuf))
            {
                height = (gint)(((gdouble)gdk_pixbuf_get_height (tmp_pixbuf) / (gdouble)gdk_pixbuf_get_width (tmp_pixbuf)) * width);
            }
            else
            {
                width = (gint)(((gdouble)gdk_pixbuf_get_width (tmp_pixbuf) / (gdouble)gdk_pixbuf_get_height (tmp_pixbuf)) * height);
            }

            gdk_pixbuf_fill (thumb->priv->pixbuf, 0x00000000);

            dst_thumb_pixbuf = gdk_pixbuf_scale_simple (tmp_pixbuf, width, height, GDK_INTERP_BILINEAR);

            g_object_unref (tmp_pixbuf);
            tmp_pixbuf = NULL;

            gdk_pixbuf_copy_area (dst_thumb_pixbuf,
                                  0, 0,
                                  width, height,
                                  thumb->priv->pixbuf,
                                  (gint)((gdouble)(gdk_pixbuf_get_width (thumb->priv->pixbuf) - width))/2,
                                  (gint)((gdouble)(gdk_pixbuf_get_height (thumb->priv->pixbuf) - height))/2
                                  );

            g_object_unref (dst_thumb_pixbuf);
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
}

GtkWidget *
rstto_thumbnail_new (RsttoFile *file)
{
    const gchar *file_uri;
    gchar *file_uri_checksum;
    gchar *filename;

    RsttoThumbnail *thumb;

    g_return_val_if_fail (file != NULL, NULL);

    thumb = g_object_new(RSTTO_TYPE_THUMBNAIL, NULL);

    thumb->priv->file = file ;
    g_object_ref (file);

    file_uri = rstto_file_get_uri (file);
    file_uri_checksum = g_compute_checksum_for_string (G_CHECKSUM_MD5, file_uri, strlen (file_uri));
    filename = g_strconcat (file_uri_checksum, ".png", NULL);

    thumb->priv->thumbnail_path = g_build_path ("/", g_get_home_dir(), ".thumbnails", "normal", filename, NULL);

    gtk_widget_set_tooltip_text(GTK_WIDGET(thumb), rstto_file_get_display_name (file));

    g_free (file_uri_checksum);
    g_free (filename);
    return GTK_WIDGET(thumb);
}

RsttoFile *
rstto_thumbnail_get_file (RsttoThumbnail *thumb)
{
    return thumb->priv->file;
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

void
rstto_thumbnail_update (RsttoThumbnail *thumb)
{
    if (thumb->priv->thumb_pixbuf)
    {
        g_object_unref (thumb->priv->thumb_pixbuf);
    }
    thumb->priv->thumb_pixbuf = gdk_pixbuf_new_from_file_at_scale (thumb->priv->thumbnail_path, 128, 128, TRUE, NULL);
    gtk_widget_queue_draw (GTK_WIDGET (thumb));
}
