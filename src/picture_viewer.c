/*
 *  Copyright (C) Stephan Arts 2006-2009 <stephan@xfce.org>
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
 *
 *  Drag-n-Drop support taken from Thunar, written by Benedict Meurer
 */

#include <config.h>
#include <gtk/gtk.h>
#include <gtk/gtkmarshal.h>
#include <string.h>
#include <gio/gio.h>
#include <libexif/exif-data.h>

#include "image.h"
#include "picture_viewer.h"

typedef enum
{
    RSTTO_PICTURE_VIEWER_STATE_NONE,
    RSTTO_PICTURE_VIEWER_STATE_MOVE,
    RSTTO_PICTURE_VIEWER_STATE_PREVIEW,
    RSTTO_PICTURE_VIEWER_STATE_BOX_ZOOM
} RsttoPictureViewerState;

enum
{
    TARGET_TEXT_URI_LIST,
};

static const GtkTargetEntry drop_targets[] = {
    {"text/uri-list", 0, TARGET_TEXT_URI_LIST},
};


struct _RsttoPictureViewerPriv
{
    RsttoImage *image;

    RsttoZoomMode    zoom_mode;

    GdkPixbuf        *dst_pixbuf; /* The pixbuf which ends up on screen */
    void             (*cb_value_changed)(GtkAdjustment *, RsttoPictureViewer *);
    GdkColor         *bg_color;

    gdouble          scale;

    struct
    {
        gdouble x;
        gdouble y;
        gdouble current_x;
        gdouble current_y;
        gint h_val;
        gint v_val;
    } motion;
    RsttoPictureViewerState state;
    struct
    {
        gint idle_id;
    } refresh;
    GtkMenu          *menu;
};

static void
rstto_picture_viewer_init(RsttoPictureViewer *);
static void
rstto_picture_viewer_class_init(RsttoPictureViewerClass *);
static void
rstto_picture_viewer_destroy(GtkObject *object);

static gdouble
rstto_picture_viewer_fit_scale(RsttoPictureViewer *viewer);

static void
cb_rstto_picture_viewer_image_updated (RsttoImage *image, RsttoPictureViewer *viewer);
static void
cb_rstto_picture_viewer_image_prepared (RsttoImage *image, RsttoPictureViewer *viewer);

static gboolean 
cb_rstto_picture_viewer_queued_repaint(RsttoPictureViewer *viewer);

static void
rstto_picture_viewer_size_request(GtkWidget *, GtkRequisition *);
static void
rstto_picture_viewer_size_allocate(GtkWidget *, GtkAllocation *);
static void
rstto_picture_viewer_realize(GtkWidget *);
static gboolean 
rstto_picture_viewer_expose(GtkWidget *, GdkEventExpose *);

static void
rstto_picture_viewer_paint (GtkWidget *widget);
static gboolean
rstto_picture_viewer_refresh(RsttoPictureViewer *viewer);

static gboolean
rstto_picture_viewer_set_scroll_adjustments(RsttoPictureViewer *, GtkAdjustment *, GtkAdjustment *);

static void
cb_rstto_picture_viewer_value_changed(GtkAdjustment *, RsttoPictureViewer *);
static void
cb_rstto_picture_viewer_scroll_event (RsttoPictureViewer *, GdkEventScroll *);
static gboolean 
cb_rstto_picture_viewer_motion_notify_event (RsttoPictureViewer *viewer,
                                             GdkEventMotion *event,
                                             gpointer user_data);
static void
cb_rstto_picture_viewer_button_press_event (RsttoPictureViewer *viewer, GdkEventButton *event);
static void
cb_rstto_picture_viewer_button_release_event (RsttoPictureViewer *viewer, GdkEventButton *event);
static void
cb_rstto_picture_viewer_popup_menu (RsttoPictureViewer *viewer, gboolean user_data);

static gboolean
rstto_picture_viewer_drag_drop (GtkWidget *widget,
                                GdkDragContext *context,
                                gint x,
                                gint y,
                                guint time);
static gboolean
rstto_picture_viewer_drag_motion (GtkWidget *widget,
                                GdkDragContext *context,
                                gint x,
                                gint y,
                                guint time);
static void
rstto_picture_viewer_drag_data_received();

static GtkWidgetClass *parent_class = NULL;

GType
rstto_picture_viewer_get_type ()
{
    static GType rstto_picture_viewer_type = 0;

    if (!rstto_picture_viewer_type)
    {
        static const GTypeInfo rstto_picture_viewer_info = 
        {
            sizeof (RsttoPictureViewerClass),
            (GBaseInitFunc) NULL,
            (GBaseFinalizeFunc) NULL,
            (GClassInitFunc) rstto_picture_viewer_class_init,
            (GClassFinalizeFunc) NULL,
            NULL,
            sizeof (RsttoPictureViewer),
            0,
            (GInstanceInitFunc) rstto_picture_viewer_init,
            NULL
        };

        rstto_picture_viewer_type = g_type_register_static (GTK_TYPE_WIDGET, "RsttoPictureViewer", &rstto_picture_viewer_info, 0);
    }
    return rstto_picture_viewer_type;
}

static void
rstto_picture_viewer_init(RsttoPictureViewer *viewer)
{
    viewer->priv = g_new0(RsttoPictureViewerPriv, 1);
    viewer->priv->cb_value_changed = cb_rstto_picture_viewer_value_changed;

    viewer->priv->dst_pixbuf = NULL;
    viewer->priv->zoom_mode = RSTTO_ZOOM_MODE_CUSTOM;
    gtk_widget_set_redraw_on_allocate(GTK_WIDGET(viewer), TRUE);
    gtk_widget_set_events (GTK_WIDGET(viewer),
                           GDK_BUTTON_PRESS_MASK |
                           GDK_BUTTON_RELEASE_MASK |
                           GDK_BUTTON1_MOTION_MASK);

    g_signal_connect(G_OBJECT(viewer), "scroll_event", G_CALLBACK(cb_rstto_picture_viewer_scroll_event), NULL);
    g_signal_connect(G_OBJECT(viewer), "button_press_event", G_CALLBACK(cb_rstto_picture_viewer_button_press_event), NULL);
    g_signal_connect(G_OBJECT(viewer), "button_release_event", G_CALLBACK(cb_rstto_picture_viewer_button_release_event), NULL);
    g_signal_connect(G_OBJECT(viewer), "motion_notify_event", G_CALLBACK(cb_rstto_picture_viewer_motion_notify_event), NULL);
    g_signal_connect(G_OBJECT(viewer), "popup-menu", G_CALLBACK(cb_rstto_picture_viewer_popup_menu), NULL);

    gtk_drag_dest_set(GTK_WIDGET(viewer), 0, drop_targets, G_N_ELEMENTS(drop_targets),
                      GDK_ACTION_COPY | GDK_ACTION_LINK | GDK_ACTION_MOVE | GDK_ACTION_PRIVATE);
}

