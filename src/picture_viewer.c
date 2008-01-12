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
#include <thunar-vfs/thunar-vfs.h>
#include <libexif/exif-data.h>

#include "navigator.h"
#include "picture_viewer.h"

typedef enum
{
    RSTTO_PICTURE_VIEWER_STATE_NONE,
    RSTTO_PICTURE_VIEWER_STATE_MOVE,
    RSTTO_PICTURE_VIEWER_STATE_BOX_ZOOM
} RsttoPictureViewerState;


struct _RsttoPictureViewerPriv
{
    RsttoNavigator   *navigator;
    RsttoNavigatorEntry *entry;
    RsttoZoomMode    zoom_mode;

    GdkPixbuf        *src_pixbuf;
    GdkPixbuf        *dst_pixbuf; /* The pixbuf which ends up on screen */
    void             (*cb_value_changed)(GtkAdjustment *, RsttoPictureViewer *);
    gboolean          show_border;
    GdkColor         *bg_color;
    struct
    {
        gdouble x;
        gdouble y;
        gdouble current_x;
        gdouble current_y;
        gint h_val;
        gint v_val;
        RsttoPictureViewerState state;
    } motion;
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
rstto_picture_viewer_paint(GtkWidget *widget);
static gboolean
rstto_picture_viewer_refresh(RsttoPictureViewer *viewer);

static gboolean
rstto_picture_viewer_set_scroll_adjustments(RsttoPictureViewer *, GtkAdjustment *, GtkAdjustment *);

static void
cb_rstto_picture_viewer_nav_iter_changed(RsttoNavigator *, gint , RsttoNavigatorEntry *, RsttoPictureViewer *);
static void
cb_rstto_picture_viewer_nav_entry_modified(RsttoNavigator *, RsttoNavigatorEntry *, RsttoPictureViewer *);

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

    viewer->priv->src_pixbuf = NULL;
    viewer->priv->dst_pixbuf = NULL;
    viewer->priv->zoom_mode = RSTTO_ZOOM_MODE_FIT;
    gtk_widget_set_redraw_on_allocate(GTK_WIDGET(viewer), TRUE);
    gtk_widget_set_events (GTK_WIDGET(viewer),
                           GDK_BUTTON_PRESS_MASK |
                           GDK_BUTTON_RELEASE_MASK |
                           GDK_BUTTON1_MOTION_MASK);

    viewer->priv->show_border = TRUE;

    g_signal_connect(G_OBJECT(viewer), "scroll_event", G_CALLBACK(cb_rstto_picture_viewer_scroll_event), NULL);
    g_signal_connect(G_OBJECT(viewer), "button_press_event", G_CALLBACK(cb_rstto_picture_viewer_button_press_event), NULL);
    g_signal_connect(G_OBJECT(viewer), "button_release_event", G_CALLBACK(cb_rstto_picture_viewer_button_release_event), NULL);
    g_signal_connect(G_OBJECT(viewer), "motion_notify_event", G_CALLBACK(cb_rstto_picture_viewer_motion_notify_event), NULL);
    g_signal_connect(G_OBJECT(viewer), "popup-menu", G_CALLBACK(cb_rstto_picture_viewer_popup_menu), NULL);
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

    object_class->destroy = rstto_picture_viewer_destroy;


    widget_class->set_scroll_adjustments_signal =
                  g_signal_new ("set_scroll_adjustments",
                                G_TYPE_FROM_CLASS (object_class),
                                G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
                                G_STRUCT_OFFSET (RsttoPictureViewerClass, set_scroll_adjustments),
                                NULL, NULL,
                                gtk_marshal_VOID__POINTER_POINTER,
                                G_TYPE_NONE, 2,
                                GTK_TYPE_ADJUSTMENT,
                                GTK_TYPE_ADJUSTMENT);
}

static void
rstto_picture_viewer_size_request(GtkWidget *widget, GtkRequisition *requisition)
{
    requisition->width = 100;
    requisition->height= 500;
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
    rstto_picture_viewer_paint(GTK_WIDGET(viewer));
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
    rstto_picture_viewer_refresh(RSTTO_PICTURE_VIEWER(widget));
    rstto_picture_viewer_paint(widget);
    return FALSE;
}

