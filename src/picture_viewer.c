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
    RSTTO_PICTURE_VIEWER_STATE_NORMAL = 0,
    RSTTO_PICTURE_VIEWER_STATE_PREVIEW
} RsttoPictureViewerState;

typedef enum
{
    RSTTO_PICTURE_VIEWER_MOTION_STATE_NORMAL = 0,
    RSTTO_PICTURE_VIEWER_MOTION_STATE_BOX_ZOOM,
    RSTTO_PICTURE_VIEWER_MOTION_STATE_MOVE
} RsttoPictureViewerMotionState;

typedef enum
{
    RSTTO_ZOOM_MODE_CUSTOM,
    RSTTO_ZOOM_MODE_100,
    RSTTO_ZOOM_MODE_FIT
} RsttoZoomMode;

enum
{
    TARGET_TEXT_URI_LIST,
};

static const GtkTargetEntry drop_targets[] = {
    {"text/uri-list", 0, TARGET_TEXT_URI_LIST},
};


struct _RsttoPictureViewerPriv
{
    RsttoImage              *image;
    GtkMenu                 *menu;
    RsttoPictureViewerState  state;
    RsttoZoomMode            zoom_mode;


    GdkPixbuf        *dst_pixbuf; /* The pixbuf which ends up on screen */
    void             (*cb_value_changed)(GtkAdjustment *, RsttoPictureViewer *);
    GdkColor         *bg_color;

    struct
    {
        gdouble x;
        gdouble y;
        gdouble current_x;
        gdouble current_y;
        gint h_val;
        gint v_val;
        RsttoPictureViewerMotionState state;
    } motion;

    struct
    {
        gint idle_id;
        gboolean refresh;
    } repaint;
};

static void
rstto_picture_viewer_init(RsttoPictureViewer *);
static void
rstto_picture_viewer_class_init(RsttoPictureViewerClass *);
static void
rstto_picture_viewer_destroy(GtkObject *object);

static void
rstto_picture_viewer_set_state (RsttoPictureViewer *viewer, RsttoPictureViewerState state);
static RsttoPictureViewerState
rstto_picture_viewer_get_state (RsttoPictureViewer *viewer);
static void
rstto_picture_viewer_set_motion_state (RsttoPictureViewer *viewer, RsttoPictureViewerMotionState state);

static void
rstto_picture_viewer_set_zoom_mode (RsttoPictureViewer *viewer, RsttoZoomMode mode);

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
rstto_picture_viewer_queued_repaint (RsttoPictureViewer *viewer, gboolean refresh);

static gboolean
rstto_picture_viewer_set_scroll_adjustments(RsttoPictureViewer *, GtkAdjustment *, GtkAdjustment *);

static void
cb_rstto_picture_viewer_value_changed(GtkAdjustment *, RsttoPictureViewer *);

static void
cb_rstto_picture_viewer_image_updated (RsttoImage *image, RsttoPictureViewer *viewer);
static void
cb_rstto_picture_viewer_image_prepared (RsttoImage *image, RsttoPictureViewer *viewer);

static gboolean 
cb_rstto_picture_viewer_queued_repaint (RsttoPictureViewer *viewer);

static void
cb_rstto_picture_viewer_button_press_event (RsttoPictureViewer *viewer, GdkEventButton *event);
static void
cb_rstto_picture_viewer_button_release_event (RsttoPictureViewer *viewer, GdkEventButton *event);
static gboolean 
cb_rstto_picture_viewer_motion_notify_event (RsttoPictureViewer *viewer,
                                             GdkEventMotion *event,
                                             gpointer user_data);
static void
cb_rstto_picture_viewer_popup_menu (RsttoPictureViewer *viewer, gboolean user_data);

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

    g_signal_connect(G_OBJECT(viewer), "button_press_event", G_CALLBACK(cb_rstto_picture_viewer_button_press_event), NULL);
    g_signal_connect(G_OBJECT(viewer), "button_release_event", G_CALLBACK(cb_rstto_picture_viewer_button_release_event), NULL);
    g_signal_connect(G_OBJECT(viewer), "motion_notify_event", G_CALLBACK(cb_rstto_picture_viewer_motion_notify_event), NULL);
    g_signal_connect(G_OBJECT(viewer), "popup-menu", G_CALLBACK(cb_rstto_picture_viewer_popup_menu), NULL);

    /*
    gtk_drag_dest_set(GTK_WIDGET(viewer), 0, drop_targets, G_N_ELEMENTS(drop_targets),
                      GDK_ACTION_COPY | GDK_ACTION_LINK | GDK_ACTION_MOVE | GDK_ACTION_PRIVATE);
    */
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