/**
 * rstto_marshal_VOID__OBJECT_OBJECT:
 * @closure:
 * @return_value:
 * @n_param_values:
 * @param_values:
 * @invocation_hint:
 * @marshal_data:
 *
 * A marshaller for the set_scroll_adjustments signal.
 */
void
rstto_marshal_VOID__OBJECT_OBJECT (GClosure     *closure,
                                   GValue       *return_value,
                                   guint         n_param_values,
                                   const GValue *param_values,
                                   gpointer      invocation_hint,
                                   gpointer      marshal_data)
{
    typedef void (*GMarshalFunc_VOID__OBJECT_OBJECT) (gpointer data1,
                                                      gpointer arg_1,
                                                      gpointer arg_2,
                                                      gpointer data2);
    register GMarshalFunc_VOID__OBJECT_OBJECT callback;
    register GCClosure *cc = (GCClosure*) closure;
    register gpointer data1, data2;

    g_return_if_fail (n_param_values == 3);

    if (G_CCLOSURE_SWAP_DATA (closure))
    {
        data1 = closure->data;
        data2 = g_value_get_object (param_values + 0);
    }
    else
    {
        data1 = g_value_get_object (param_values + 0);
        data2 = closure->data;
    }
    callback = (GMarshalFunc_VOID__OBJECT_OBJECT) (marshal_data ?
    marshal_data : cc->callback);

    callback (data1,
              g_value_get_object (param_values + 1),
              g_value_get_object (param_values + 2),
              data2);
}

static void
rstto_picture_viewer_class_init(RsttoPictureViewerClass *viewer_class)
{
    GtkWidgetClass *widget_class;
    GtkObjectClass *object_class;

    widget_class = (GtkWidgetClass*)viewer_class;
    object_class = (GtkObjectClass*)viewer_class;

    parent_class = g_type_class_peek_parent(viewer_class);

    viewer_class->set_scroll_adjustments = rstto_picture_viewer_set_scroll_adjustments;

    widget_class->realize = rstto_picture_viewer_realize;
    widget_class->expose_event = rstto_picture_viewer_expose;
    widget_class->size_request = rstto_picture_viewer_size_request;
    widget_class->size_allocate = rstto_picture_viewer_size_allocate;
    widget_class->drag_drop = rstto_picture_viewer_drag_drop;
    widget_class->drag_motion = rstto_picture_viewer_drag_motion;
    widget_class->drag_data_received = rstto_picture_viewer_drag_data_received;

    object_class->destroy = rstto_picture_viewer_destroy;


    widget_class->set_scroll_adjustments_signal =
                  g_signal_new ("set_scroll_adjustments",
                                G_TYPE_FROM_CLASS (object_class),
                                G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
                                G_STRUCT_OFFSET (RsttoPictureViewerClass, set_scroll_adjustments),
                                NULL, NULL,
                                rstto_marshal_VOID__OBJECT_OBJECT,
                                G_TYPE_NONE, 2,
                                GTK_TYPE_ADJUSTMENT,
                                GTK_TYPE_ADJUSTMENT);
}

static void
rstto_picture_viewer_size_request(GtkWidget *widget, GtkRequisition *requisition)
{
    requisition->width = 300;
    requisition->height= 400;
}

static void
rstto_picture_viewer_size_allocate(GtkWidget *widget, GtkAllocation *allocation)
{
    RsttoPictureViewer *viewer = RSTTO_PICTURE_VIEWER(widget);
    gint border_width =  0;
    widget->allocation = *allocation;

    if (GTK_WIDGET_REALIZED (widget))
    {
         gdk_window_move_resize (widget->window,
            allocation->x + border_width,
            allocation->y + border_width,
            allocation->width - border_width * 2,
            allocation->height - border_width * 2);
    }

    rstto_picture_viewer_refresh(viewer);
    rstto_picture_viewer_paint (GTK_WIDGET(viewer));
}

static void
rstto_picture_viewer_realize(GtkWidget *widget)
{
    g_return_if_fail (widget != NULL);
    g_return_if_fail (RSTTO_IS_PICTURE_VIEWER(widget));

    GdkWindowAttr attributes;
    gint attributes_mask;

    GTK_WIDGET_SET_FLAGS (widget, GTK_REALIZED);

    attributes.x = widget->allocation.x;
    attributes.y = widget->allocation.y;
    attributes.width = widget->allocation.width;
    attributes.height = widget->allocation.height;
    attributes.wclass = GDK_INPUT_OUTPUT;
    attributes.window_type = GDK_WINDOW_CHILD;
    attributes.event_mask = gtk_widget_get_events (widget) | 
    GDK_EXPOSURE_MASK | GDK_BUTTON_PRESS_MASK;
    attributes.visual = gtk_widget_get_visual (widget);
    attributes.colormap = gtk_widget_get_colormap (widget);

    attributes_mask = GDK_WA_X | GDK_WA_Y | GDK_WA_VISUAL | GDK_WA_COLORMAP;
    widget->window = gdk_window_new (gtk_widget_get_parent_window(widget), &attributes, attributes_mask);

    widget->style = gtk_style_attach (widget->style, widget->window);
    gdk_window_set_user_data (widget->window, widget);

    gtk_style_set_background (widget->style, widget->window, GTK_STATE_ACTIVE);
}

static gboolean
rstto_picture_viewer_expose(GtkWidget *widget, GdkEventExpose *event)
{
    rstto_picture_viewer_refresh (RSTTO_PICTURE_VIEWER(widget));
    rstto_picture_viewer_paint (widget);
    return FALSE;
}