static void
rstto_picture_viewer_paint(GtkWidget *widget)
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
            if(viewer->priv->motion.state == RSTTO_PICTURE_VIEWER_STATE_BOX_ZOOM)
            {
                gdk_gc_set_foreground(gc,
                        &(widget->style->fg[GTK_STATE_SELECTED]));
                gdouble x1, x2, y1, y2;

                if (viewer->priv->motion.x < viewer->priv->motion.current_x)
                {
                    x1 = viewer->priv->motion.x;
                    x2 = viewer->priv->motion.current_x;
                }
                else
                {
                    x1 = viewer->priv->motion.current_x;
                    x2 = viewer->priv->motion.x;
                }
                if (viewer->priv->motion.y < viewer->priv->motion.current_y)
                {
                    y1 = viewer->priv->motion.y;
                    y2 = viewer->priv->motion.current_y;
                }
                else
                {
                    y1 = viewer->priv->motion.current_y;
                    y2 = viewer->priv->motion.y;
                }
                gdk_draw_rectangle(GDK_DRAWABLE(buffer),
                                gc,
                                FALSE,
                                x1,
                                y1,
                                x2 - x1,
                                y2 - y1);
            }

            if(viewer->priv->show_border)
            {
                gdk_gc_set_foreground(gc, &line_color);
                gdk_draw_line(GDK_DRAWABLE(buffer), gc, x1, y1, x1, y1+y2);
                gdk_draw_line(GDK_DRAWABLE(buffer), gc, x1, y1+y2, x1+x2, y1+y2);
                gdk_draw_line(GDK_DRAWABLE(buffer), gc, x1, y1, x1+x2, y1);
                gdk_draw_line(GDK_DRAWABLE(buffer), gc, x1+x2, y1, x1+x2, y1+y2);
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
rstto_picture_viewer_new(RsttoNavigator *navigator)
{
    GtkWidget *widget;

    widget = g_object_new(RSTTO_TYPE_PICTURE_VIEWER, NULL);
    RSTTO_PICTURE_VIEWER(widget)->priv->navigator = navigator;
    g_signal_connect(G_OBJECT(navigator), "iter-changed", G_CALLBACK(cb_rstto_picture_viewer_nav_iter_changed), widget);
    g_signal_connect(G_OBJECT(navigator), "entry-modified", G_CALLBACK(cb_rstto_picture_viewer_nav_entry_modified), widget);

    return widget;
}

void
rstto_picture_viewer_set_scale(RsttoPictureViewer *viewer, gdouble scale)
{
    RsttoNavigatorEntry *entry = rstto_navigator_get_file(viewer->priv->navigator);
    if (entry)
    {
        gdouble old_scale = rstto_navigator_entry_get_scale(entry);

        gdouble width = (gdouble)gdk_pixbuf_get_width(viewer->priv->src_pixbuf);
        gdouble height = (gdouble)gdk_pixbuf_get_height(viewer->priv->src_pixbuf);

        rstto_navigator_entry_set_fit_to_screen (entry, FALSE);
        rstto_navigator_entry_set_scale(entry, scale);

        viewer->hadjustment->upper = width * scale;
        gtk_adjustment_changed(viewer->hadjustment);

        viewer->vadjustment->upper = height * scale;
        gtk_adjustment_changed(viewer->vadjustment);

        viewer->hadjustment->value = (((viewer->hadjustment->value +
                                      (viewer->hadjustment->page_size / 2)) *
                                       scale) / old_scale) - (viewer->hadjustment->page_size / 2);
        viewer->vadjustment->value = (((viewer->vadjustment->value +
                                      (viewer->vadjustment->page_size / 2)) *
                                       scale) / old_scale) - (viewer->vadjustment->page_size / 2);

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

    }
}

gdouble
rstto_picture_viewer_fit_scale(RsttoPictureViewer *viewer)
{
    RsttoNavigatorEntry *entry = rstto_navigator_get_file(viewer->priv->navigator);
    if (entry)
    {
        rstto_navigator_entry_set_fit_to_screen (entry, TRUE);

        if(rstto_picture_viewer_refresh(viewer))
        {
            rstto_picture_viewer_paint(GTK_WIDGET(viewer));
        }
        return rstto_navigator_entry_get_scale(entry);
    }
    return 0;
}

gdouble
rstto_picture_viewer_get_scale(RsttoPictureViewer *viewer)
{
    RsttoNavigatorEntry *entry = rstto_navigator_get_file(viewer->priv->navigator);
    if (entry)
    {
        return rstto_navigator_entry_get_scale(entry);
    }
    return 0;
}

static gboolean
rstto_picture_viewer_refresh(RsttoPictureViewer *viewer)
{
    GtkWidget *widget = GTK_WIDGET(viewer);
    gboolean fit_to_screen = FALSE;
    gdouble scale = 0;
    RsttoNavigatorEntry *entry = rstto_navigator_get_file(viewer->priv->navigator);
    gboolean changed = TRUE;
    if (entry)
    {
        fit_to_screen = rstto_navigator_entry_get_fit_to_screen(entry);
        scale = rstto_navigator_entry_get_scale(entry);
    }
    

    gboolean vadjustment_changed = FALSE;
    gboolean hadjustment_changed = FALSE;

    if (viewer->priv->src_pixbuf != NULL && entry == NULL)
    {
        gdk_pixbuf_unref(viewer->priv->src_pixbuf);
        viewer->priv->src_pixbuf = NULL;
    }

    if(viewer->priv->src_pixbuf)
    {
        gdouble width = (gdouble)gdk_pixbuf_get_width(viewer->priv->src_pixbuf);
        gdouble height = (gdouble)gdk_pixbuf_get_height(viewer->priv->src_pixbuf);
        if (scale == 0)
        {
            if ((widget->allocation.width > width) && (widget->allocation.height > height))
            {
                scale = 1.0;
                rstto_navigator_entry_set_scale(entry, scale);
                fit_to_screen = FALSE;
            }
            else
            {
                fit_to_screen = TRUE;
                rstto_navigator_entry_set_fit_to_screen(entry, TRUE);
            }
        }

        switch (viewer->priv->zoom_mode)
        {
            case RSTTO_ZOOM_MODE_FIT:
                fit_to_screen = TRUE;
                rstto_navigator_entry_set_fit_to_screen(entry, TRUE);
                break;
            case RSTTO_ZOOM_MODE_100:
                fit_to_screen = FALSE;
                scale = 1.0;
                rstto_navigator_entry_set_scale(entry, scale);
                break;
            case RSTTO_ZOOM_MODE_CUSTOM:
                break;
        }

        if(fit_to_screen)
        {
            gdouble h_scale = GTK_WIDGET(viewer)->allocation.width / width;
            gdouble v_scale = GTK_WIDGET(viewer)->allocation.height / height;
            if(h_scale < v_scale)
            {
                if(scale != h_scale)
                {
                    scale = h_scale;
                    changed = TRUE;
                }
                rstto_navigator_entry_set_scale(entry, h_scale);
            }
            else
            {
                if(scale != v_scale)
                {
                    scale = v_scale;
                    changed = TRUE;
                }
                rstto_navigator_entry_set_scale(entry, v_scale);
            }
        }
        if(GTK_WIDGET_REALIZED(widget))
        {
            gdouble width = (gdouble)gdk_pixbuf_get_width(viewer->priv->src_pixbuf);
            gdouble height = (gdouble)gdk_pixbuf_get_height(viewer->priv->src_pixbuf);
            
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


            GdkPixbuf *tmp_pixbuf = NULL;
            if (viewer->vadjustment && viewer->hadjustment)
            {
                if (1.0)
                {
                    tmp_pixbuf = gdk_pixbuf_new_subpixbuf(viewer->priv->src_pixbuf,
                                                      (gint)(viewer->hadjustment->value / scale), 
                                                      viewer->vadjustment->value / scale,
                                                      ((widget->allocation.width/scale)) < width?
                                                        widget->allocation.width/scale:width,
                                                      ((widget->allocation.height/scale))< height?
                                                        widget->allocation.height/scale:height);
                }
                else
                {
                    tmp_pixbuf = viewer->priv->src_pixbuf;
                    g_object_ref(tmp_pixbuf);
                }
            }

            if(viewer->priv->dst_pixbuf)
            {
                g_object_unref(viewer->priv->dst_pixbuf);
                viewer->priv->dst_pixbuf = NULL;
            }

            if(tmp_pixbuf)
            {
                gint dst_width = gdk_pixbuf_get_width(tmp_pixbuf)*scale;
                gint dst_height = gdk_pixbuf_get_height(tmp_pixbuf)*scale;
                viewer->priv->dst_pixbuf = gdk_pixbuf_scale_simple(tmp_pixbuf,
                                        dst_width>0?dst_width:1,
                                        dst_height>0?dst_height:1,
                                        GDK_INTERP_BILINEAR);
                g_object_unref(tmp_pixbuf);
                tmp_pixbuf = NULL;
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
    else
    {
        if(viewer->priv->dst_pixbuf)
        {
            g_object_unref(viewer->priv->dst_pixbuf);
            viewer->priv->dst_pixbuf = NULL;
        }
    }
    return changed;
}

static void
cb_rstto_picture_viewer_nav_iter_changed(RsttoNavigator *nav, gint nr, RsttoNavigatorEntry *entry, RsttoPictureViewer *viewer)
{
    viewer->priv->entry = entry;
    if(entry)
    {
        rstto_navigator_entry_load_image(entry, FALSE);
    }
    else
    {
        rstto_picture_viewer_refresh(viewer);
        rstto_picture_viewer_paint(GTK_WIDGET(viewer));
    }
}

static void
cb_rstto_picture_viewer_nav_entry_modified(RsttoNavigator *nav, RsttoNavigatorEntry *entry, RsttoPictureViewer *viewer)
{
    if (entry == viewer->priv->entry)
    {
        if(viewer->priv->src_pixbuf)
        {
            gdk_pixbuf_unref(viewer->priv->src_pixbuf);
        }
        viewer->priv->src_pixbuf = rstto_navigator_entry_get_pixbuf(entry);
        if (viewer->priv->src_pixbuf)
        {
            gdk_pixbuf_ref(viewer->priv->src_pixbuf);
        }
        rstto_picture_viewer_refresh(viewer);
        rstto_picture_viewer_paint(GTK_WIDGET(viewer));
    }
}

static void
cb_rstto_picture_viewer_scroll_event (RsttoPictureViewer *viewer, GdkEventScroll *event)
{
    RsttoNavigatorEntry *entry = rstto_navigator_get_file(viewer->priv->navigator);
    gdouble scale = rstto_navigator_entry_get_scale(entry);
    viewer->priv->zoom_mode = RSTTO_ZOOM_MODE_CUSTOM;
    navigator->max_history = 0;
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

        switch (viewer->priv->motion.state)
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
    if(event->button == 1)
    {
        viewer->priv->motion.x = event->x;
        viewer->priv->motion.y = event->y;
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
                viewer->priv->motion.state = RSTTO_PICTURE_VIEWER_STATE_NONE;

                if (viewer->priv->refresh.idle_id > 0)
                {
                    g_source_remove(viewer->priv->refresh.idle_id);
                }
                viewer->priv->refresh.idle_id = g_idle_add((GSourceFunc)cb_rstto_picture_viewer_queued_repaint, viewer);
                break;
        }

    }

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
        gtk_menu_detach(viewer->priv->menu);
        gtk_widget_destroy(GTK_WIDGET(viewer->priv->menu));
    }

    viewer->priv->menu = menu;

    if (viewer->priv->menu)
    {
        gtk_menu_attach_to_widget(viewer->priv->menu, GTK_WIDGET(viewer), NULL);
    }
}

void
rstto_picture_viewer_set_bg_color (RsttoPictureViewer *viewer, const GdkColor *color)
{
    if (viewer->priv->bg_color)
    {
        gdk_color_free(viewer->priv->bg_color);
        viewer->priv->bg_color = NULL;
    }
    if (color)
    {
        viewer->priv->bg_color = gdk_color_copy(color);
        GdkColormap *colormap = gtk_widget_get_colormap(GTK_WIDGET(viewer));
        gdk_colormap_alloc_color(colormap, viewer->priv->bg_color, TRUE, TRUE);
    }
}

const GdkColor *
rstto_picture_viewer_get_bg_color (RsttoPictureViewer *viewer)
{
    return viewer->priv->bg_color;
}

void
rstto_picture_viewer_redraw(RsttoPictureViewer *viewer)
{
    rstto_picture_viewer_refresh(viewer);
    rstto_picture_viewer_paint(GTK_WIDGET(viewer));
}

void
rstto_picture_viewer_set_zoom_mode(RsttoPictureViewer *viewer, RsttoZoomMode mode)
{
    viewer->priv->zoom_mode = mode;
}