/**
 * rstto_picture_viewer_class_init:
 * @viewer_class:
 *
 * Initialize pictureviewer class
 */
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

/**
 * rstto_picture_viewer_realize:
 * @widget:
 *
 */
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

/**
 * rstto_picture_viewer_size_request:
 * @widget:
 * @requisition:
 *
 * Request a default size of 300 by 400 pixels
 */
static void
rstto_picture_viewer_size_request(GtkWidget *widget, GtkRequisition *requisition)
{
    requisition->width = 400;
    requisition->height= 300;
}


/**
 * rstto_picture_viewer_size_allocate:
 * @widget:
 * @allocation:
 *
 *
 */
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

    /** 
     * TODO: Check if we really nead a refresh
     */
    rstto_picture_viewer_queued_repaint (viewer, FALSE);
    //rstto_picture_viewer_paint (viewer);
}

/**
 * rstto_picture_viewer_expose:
 * @widget:
 * @event:
 *
 */
static gboolean
rstto_picture_viewer_expose(GtkWidget *widget, GdkEventExpose *event)
{
    RsttoPictureViewer *viewer = RSTTO_PICTURE_VIEWER (widget);

    /** 
     * TODO: Check if we really nead a refresh
     */
    //rstto_picture_viewer_paint (viewer);
    rstto_picture_viewer_queued_repaint (viewer, TRUE);
    return FALSE;
}

/**
 * rstto_picture_viewer_paint:
 * @widget:
 *
 * Paint the picture_viewer widget contents
 */
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
            if(viewer->priv->motion.state == RSTTO_PICTURE_VIEWER_MOTION_STATE_BOX_ZOOM)
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
    /** 
     * A new subpixbuf needs to be blown up
     */
    rstto_picture_viewer_queued_repaint (viewer, TRUE);
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
    gdouble *img_scale, *new_scale;
    GdkPixbuf *src_pixbuf = NULL;

    if (viewer->priv->image)
    {
        src_pixbuf = rstto_image_get_pixbuf (viewer->priv->image);
        img_scale = g_object_get_data (G_OBJECT (viewer->priv->image), "viewer-scale");

        if (src_pixbuf)
        {
            gdouble width = (gdouble)gdk_pixbuf_get_width (src_pixbuf);
            gdouble height = (gdouble)gdk_pixbuf_get_height (src_pixbuf);


            viewer->hadjustment->upper = width * scale;
            gtk_adjustment_changed(viewer->hadjustment);

            viewer->vadjustment->upper = height * scale;
            gtk_adjustment_changed(viewer->vadjustment);

            viewer->hadjustment->value = (((viewer->hadjustment->value +
                                          (viewer->hadjustment->page_size / 2)) *
                                           (scale)) / (*img_scale)) - (viewer->hadjustment->page_size / 2);
            viewer->vadjustment->value = (((viewer->vadjustment->value +
                                          (viewer->vadjustment->page_size / 2)) *
                                           (scale)) / (*img_scale)) - (viewer->vadjustment->page_size / 2);

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

            /** 
             * Set these settings at the end of the function, 
             * since the old and new values are required in the above code
             */
            *img_scale = scale;

            rstto_picture_viewer_queued_repaint (viewer, TRUE);
        }
    }
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

/**
 * rstto_picture_viewer_calculate_scale:
 * @viewer:
 *
 */
static gdouble
rstto_picture_viewer_calculate_scale (RsttoPictureViewer *viewer)
{
    GdkPixbuf *p_src_pixbuf;
    gint width = 0, height = 0;

    if (viewer->priv->image != NULL)
    {   
        p_src_pixbuf = rstto_image_get_pixbuf (viewer->priv->image);
        if (p_src_pixbuf)
        {

            width = gdk_pixbuf_get_width (p_src_pixbuf);
            height = gdk_pixbuf_get_height (p_src_pixbuf);

        }
    }

    if (width > 0 && height > 0)
    {
        if ((gdouble)(GTK_WIDGET (viewer)->allocation.width / (gdouble)width) <
            ((gdouble)GTK_WIDGET (viewer)->allocation.height / (gdouble)height))
        {
            return (gdouble)GTK_WIDGET (viewer)->allocation.width / (gdouble)width;
        }
        else
        {
            return (gdouble)GTK_WIDGET (viewer)->allocation.height / (gdouble)height;
        }
    }
    return -1;
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
            if (scale= 0.05)
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

        switch (viewer->priv->motion.state)
        {
            case RSTTO_PICTURE_VIEWER_MOTION_STATE_MOVE:
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
            case RSTTO_PICTURE_VIEWER_MOTION_STATE_BOX_ZOOM:
                rstto_picture_viewer_queued_repaint (viewer, FALSE);
                break;
            default:
                break;
        }
    }
    return FALSE;
}