static void
rstto_picture_viewer_paint (GtkWidget *widget)
{
    RsttoPictureViewer *viewer = RSTTO_PICTURE_VIEWER(widget);
    GdkPixbuf *pixbuf = viewer->priv->dst_pixbuf;
    GdkColor color;
    GdkColor line_color;

    color.pixel = 0x0;
    line_color.pixel = 0x0;

    gint i, a, height, width;

    /* required for transparent pixbufs... add double buffering to fix flickering*/
    if(GTK_WIDGET_REALIZED(widget))
    {          
        GdkPixmap *buffer = gdk_pixmap_new(NULL, widget->allocation.width, widget->allocation.height, gdk_drawable_get_depth(widget->window));
        GdkGC *gc = gdk_gc_new(GDK_DRAWABLE(buffer));

        if (viewer->priv->bg_color)
        {
            gdk_gc_set_foreground(gc, viewer->priv->bg_color);
        }
        else
        {
            gdk_gc_set_foreground(gc, &(widget->style->bg[GTK_STATE_NORMAL]));
        }
        gdk_draw_rectangle(GDK_DRAWABLE(buffer), gc, TRUE, 0, 0, widget->allocation.width, widget->allocation.height);
        if(pixbuf)
        {
            gint x1 = (widget->allocation.width-gdk_pixbuf_get_width(pixbuf))<0?0:(widget->allocation.width-gdk_pixbuf_get_width(pixbuf))/2;
            gint y1 = (widget->allocation.height-gdk_pixbuf_get_height(pixbuf))<0?0:(widget->allocation.height-gdk_pixbuf_get_height(pixbuf))/2;
            gint x2 = gdk_pixbuf_get_width(pixbuf);
            gint y2 = gdk_pixbuf_get_height(pixbuf);
            
            /* We only need to paint a checkered background if the image is transparent */
            if(gdk_pixbuf_get_has_alpha(pixbuf))
            {
                for(i = 0; i <= x2/10; i++)
                {
                    if(i == x2/10)
                    {
                        width = x2-10*i;
                    }
                    else
                    {   
                        width = 10;
                    }
                    for(a = 0; a <= y2/10; a++)
                    {
                        if(a%2?i%2:!(i%2))
                            color.pixel = 0xcccccccc;
                        else
                            color.pixel = 0xdddddddd;
                        gdk_gc_set_foreground(gc, &color);
                        if(a == y2/10)
                        {
                            height = y2-10*a;
                        }
                        else
                        {   
                            height = 10;
                        }

                        gdk_draw_rectangle(GDK_DRAWABLE(buffer),
                                        gc,
                                        TRUE,
                                        x1+10*i,
                                        y1+10*a,
                                        width,
                                        height);
                    }
                }
            }
            gdk_draw_pixbuf(GDK_DRAWABLE(buffer), 
                            NULL, 
                            pixbuf,
                            0,
                            0,
                            x1,
                            y1,
                            x2, 
                            y2,
                            GDK_RGB_DITHER_NONE,
                            0,0);
            if(viewer->priv->state == RSTTO_PICTURE_VIEWER_STATE_BOX_ZOOM)
            {
                gdk_gc_set_foreground(gc,
                        &(widget->style->fg[GTK_STATE_SELECTED]));
                gdouble m_x1, m_x2, m_y1, m_y2;

                if (viewer->priv->motion.x < viewer->priv->motion.current_x)
                {
                    m_x1 = viewer->priv->motion.x;
                    m_x2 = viewer->priv->motion.current_x;
                }
                else
                {
                    m_x1 = viewer->priv->motion.current_x;
                    m_x2 = viewer->priv->motion.x;
                }
                if (viewer->priv->motion.y < viewer->priv->motion.current_y)
                {
                    m_y1 = viewer->priv->motion.y;
                    m_y2 = viewer->priv->motion.current_y;
                }
                else
                {
                    m_y1 = viewer->priv->motion.current_y;
                    m_y2 = viewer->priv->motion.y;
                }
                if (m_y1 < y1)
                    m_y1 = y1;
                if (m_x1 < x1)
                    m_x1 = x1;

                if (m_x2 > x2 + x1)
                    m_x2 = x2 + x1;
                if (m_y2 > y2 + y1)
                    m_y2 = y2 + y1;

                if ((m_x2 - m_x1 >= 2) && (m_y2 - m_y1 >= 2))
                {
                    GdkPixbuf *sub = gdk_pixbuf_new_subpixbuf(pixbuf,
                                                              m_x1-x1,
                                                              m_y1-y1,
                                                              m_x2-m_x1,
                                                              m_y2-m_y1);
                    if(sub)
                    {
                        sub = gdk_pixbuf_composite_color_simple(sub,
                                                          m_x2-m_x1,
                                                          m_y2-m_y1,
                                                          GDK_INTERP_BILINEAR,
                                                          200,
                                                          200,
                                                          widget->style->bg[GTK_STATE_SELECTED].pixel,
                                                          widget->style->bg[GTK_STATE_SELECTED].pixel);

                        gdk_draw_pixbuf(GDK_DRAWABLE(buffer),
                                        gc,
                                        sub,
                                        0,0,
                                        m_x1,
                                        m_y1,
                                        -1, -1,
                                        GDK_RGB_DITHER_NONE,
                                        0, 0);

                        gdk_pixbuf_unref(sub);
                        sub = NULL;
                    }
                }

                gdk_draw_rectangle(GDK_DRAWABLE(buffer),
                                gc,
                                FALSE,
                                m_x1,
                                m_y1,
                                m_x2 - m_x1,
                                m_y2 - m_y1);
            }

        }
        gdk_draw_drawable(GDK_DRAWABLE(widget->window), 
                        gdk_gc_new(widget->window), 
                        buffer,
                        0,
                        0,
                        0,
                        0,
                        widget->allocation.width,
                        widget->allocation.height);
        g_object_unref(buffer);
   }
}

static void
rstto_picture_viewer_destroy(GtkObject *object)
{

}

static gboolean  
rstto_picture_viewer_set_scroll_adjustments(RsttoPictureViewer *viewer, GtkAdjustment *hadjustment, GtkAdjustment *vadjustment)
{
    if(viewer->hadjustment)
    {
        g_signal_handlers_disconnect_by_func(viewer->hadjustment, viewer->priv->cb_value_changed, viewer);
        g_object_unref(viewer->hadjustment);
    }
    if(viewer->vadjustment)
    {
        g_signal_handlers_disconnect_by_func(viewer->vadjustment, viewer->priv->cb_value_changed, viewer);
        g_object_unref(viewer->vadjustment);
    }

    viewer->hadjustment = hadjustment;
    viewer->vadjustment = vadjustment;

    if(viewer->hadjustment)
    {
        g_signal_connect(G_OBJECT(viewer->hadjustment), "value-changed", (GCallback)viewer->priv->cb_value_changed, viewer);
        g_object_ref(viewer->hadjustment);
    }
    if(viewer->vadjustment)
    {
        g_signal_connect(G_OBJECT(viewer->vadjustment), "value-changed", (GCallback)viewer->priv->cb_value_changed, viewer);
        g_object_ref(viewer->vadjustment);
    }
    return TRUE;
}

static void
cb_rstto_picture_viewer_value_changed(GtkAdjustment *adjustment, RsttoPictureViewer *viewer)
{
    if (viewer->priv->refresh.idle_id > 0)
    {
        g_source_remove(viewer->priv->refresh.idle_id);
    }
    viewer->priv->refresh.idle_id = g_idle_add((GSourceFunc)cb_rstto_picture_viewer_queued_repaint, viewer);
}

GtkWidget *
rstto_picture_viewer_new()
{
    GtkWidget *widget;

    widget = g_object_new(RSTTO_TYPE_PICTURE_VIEWER, NULL);

    return widget;
}

void
rstto_picture_viewer_set_scale(RsttoPictureViewer *viewer, gdouble scale)
{
    gdouble *old_scale, *new_scale;
    GdkPixbuf *src_pixbuf = NULL;
    gboolean *fit_to_screen = NULL;

    if (viewer->priv->image)
    {
        src_pixbuf = rstto_image_get_pixbuf (viewer->priv->image);
        old_scale = g_object_get_data (G_OBJECT (viewer->priv->image), "viewer-scale");
        fit_to_screen = g_object_get_data (G_OBJECT (viewer->priv->image), "viewer-fit-to-screen");
        rstto_picture_viewer_set_zoom_mode (viewer, RSTTO_ZOOM_MODE_CUSTOM);

        new_scale = g_new0 (gdouble, 1);
        *new_scale = scale;
        *fit_to_screen = FALSE;
        g_object_set_data (G_OBJECT (viewer->priv->image), "viewer-scale", new_scale);
        g_object_set_data (G_OBJECT (viewer->priv->image), "viewer-fit-to-screen", fit_to_screen);

        gdouble width = (gdouble)gdk_pixbuf_get_width (src_pixbuf);
        gdouble height = (gdouble)gdk_pixbuf_get_height (src_pixbuf);


        viewer->hadjustment->upper = width * (*new_scale);
        gtk_adjustment_changed(viewer->hadjustment);

        viewer->vadjustment->upper = height * (*new_scale);
        gtk_adjustment_changed(viewer->vadjustment);

        viewer->hadjustment->value = (((viewer->hadjustment->value +
                                      (viewer->hadjustment->page_size / 2)) *
                                       (*new_scale)) / (*old_scale)) - (viewer->hadjustment->page_size / 2);
        viewer->vadjustment->value = (((viewer->vadjustment->value +
                                      (viewer->vadjustment->page_size / 2)) *
                                       (*new_scale)) / (*old_scale)) - (viewer->vadjustment->page_size / 2);

        if((viewer->hadjustment->value + viewer->hadjustment->page_size) > viewer->hadjustment->upper)
        {
            viewer->hadjustment->value = viewer->hadjustment->upper - viewer->hadjustment->page_size;
        }
        if(viewer->hadjustment->value < viewer->hadjustment->lower)
        {
            viewer->hadjustment->value = viewer->hadjustment->lower;
        }
        if((viewer->vadjustment->value + viewer->vadjustment->page_size) > viewer->vadjustment->upper)
        {
            viewer->vadjustment->value = viewer->vadjustment->upper - viewer->vadjustment->page_size;
        }
        if(viewer->vadjustment->value < viewer->vadjustment->lower)
        {
            viewer->vadjustment->value = viewer->vadjustment->lower;
        }

        gtk_adjustment_value_changed(viewer->hadjustment);
        gtk_adjustment_value_changed(viewer->vadjustment);

        g_free (old_scale);

    }
}

/**
 * rstto_picture_viewer_fit_scale:
 * @viewer:
 *
 */
static gdouble
rstto_picture_viewer_fit_scale(RsttoPictureViewer *viewer)
{
    gboolean *fit_to_screen;
    if (viewer->priv->image)
    {
        fit_to_screen = g_new0(gboolean, 1);
        g_object_set_data (G_OBJECT (viewer->priv->image), "viewer-fit-to-screen", fit_to_screen);

        if(rstto_picture_viewer_refresh(viewer))
        {
            rstto_picture_viewer_paint(GTK_WIDGET(viewer));
        }
    }
    return 0;
}

gdouble
rstto_picture_viewer_get_scale(RsttoPictureViewer *viewer)
{
    gdouble *scale;
    if (viewer->priv->image)
    {
        scale = g_object_get_data (G_OBJECT (viewer->priv->image), "viewer-scale");
        return *scale;
    }
    return 0;
}