static void
rstto_picture_viewer_calculate_adjustments (RsttoPictureViewer *viewer, gdouble scale)
{
    GdkPixbuf *p_src_pixbuf;
    GtkWidget *widget = GTK_WIDGET (viewer);
    gdouble width, height;
    gboolean vadjustment_changed = FALSE;
    gboolean hadjustment_changed = FALSE;

    if (viewer->priv->image != NULL)
    {   
        p_src_pixbuf = rstto_image_get_pixbuf (viewer->priv->image);
        if (p_src_pixbuf)
        {
            width = (gdouble)gdk_pixbuf_get_width (p_src_pixbuf);
            height = (gdouble)gdk_pixbuf_get_height (p_src_pixbuf);


            if(viewer->hadjustment)
            {
                viewer->hadjustment->page_size = widget->allocation.width;
                viewer->hadjustment->upper = width * scale;
                viewer->hadjustment->lower = 0;
                viewer->hadjustment->step_increment = 1;
                viewer->hadjustment->page_increment = 100;
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
            }
            if(viewer->vadjustment)
            {
                viewer->vadjustment->page_size = widget->allocation.height;
                viewer->vadjustment->upper = height * scale;
                viewer->vadjustment->lower = 0;
                viewer->vadjustment->step_increment = 1;
                viewer->vadjustment->page_increment = 100;
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
    }

}

static gboolean 
rstto_picture_viewer_queued_repaint (RsttoPictureViewer *viewer, gboolean refresh)
{
    g_return_if_fail (RSTTO_IS_PICTURE_VIEWER (viewer));

    if (viewer->priv->repaint.idle_id > 0)
    {
        g_source_remove(viewer->priv->repaint.idle_id);
    }
    if (refresh)
    {
        viewer->priv->repaint.refresh = TRUE;
    }
    viewer->priv->repaint.idle_id = g_idle_add((GSourceFunc)cb_rstto_picture_viewer_queued_repaint, viewer);
}

static gboolean 
cb_rstto_picture_viewer_queued_repaint (RsttoPictureViewer *viewer)
{
    GdkPixbuf *p_src_pixbuf = NULL;
    GdkPixbuf *p_tmp_pixbuf = NULL;
    gdouble *p_scale = NULL;
    gboolean *p_fit_to_screen= NULL;
    gdouble scale;
    gdouble thumb_scale = 1;
    gdouble thumb_width = 0;
    gboolean fit_to_screen = FALSE;
    gdouble width, height;
    GtkWidget *widget = GTK_WIDGET (viewer);

    if (viewer->priv->image != NULL)
    {   
        p_src_pixbuf = rstto_image_get_pixbuf (viewer->priv->image);
        if (p_src_pixbuf)
        {
            width = (gdouble)gdk_pixbuf_get_width (p_src_pixbuf);
            height = (gdouble)gdk_pixbuf_get_height (p_src_pixbuf);
            if (viewer->priv->state != RSTTO_PICTURE_VIEWER_STATE_NORMAL)
            {
                switch (viewer->priv->state)
                {
                    case RSTTO_PICTURE_VIEWER_STATE_PREVIEW:
                        p_src_pixbuf = rstto_image_get_thumbnail (viewer->priv->image);
                        thumb_width = (gdouble)gdk_pixbuf_get_width (p_src_pixbuf);
                        thumb_scale = (thumb_width / width);
                        break;
                    default:
                        break;
                }
            }
        }

        p_scale = g_object_get_data (G_OBJECT (viewer->priv->image), "viewer-scale");
        p_fit_to_screen = g_object_get_data (G_OBJECT (viewer->priv->image), "viewer-fit-to-screen");
        scale = *p_scale;
        fit_to_screen = *p_fit_to_screen;

        if ((scale <= 0) || (fit_to_screen == TRUE))
        {
            scale = rstto_picture_viewer_calculate_scale (viewer);
            *p_fit_to_screen = TRUE;
            *p_scale = scale;
        }
    }


    rstto_picture_viewer_calculate_adjustments (viewer, scale);


    if (viewer->priv->repaint.refresh)
    {
        if (p_src_pixbuf)
        {
            /**
             *  tmp_scale is the factor between the original image and the thumbnail,
             *  when looking at the actual image, tmp_scale == 1.0
             */
            gint x = (gint)viewer->hadjustment->value * scale;
            gint y = (gint)viewer->vadjustment->value * scale;

            p_tmp_pixbuf = gdk_pixbuf_new_subpixbuf (p_src_pixbuf,
                                               (gint)(x * thumb_scale), 
                                                      (y * thumb_scale),
                                                    ((widget->allocation.width / scale) < width?
                                                      (widget->allocation.width / scale)*thumb_scale:width*thumb_scale),
                                                    ((widget->allocation.height / scale) < height?
                                                      (widget->allocation.height / scale)*thumb_scale:height*thumb_scale));

            if(viewer->priv->dst_pixbuf)
            {
                g_object_unref(viewer->priv->dst_pixbuf);
                viewer->priv->dst_pixbuf = NULL;
            }
            if(p_tmp_pixbuf)
            {
                gint dst_width = gdk_pixbuf_get_width (p_tmp_pixbuf)*(scale/thumb_scale);
                gint dst_height = gdk_pixbuf_get_height (p_tmp_pixbuf)*(scale/thumb_scale);
                viewer->priv->dst_pixbuf = gdk_pixbuf_scale_simple (p_tmp_pixbuf,
                                        dst_width>0?dst_width:1,
                                        dst_height>0?dst_height:1,
                                        GDK_INTERP_BILINEAR);
                g_object_unref (p_tmp_pixbuf);
                p_tmp_pixbuf = NULL;
            }
        }
    }
    rstto_picture_viewer_paint (GTK_WIDGET (viewer));

    g_source_remove (viewer->priv->repaint.idle_id);
    viewer->priv->repaint.idle_id = -1;
    viewer->priv->repaint.refresh = FALSE;
    return FALSE;
}

static RsttoPictureViewerState
rstto_picture_viewer_get_state (RsttoPictureViewer *viewer)
{
    return viewer->priv->state;
}

static void
rstto_picture_viewer_set_state (RsttoPictureViewer *viewer, RsttoPictureViewerState state)
{
    viewer->priv->state = state;
}

static void
rstto_picture_viewer_set_motion_state (RsttoPictureViewer *viewer, RsttoPictureViewerMotionState state)
{
    viewer->priv->motion.state = state;
}

static void
cb_rstto_picture_viewer_button_press_event (RsttoPictureViewer *viewer, GdkEventButton *event)
{
    if(event->button == 1)
    {
        viewer->priv->motion.x = event->x;
        viewer->priv->motion.y = event->y;
        viewer->priv->motion.current_x = event->x;
        viewer->priv->motion.current_y = event->y;
        viewer->priv->motion.h_val = viewer->hadjustment->value;
        viewer->priv->motion.v_val = viewer->vadjustment->value;

        if (viewer->priv->image != NULL && rstto_picture_viewer_get_state (viewer) == RSTTO_PICTURE_VIEWER_STATE_NORMAL)
        {

            if (!(event->state & (GDK_CONTROL_MASK)))
            {
                GtkWidget *widget = GTK_WIDGET(viewer);
                GdkCursor *cursor = gdk_cursor_new(GDK_FLEUR);
                gdk_window_set_cursor(widget->window, cursor);
                gdk_cursor_unref(cursor);

                rstto_picture_viewer_set_motion_state (viewer, RSTTO_PICTURE_VIEWER_MOTION_STATE_MOVE);
            }

            if (event->state & GDK_CONTROL_MASK)
            {
                GtkWidget *widget = GTK_WIDGET(viewer);
                GdkCursor *cursor = gdk_cursor_new(GDK_UL_ANGLE);
                gdk_window_set_cursor(widget->window, cursor);
                gdk_cursor_unref(cursor);

                rstto_picture_viewer_set_motion_state (viewer, RSTTO_PICTURE_VIEWER_MOTION_STATE_BOX_ZOOM);
            }
        }

        
    }
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
    GtkWidget *widget = GTK_WIDGET(viewer);

    gdk_window_set_cursor(widget->window, NULL);

    rstto_picture_viewer_set_motion_state (viewer, RSTTO_PICTURE_VIEWER_MOTION_STATE_NORMAL);
    rstto_picture_viewer_queued_repaint (viewer, FALSE);
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

static void
rstto_picture_viewer_set_zoom_mode(RsttoPictureViewer *viewer, RsttoZoomMode mode)
{
    gdouble scale;
    gboolean *p_fit_to_screen;
    viewer->priv->zoom_mode = mode;

    switch (viewer->priv->zoom_mode)
    {
        case RSTTO_ZOOM_MODE_CUSTOM:
            if (viewer->priv->image)
            {
                p_fit_to_screen = g_object_get_data (G_OBJECT (viewer->priv->image), "viewer-fit-to-screen");
                *p_fit_to_screen = FALSE;
                g_object_set_data (G_OBJECT (viewer->priv->image), "viewer-fit-to-screen", p_fit_to_screen);
            }
            break;
        case RSTTO_ZOOM_MODE_FIT:
            if (viewer->priv->image)
            {
                p_fit_to_screen = g_object_get_data (G_OBJECT (viewer->priv->image), "viewer-fit-to-screen");
                *p_fit_to_screen = TRUE;
                g_object_set_data (G_OBJECT (viewer->priv->image), "viewer-fit-to-screen", p_fit_to_screen);
            }
            scale = rstto_picture_viewer_calculate_scale (viewer);
            if (scale != -1.0)
                rstto_picture_viewer_set_scale (viewer, scale);
            break;
        case RSTTO_ZOOM_MODE_100:
            if (viewer->priv->image)
            {
                p_fit_to_screen = g_object_get_data (G_OBJECT (viewer->priv->image), "viewer-fit-to-screen");
                *p_fit_to_screen = FALSE;
                g_object_set_data (G_OBJECT (viewer->priv->image), "viewer-fit-to-screen", p_fit_to_screen);
            }
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
    gdouble *scale = NULL;
    gboolean *fit_to_screen = NULL;

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

        scale = g_object_get_data (G_OBJECT (viewer->priv->image), "viewer-scale");
        fit_to_screen = g_object_get_data (G_OBJECT (viewer->priv->image), "viewer-fit-to-screen");

        if (scale == NULL)
        {
            scale = g_new0 (gdouble, 1);
            *scale = -1.0;
            g_object_set_data (G_OBJECT (viewer->priv->image), "viewer-scale", scale);
        }
        if (fit_to_screen == NULL)
        {
            fit_to_screen = g_new0 (gboolean, 1);
            g_object_set_data (G_OBJECT (viewer->priv->image), "viewer-fit-to-screen", fit_to_screen);
        }
        rstto_image_load (viewer->priv->image, FALSE, NULL);
    }
}

/**
 * cb_rstto_picture_viewer_image_updated:
 * @image:
 * @viewer:
 *
 */
static void
cb_rstto_picture_viewer_image_updated (RsttoImage *image, RsttoPictureViewer *viewer)
{
    rstto_picture_viewer_set_state (viewer, RSTTO_PICTURE_VIEWER_STATE_NORMAL);

    rstto_picture_viewer_queued_repaint (viewer, TRUE);
}

/**
 * cb_rstto_picture_viewer_image_prepared:
 * @image:
 * @viewer:
 *
 */
static void
cb_rstto_picture_viewer_image_prepared (RsttoImage *image, RsttoPictureViewer *viewer)
{
    rstto_picture_viewer_set_state (viewer, RSTTO_PICTURE_VIEWER_STATE_PREVIEW);

    rstto_picture_viewer_queued_repaint (viewer, TRUE);
}

/**
 * rstto_picture_viewer_zoom_fit:
 * @window:
 *
 * Adjust the scale to make the image fit the window
 */
void
rstto_picture_viewer_zoom_fit (RsttoPictureViewer *viewer)
{
    rstto_picture_viewer_set_zoom_mode (viewer, RSTTO_ZOOM_MODE_FIT);
}

/**
 * rstto_picture_viewer_zoom_100:
 * @viewer:
 *
 * Set the scale to 1, meaning a zoom-factor of 100%
 */
void
rstto_picture_viewer_zoom_100 (RsttoPictureViewer *viewer)
{
    rstto_picture_viewer_set_zoom_mode (viewer, RSTTO_ZOOM_MODE_100);
}

/**
 * rstto_picture_viewer_zoom_in:
 * @viewer:
 * @factor:
 *
 * Zoom in the scale with a certain factor
 */
void
rstto_picture_viewer_zoom_in (RsttoPictureViewer *viewer, gdouble factor)
{
    gdouble scale;

    rstto_picture_viewer_set_zoom_mode (viewer, RSTTO_ZOOM_MODE_CUSTOM);
    scale = rstto_picture_viewer_get_scale (viewer);
    rstto_picture_viewer_set_scale (viewer, scale * factor);
}

/**
 * rstto_picture_viewer_zoom_out:
 * @viewer:
 * @factor:
 *
 * Zoom out the scale with a certain factor
 */
void
rstto_picture_viewer_zoom_out (RsttoPictureViewer *viewer, gdouble factor)
{
    gdouble scale;

    rstto_picture_viewer_set_zoom_mode (viewer, RSTTO_ZOOM_MODE_CUSTOM);
    scale = rstto_picture_viewer_get_scale (viewer);
    rstto_picture_viewer_set_scale (viewer, scale / factor);
}


/******************************************************************************************/


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