static gboolean
rstto_picture_viewer_refresh(RsttoPictureViewer *viewer)
{
    GtkWidget *widget = GTK_WIDGET(viewer);
    GdkPixbuf *src_pixbuf = NULL, *tmp_pixbuf = NULL;
    GdkPixbuf *thumb_pixbuf = NULL;
    gboolean *fit_to_screen = NULL;
    gdouble *scale = NULL; 
    gint width = 0, height = 0;
    gboolean vadjustment_changed = FALSE;
    gboolean hadjustment_changed = FALSE;

    /**
     * Get all the required image peripherals
     */
    if (viewer->priv->image != NULL)
    {   
        src_pixbuf = rstto_image_get_pixbuf (viewer->priv->image);
        thumb_pixbuf = rstto_image_get_thumbnail (viewer->priv->image);

        width = gdk_pixbuf_get_width(src_pixbuf);
        height = gdk_pixbuf_get_height(src_pixbuf);

        fit_to_screen = g_object_get_data (G_OBJECT (viewer->priv->image), "viewer-fit-to-screen");
        scale         = g_object_get_data (G_OBJECT (viewer->priv->image), "viewer-scale");
    }

    if (scale == NULL)
        scale = g_new0 (gdouble, 1);

    if (fit_to_screen == NULL)
        fit_to_screen = g_new0 (gboolean, 1);

    /**
     * Clean up the destination pixbuf, since we are going to re-create it
     * anyway
     */
    if(viewer->priv->dst_pixbuf)
    {
        g_object_unref(viewer->priv->dst_pixbuf);
        viewer->priv->dst_pixbuf = NULL;
    }

    /**
     * If Scale == 0, this means the image has not been rendered before.
     * When this is the case, we render the image at 100% unless it is
     * larger thenthe dimensions of the picture viewer.
     */
    if ((*scale == 0) && (viewer->priv->image))
    {
        /* Check if the image fits inside the viewer,
         * if not, scale it down to fit
         */
        if ((GTK_WIDGET (viewer)->allocation.width < width) ||
            (GTK_WIDGET (viewer)->allocation.height < height))
        {
            *fit_to_screen = TRUE;
            /* The image does not fit the picture-viewer, and 
             * we decided to scale it down to fit. Now we need to check
             * which side we need to use as a reference.
             *
             * We use the one that produces a larger scale difference
             * to the viewer. This way we know the image will always fit.
             */
            if ((GTK_WIDGET (viewer)->allocation.width / width) >
                (GTK_WIDGET (viewer)->allocation.height / height))
            {
                *scale = (gdouble)GTK_WIDGET (viewer)->allocation.width / (gdouble)width;
            }
            else
            {
                *scale = (gdouble)GTK_WIDGET (viewer)->allocation.height / (gdouble)height;
            }
            
        }
        else
        {
            /* The image is smaller then the picture-viewer,
             * As a result, view it at it's original size.
             */
            *scale = 1.0;
            *fit_to_screen = FALSE;
        }
        g_object_set_data (G_OBJECT (viewer->priv->image), "viewer-scale", scale);
        g_object_set_data (G_OBJECT (viewer->priv->image), "viewer-fit-to-screen", fit_to_screen);

    }
    else
    {
        switch (viewer->priv->zoom_mode)
        {
            case RSTTO_ZOOM_MODE_FIT:
                *fit_to_screen = TRUE;
                g_object_set_data (G_OBJECT (viewer->priv->image), "viewer-fit-to-screen", fit_to_screen);
                break;
            case RSTTO_ZOOM_MODE_100:
                *fit_to_screen = FALSE;
                *scale = 1.0;
                g_object_set_data (G_OBJECT (viewer->priv->image), "viewer-scale", scale);
                break;
            case RSTTO_ZOOM_MODE_CUSTOM:
                break;
        }

        if(*fit_to_screen)
        {
            /* The image does not fit the picture-viewer, and 
             * we decided to scale it down to fit. Now we need to check
             * which side we need to use as a reference.
             *
             * We use the one that produces a larger scale difference
             * to the viewer. This way we know the image will always fit.
             */
            if ((GTK_WIDGET (viewer)->allocation.width / width) >
                (GTK_WIDGET (viewer)->allocation.height / height))
            {
                *scale = (gdouble)GTK_WIDGET (viewer)->allocation.width / (gdouble)width;
            }
            else
            {
                *scale = (gdouble)GTK_WIDGET (viewer)->allocation.height / (gdouble)height;
            }

            g_object_set_data (G_OBJECT (viewer->priv->image), "viewer-scale", scale);
        }
    }

    if (GTK_WIDGET_REALIZED (widget))
    {
        if (viewer->hadjustment && viewer->hadjustment)
        {
            viewer->hadjustment->page_size = widget->allocation.width;
            viewer->hadjustment->upper = width * (*scale);
            viewer->hadjustment->lower = 0;

            if((viewer->hadjustment->value + viewer->hadjustment->page_size) > viewer->hadjustment->upper)
            {
                viewer->hadjustment->value = viewer->hadjustment->upper - viewer->hadjustment->page_size;
                hadjustment_changed = TRUE;
            }
            if(viewer->hadjustment->value < viewer->hadjustment->lower)
            {
                viewer->hadjustment->value = viewer->hadjustment->lower;
                hadjustment_changed = TRUE;
            }

            viewer->vadjustment->page_size = widget->allocation.height;
            viewer->vadjustment->upper = height * (*scale);
            viewer->vadjustment->lower = 0;

            if((viewer->vadjustment->value + viewer->vadjustment->page_size) > viewer->vadjustment->upper)
            {
                viewer->vadjustment->value = viewer->vadjustment->upper - viewer->vadjustment->page_size;
                vadjustment_changed = TRUE;
            }
            if(viewer->vadjustment->value < viewer->vadjustment->lower)
            {
                viewer->vadjustment->value = viewer->vadjustment->lower;
                vadjustment_changed = TRUE;
            }

        }

        if (viewer->priv->state == RSTTO_PICTURE_VIEWER_STATE_PREVIEW)
        {

            viewer->priv->dst_pixbuf = gdk_pixbuf_scale_simple (thumb_pixbuf,
                                                            (gint)((gdouble)width * (*scale)),
                                                            (gint)((gdouble)height * (*scale)),
                                                            GDK_INTERP_BILINEAR);
        }
        else
        {
            if (src_pixbuf)
            {
                tmp_pixbuf = gdk_pixbuf_new_subpixbuf(src_pixbuf,
                                                   (gint)(viewer->hadjustment->value / (*scale)), 
                                                          viewer->vadjustment->value / (*scale),
                                                        ((widget->allocation.width/(*scale))) < (gdouble)width?
                                                          widget->allocation.width/(*scale):(gdouble)width,
                                                        ((widget->allocation.height/(*scale)))< (gdouble)height?
                                                          widget->allocation.height/(*scale):(gdouble)height);
                if(tmp_pixbuf)
                {
                    gint dst_width = (gdouble)gdk_pixbuf_get_width(tmp_pixbuf)*(*scale);
                    gint dst_height = (gdouble)gdk_pixbuf_get_height(tmp_pixbuf)*(*scale);
                    viewer->priv->dst_pixbuf = gdk_pixbuf_scale_simple(tmp_pixbuf,
                                            dst_width>0?dst_width:1,
                                            dst_height>0?dst_height:1,
                                            GDK_INTERP_BILINEAR);
                    g_object_unref(tmp_pixbuf);
                    tmp_pixbuf = NULL;
                }
            }

        }
        if (viewer->vadjustment && viewer->hadjustment)
        {
            gtk_adjustment_changed(viewer->hadjustment);
            gtk_adjustment_changed(viewer->vadjustment);
        }
        if (hadjustment_changed == TRUE)
            gtk_adjustment_value_changed(viewer->hadjustment);
        if (vadjustment_changed == TRUE)
            gtk_adjustment_value_changed(viewer->vadjustment);
    }
    return TRUE;
}

static void
cb_rstto_picture_viewer_scroll_event (RsttoPictureViewer *viewer, GdkEventScroll *event)
{
    /*
    RsttoNavigatorEntry *entry = rstto_navigator_get_file(viewer->priv->navigator);

    if (entry == NULL)
    {
        return;
    }

    gdouble scale = rstto_navigator_entry_get_scale(entry);
    viewer->priv->zoom_mode = RSTTO_ZOOM_MODE_CUSTOM;
    switch(event->direction)
    {
        case GDK_SCROLL_UP:
        case GDK_SCROLL_LEFT:
            if (scale <= 0.05)
                return;
            if (viewer->priv->refresh.idle_id > 0)
            {
                g_source_remove(viewer->priv->refresh.idle_id);
            }
            rstto_navigator_entry_set_scale(entry, scale / 1.1);
            rstto_navigator_entry_set_fit_to_screen (entry, FALSE);

            viewer->vadjustment->value = ((viewer->vadjustment->value + event->y) / 1.1) - event->y;
            viewer->hadjustment->value = ((viewer->hadjustment->value + event->x) / 1.1) - event->x;

            viewer->priv->refresh.idle_id = g_idle_add((GSourceFunc)cb_rstto_picture_viewer_queued_repaint, viewer);
            break;
        case GDK_SCROLL_DOWN:
        case GDK_SCROLL_RIGHT:
            if (scale >= 16)
                return;
            if (viewer->priv->refresh.idle_id > 0)
            {
                g_source_remove(viewer->priv->refresh.idle_id);
            }
            rstto_navigator_entry_set_scale(entry, scale * 1.1);
            rstto_navigator_entry_set_fit_to_screen (entry, FALSE);


            viewer->vadjustment->value = ((viewer->vadjustment->value + event->y) * 1.1) - event->y;
            viewer->hadjustment->value = ((viewer->hadjustment->value + event->x) * 1.1) - event->x;

            gtk_adjustment_value_changed(viewer->hadjustment);
            gtk_adjustment_value_changed(viewer->vadjustment);

            viewer->priv->refresh.idle_id = g_idle_add((GSourceFunc)cb_rstto_picture_viewer_queued_repaint, viewer);
            break;
    }
    */
}

static gboolean 
cb_rstto_picture_viewer_motion_notify_event (RsttoPictureViewer *viewer,
                                             GdkEventMotion *event,
                                             gpointer user_data)
{
    if (event->state & GDK_BUTTON1_MASK)
    {
        viewer->priv->motion.current_x = event->x;
        viewer->priv->motion.current_y = event->y;

        if (viewer->priv->refresh.idle_id > 0)
        {
            g_source_remove(viewer->priv->refresh.idle_id);
            viewer->priv->refresh.idle_id = 0;
        }

        switch (viewer->priv->state)
        {
            case RSTTO_PICTURE_VIEWER_STATE_MOVE:
                if (viewer->priv->motion.x != viewer->priv->motion.current_x)
                {
                    gint val = viewer->hadjustment->value;
                    viewer->hadjustment->value = viewer->priv->motion.h_val + (viewer->priv->motion.x - viewer->priv->motion.current_x);
                    if((viewer->hadjustment->value + viewer->hadjustment->page_size) > viewer->hadjustment->upper)
                    {
                        viewer->hadjustment->value = viewer->hadjustment->upper - viewer->hadjustment->page_size;
                    }
                    if((viewer->hadjustment->value) < viewer->hadjustment->lower)
                    {
                        viewer->hadjustment->value = viewer->hadjustment->lower;
                    }
                    if (val != viewer->hadjustment->value)
                        gtk_adjustment_value_changed(viewer->hadjustment);
                }

                if (viewer->priv->motion.y != viewer->priv->motion.current_y)
                {
                    gint val = viewer->vadjustment->value;
                    viewer->vadjustment->value = viewer->priv->motion.v_val + (viewer->priv->motion.y - viewer->priv->motion.current_y);
                    if((viewer->vadjustment->value + viewer->vadjustment->page_size) > viewer->vadjustment->upper)
                    {
                        viewer->vadjustment->value = viewer->vadjustment->upper - viewer->vadjustment->page_size;
                    }
                    if((viewer->vadjustment->value) < viewer->vadjustment->lower)
                    {
                        viewer->vadjustment->value = viewer->vadjustment->lower;
                    }
                    if (val != viewer->vadjustment->value)
                        gtk_adjustment_value_changed(viewer->vadjustment);
                }
                break;
            case RSTTO_PICTURE_VIEWER_STATE_BOX_ZOOM:
                viewer->priv->refresh.idle_id = g_idle_add((GSourceFunc)cb_rstto_picture_viewer_queued_repaint, viewer);
                break;
            default:
                break;
        }
    }
    return FALSE;
}

static gboolean 
cb_rstto_picture_viewer_queued_repaint(RsttoPictureViewer *viewer)
{
    rstto_picture_viewer_refresh(viewer);
    rstto_picture_viewer_paint(GTK_WIDGET(viewer));

    g_source_remove(viewer->priv->refresh.idle_id);
    viewer->priv->refresh.idle_id = -1;
    return FALSE;
}

static void
cb_rstto_picture_viewer_button_press_event (RsttoPictureViewer *viewer, GdkEventButton *event)
{
    /*
    if(event->button == 1)
    {
        viewer->priv->motion.x = event->x;
        viewer->priv->motion.y = event->y;
        viewer->priv->motion.current_x = event->x;
        viewer->priv->motion.current_y = event->y;
        viewer->priv->motion.h_val = viewer->hadjustment->value;
        viewer->priv->motion.v_val = viewer->vadjustment->value;

        if (rstto_navigator_get_file(viewer->priv->navigator) != NULL)
        {

            if (!(event->state & (GDK_CONTROL_MASK)))
            {
                GtkWidget *widget = GTK_WIDGET(viewer);
                GdkCursor *cursor = gdk_cursor_new(GDK_FLEUR);
                gdk_window_set_cursor(widget->window, cursor);
                gdk_cursor_unref(cursor);

                viewer->priv->motion.state = RSTTO_PICTURE_VIEWER_STATE_MOVE;
            }

            if (event->state & GDK_CONTROL_MASK)
            {
                GtkWidget *widget = GTK_WIDGET(viewer);
                GdkCursor *cursor = gdk_cursor_new(GDK_UL_ANGLE);
                gdk_window_set_cursor(widget->window, cursor);
                gdk_cursor_unref(cursor);

                viewer->priv->motion.state = RSTTO_PICTURE_VIEWER_STATE_BOX_ZOOM;
            }
        }

        
    }
    */
    if(event->button == 3)
    {
        if (viewer->priv->menu)
        {
            gtk_widget_show_all(GTK_WIDGET(viewer->priv->menu));
            gtk_menu_popup(viewer->priv->menu,
                           NULL,
                           NULL,
                           NULL,
                           NULL,
                           3,
                           event->time);
        }
    }
}

static void
cb_rstto_picture_viewer_button_release_event (RsttoPictureViewer *viewer, GdkEventButton *event)
{
    /*
    if(event->button == 1)
    {
        GtkWidget *widget = GTK_WIDGET(viewer);
        gdk_window_set_cursor(widget->window, NULL);
        switch (viewer->priv->motion.state)
        {
            case RSTTO_PICTURE_VIEWER_STATE_NONE:
                break;
            case RSTTO_PICTURE_VIEWER_STATE_MOVE:
                break;
            case RSTTO_PICTURE_VIEWER_STATE_BOX_ZOOM:
                viewer->priv->zoom_mode = RSTTO_ZOOM_MODE_CUSTOM;
                if(GTK_WIDGET_REALIZED(widget))
                {
                    RsttoNavigatorEntry *entry = rstto_navigator_get_file(viewer->priv->navigator);
                    gdouble scale = rstto_navigator_entry_get_scale(entry);
                    gdouble old_scale = scale;
                    gdouble width = (gdouble)gdk_pixbuf_get_width(viewer->priv->src_pixbuf);
                    gdouble height = (gdouble)gdk_pixbuf_get_height(viewer->priv->src_pixbuf);

                    gdouble d_width = (gdouble)gdk_pixbuf_get_width(viewer->priv->dst_pixbuf);
                    gdouble d_height = (gdouble)gdk_pixbuf_get_height(viewer->priv->dst_pixbuf);

                    gdouble box_width, box_height;
                    gdouble top_left_x, top_left_y;

                    if (viewer->priv->motion.x < viewer->priv->motion.current_x)
                    {
                        gint x_offset = (widget->allocation.width - d_width)<=0?0:((widget->allocation.width - d_width)/2);
                        top_left_x = viewer->priv->motion.x + viewer->hadjustment->value - x_offset;
                        box_width = viewer->priv->motion.current_x - viewer->priv->motion.x;
                    }
                    else
                    {
                        gint x_offset = (widget->allocation.width - d_width)<=0?0:((widget->allocation.width - d_width)/2);
                        top_left_x = viewer->priv->motion.current_x + viewer->hadjustment->value - x_offset;
                        box_width = viewer->priv->motion.x - viewer->priv->motion.current_x;
                    }
                    if (viewer->priv->motion.y < viewer->priv->motion.current_y)
                    {
                        gint y_offset = (widget->allocation.height - d_height)<=0?0:((widget->allocation.height - d_height)/2);
                        top_left_y = viewer->priv->motion.y + viewer->vadjustment->value - y_offset;
                        box_height = viewer->priv->motion.current_y - viewer->priv->motion.y;
                    }
                    else
                    {
                        gint y_offset = (widget->allocation.height - d_height) <=0?0:((widget->allocation.height - d_height)/2);

                        top_left_y = viewer->priv->motion.current_y + viewer->vadjustment->value - y_offset;
                        box_height = viewer->priv->motion.y - viewer->priv->motion.current_y;
                    }

                    gdouble h_scale = widget->allocation.width / box_width * scale;
                    gdouble v_scale = widget->allocation.height / box_height * scale;

                     //Check if the mouse has been moved (and there exists a box
                    if (box_height > 1 && box_width > 1)
                    {
                        if (h_scale < v_scale)
                        {
                            rstto_navigator_entry_set_scale(entry, h_scale);
                            gdouble d_box_height = box_height * v_scale / h_scale;
                            top_left_y -= (d_box_height - box_height) / 2;
                            box_height = d_box_height;
                        }
                        else
                        {
                            rstto_navigator_entry_set_scale(entry, v_scale);
                            gdouble d_box_width = box_width * h_scale / v_scale;
                            top_left_x -= (d_box_width - box_width) / 2;
                            box_width = d_box_width;
                        }

                        rstto_navigator_entry_set_fit_to_screen(entry, FALSE);
                        scale = rstto_navigator_entry_get_scale(entry);

                        if(viewer->hadjustment)
                        {
                            viewer->hadjustment->page_size = box_width / old_scale * scale;
                            viewer->hadjustment->upper = width * scale;
                            viewer->hadjustment->lower = 0;
                            viewer->hadjustment->step_increment = 1;
                            viewer->hadjustment->page_increment = 100;
                            viewer->hadjustment->value = top_left_x / old_scale * scale;
                            if((viewer->hadjustment->value + viewer->hadjustment->page_size) > viewer->hadjustment->upper)
                            {
                                viewer->hadjustment->value = viewer->hadjustment->upper - viewer->hadjustment->page_size;
                            }
                            if(viewer->hadjustment->value < viewer->hadjustment->lower)
                            {
                                viewer->hadjustment->value = viewer->hadjustment->lower;
                            }
                            gtk_adjustment_changed(viewer->hadjustment);
                            gtk_adjustment_value_changed(viewer->hadjustment);
                        }
                        if(viewer->vadjustment)
                        {
                            viewer->vadjustment->page_size = box_height /old_scale* scale;
                            viewer->vadjustment->upper = height * scale;
                            viewer->vadjustment->lower = 0;
                            viewer->vadjustment->step_increment = 1;
                            viewer->vadjustment->page_increment = 100;
                            viewer->vadjustment->value = top_left_y / old_scale * scale;
                            if((viewer->vadjustment->value + viewer->vadjustment->page_size) > viewer->vadjustment->upper)
                            {
                                viewer->vadjustment->value = viewer->vadjustment->upper - viewer->vadjustment->page_size;
                            }
                            if(viewer->vadjustment->value < viewer->vadjustment->lower)
                            {
                                viewer->vadjustment->value = viewer->vadjustment->lower;
                            }
                            gtk_adjustment_changed(viewer->vadjustment);
                            gtk_adjustment_value_changed(viewer->vadjustment);
                        }
                    }
                }
                if (viewer->priv->refresh.idle_id > 0)
                {
                    g_source_remove(viewer->priv->refresh.idle_id);
                }
                viewer->priv->refresh.idle_id = g_idle_add((GSourceFunc)cb_rstto_picture_viewer_queued_repaint, viewer);
                break;
        }

    }

    viewer->priv->motion.state = RSTTO_PICTURE_VIEWER_STATE_NONE;
    */

}

static void
cb_rstto_picture_viewer_popup_menu (RsttoPictureViewer *viewer, gboolean user_data)
{
    if (viewer->priv->menu)
    {
        gtk_widget_show_all(GTK_WIDGET(viewer->priv->menu));
        gtk_menu_popup(viewer->priv->menu,
                       NULL,
                       NULL,
                       NULL,
                       NULL,
                       0,
                       gtk_get_current_event_time());
    }
}

void
rstto_picture_viewer_set_menu (RsttoPictureViewer *viewer, GtkMenu *menu)
{
    if (viewer->priv->menu)
    {
        gtk_menu_detach (viewer->priv->menu);
        gtk_widget_destroy (GTK_WIDGET(viewer->priv->menu));
    }

    viewer->priv->menu = menu;

    if (viewer->priv->menu)
    {
        gtk_menu_attach_to_widget (viewer->priv->menu, GTK_WIDGET(viewer), NULL);
    }
}

void
rstto_picture_viewer_set_zoom_mode(RsttoPictureViewer *viewer, RsttoZoomMode mode)
{
    viewer->priv->zoom_mode = mode;
    switch (viewer->priv->zoom_mode)
    {
        case RSTTO_ZOOM_MODE_CUSTOM:
            break;
        case RSTTO_ZOOM_MODE_FIT:
            rstto_picture_viewer_fit_scale (viewer);
            break;
        case RSTTO_ZOOM_MODE_100:
            rstto_picture_viewer_set_scale (viewer, 1);
            break;
    }
}

/**
 *  rstto_picture_viewer_set_image:
 *  @viewer :
 *  @image  :
 *
 *
 */
void
rstto_picture_viewer_set_image (RsttoPictureViewer *viewer, RsttoImage *image)
{
    if (viewer->priv->image)
    {
        g_signal_handlers_disconnect_by_func (viewer->priv->image, cb_rstto_picture_viewer_image_updated, viewer);
        g_signal_handlers_disconnect_by_func (viewer->priv->image, cb_rstto_picture_viewer_image_prepared, viewer);
        g_object_unref (viewer->priv->image);
    }

    viewer->priv->image = image;

    if (viewer->priv->image)
    {
        g_object_ref (viewer->priv->image);
        g_signal_connect (G_OBJECT (viewer->priv->image), "updated", G_CALLBACK (cb_rstto_picture_viewer_image_updated), viewer);
        g_signal_connect (G_OBJECT (viewer->priv->image), "prepared", G_CALLBACK (cb_rstto_picture_viewer_image_prepared), viewer);
        rstto_image_load (viewer->priv->image, FALSE, NULL);
    }
}

static void
cb_rstto_picture_viewer_image_updated (RsttoImage *image, RsttoPictureViewer *viewer)
{
    if (viewer->priv->state == RSTTO_PICTURE_VIEWER_STATE_PREVIEW)
        viewer->priv->state = RSTTO_PICTURE_VIEWER_STATE_NONE;

    rstto_picture_viewer_refresh (viewer);   
    rstto_picture_viewer_paint (GTK_WIDGET (viewer));
}

static void
cb_rstto_picture_viewer_image_prepared (RsttoImage *image, RsttoPictureViewer *viewer)
{
    if (viewer->priv->state == RSTTO_PICTURE_VIEWER_STATE_NONE)
        viewer->priv->state = RSTTO_PICTURE_VIEWER_STATE_PREVIEW;

    rstto_picture_viewer_refresh (viewer);   
    rstto_picture_viewer_paint (GTK_WIDGET (viewer));
}

/************************
 * FIXME: DnD
 */

static void
rstto_picture_viewer_drag_data_received(GtkWidget *widget,
                                        GdkDragContext *context,
                                        gint x,
                                        gint y,
                                        GtkSelectionData *selection_data,
                                        guint info,
                                        guint time)
{
    /*
    RsttoPictureViewer *picture_viewer = RSTTO_PICTURE_VIEWER(widget);
    gchar **array = gtk_selection_data_get_uris (selection_data);

    context->action = GDK_ACTION_PRIVATE;

    if (array == NULL)
    {
        gtk_drag_finish (context, FALSE, FALSE, time);
    }

    gchar **_array = array;

    while(*_array)
    {
        ThunarVfsPath *vfs_path = thunar_vfs_path_new(*_array, NULL);
        gchar *path = thunar_vfs_path_dup_string(vfs_path);
        if (g_file_test(path, G_FILE_TEST_EXISTS))
        {
            if (g_file_test(path, G_FILE_TEST_IS_DIR))
            {
                if(rstto_navigator_open_folder(picture_viewer->priv->navigator, path, FALSE, NULL) == TRUE)
                {
                    rstto_navigator_jump_first(picture_viewer->priv->navigator);
                }
            }
            else
            {
                rstto_navigator_open_file(picture_viewer->priv->navigator, path, FALSE, NULL);
            }
        }

        g_free(path);
        thunar_vfs_path_unref(vfs_path);
        _array++;
    }
    
    gtk_drag_finish (context, TRUE, FALSE, time);
    */
}

static gboolean
rstto_picture_viewer_drag_drop (GtkWidget *widget,
                                GdkDragContext *context,
                                gint x,
                                gint y,
                                guint time)
{
    GdkAtom target;

    /* determine the drop target */
    target = gtk_drag_dest_find_target (widget, context, NULL);
    if (G_LIKELY (target == gdk_atom_intern ("text/uri-list", FALSE)))
    {
        /* set state so the drag-data-received handler
         * knows that this is really a drop this time.
         */

        /* request the drag data from the source. */
        gtk_drag_get_data (widget, context, target, time);
    }
    else
    {
        return FALSE;
    }
    return TRUE;
}


static gboolean
rstto_picture_viewer_drag_motion (GtkWidget *widget,
                                GdkDragContext *context,
                                gint x,
                                gint y,
                                guint time)
{
    GdkAtom target;
    target = gtk_drag_dest_find_target (widget, context, NULL);
    if (G_UNLIKELY (target != gdk_atom_intern ("text/uri-list", FALSE)))
    {
        /* we cannot handle the drop */
        g_debug("FAAAAAAAAAAAAAALSE");
        return FALSE;
    }
    return TRUE;
}

