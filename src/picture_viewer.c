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

struct _RsttoPictureViewerPriv
{
    GdkPixbufLoader  *loader;
    GdkPixbufAnimation *animation;
    GdkPixbufAnimationIter *iter;
    GIOChannel       *io_channel;
    GdkPixbuf        *src_pixbuf;
    GdkPixbuf        *dst_pixbuf; /* The pixbuf which ends up on screen */
    RsttoNavigator   *navigator;
    void             (*cb_value_changed)(GtkAdjustment *, RsttoPictureViewer *);
    gboolean          show_border;
    GTimer           *timer;
    gint              timeout_id;
    gboolean          timeout;
};

static void
rstto_picture_viewer_init(RsttoPictureViewer *);
static void
rstto_picture_viewer_class_init(RsttoPictureViewerClass *);
static void
rstto_picture_viewer_destroy(GtkObject *object);

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
cb_rstto_picture_viewer_value_changed(GtkAdjustment *, RsttoPictureViewer *);
static void
cb_rstto_picture_viewer_scroll_event (RsttoPictureViewer *, GdkEventScroll *);
static gboolean
cb_rstto_picture_viewer_update_image(RsttoPictureViewer *viewer);

static gboolean
cb_rstto_picture_viewer_read_file(GIOChannel *io_channel, GIOCondition cond, RsttoPictureViewer *viewer);

static void
cb_rstto_picture_viewer_area_prepared(GdkPixbufLoader *loader, RsttoPictureViewer *viewer);
static void
cb_rstto_picture_viewer_area_updated(GdkPixbufLoader *loader, gint x, gint y, gint width, gint height, RsttoPictureViewer *viewer);
static void
cb_rstto_picture_viewer_closed(GdkPixbufLoader *loader, RsttoPictureViewer *viewer);


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
    gtk_widget_set_redraw_on_allocate(GTK_WIDGET(viewer), TRUE);

    viewer->priv->show_border = TRUE;
    viewer->priv->timer = g_timer_new();
    viewer->priv->timeout = TRUE;

    g_signal_connect(G_OBJECT(viewer), "scroll_event", G_CALLBACK(cb_rstto_picture_viewer_scroll_event), NULL);
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
    if(rstto_picture_viewer_refresh(RSTTO_PICTURE_VIEWER(widget)))
    {
        rstto_picture_viewer_paint(widget);
    }
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

        gdk_gc_set_foreground(gc, &color);
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
    gdouble scale = 1.0;
    gdouble width = 1.0;
    gdouble height = 1.0;
    GtkWidget *widget = GTK_WIDGET(viewer);
    RsttoNavigatorEntry *entry = rstto_navigator_get_file(viewer->priv->navigator);
    if (entry)
    {
        scale = rstto_navigator_entry_get_scale(entry);
        width = (gdouble)gdk_pixbuf_get_width(viewer->priv->src_pixbuf);
        height = (gdouble)gdk_pixbuf_get_height(viewer->priv->src_pixbuf);
    }
    GdkPixbuf *tmp_pixbuf = NULL;
    if (viewer->vadjustment && viewer->hadjustment)
    {
        if ((width > widget->allocation.width) || (height > widget->allocation.height))
        {
            tmp_pixbuf = gdk_pixbuf_new_subpixbuf(viewer->priv->src_pixbuf,
                                              viewer->hadjustment->value / scale >= 0?
                                                viewer->hadjustment->value / scale : 0,
                                              viewer->vadjustment->value / scale >= 0?
                                                viewer->vadjustment->value / scale : 0,
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
        if (scale < 1.0)
        {
            viewer->priv->dst_pixbuf = gdk_pixbuf_scale_simple(tmp_pixbuf,
                                dst_width>0?dst_width:1,
                                dst_height>0?dst_height:1,
                                GDK_INTERP_BILINEAR);
        }
        if (scale > 1.0)
        {
            viewer->priv->dst_pixbuf = gdk_pixbuf_scale_simple(tmp_pixbuf,
                                dst_width>0?dst_width:1,
                                dst_height>0?dst_height:1,
                                GDK_INTERP_NEAREST);
        }
        if (scale == 1.0)
        {
            viewer->priv->dst_pixbuf = tmp_pixbuf;
            g_object_ref(viewer->priv->dst_pixbuf);
        }
        g_object_unref(tmp_pixbuf);
        tmp_pixbuf = NULL;
    }
    rstto_picture_viewer_paint(widget);
}

GtkWidget *
rstto_picture_viewer_new(RsttoNavigator *navigator)
{
    GtkWidget *widget;

    widget = g_object_new(RSTTO_TYPE_PICTURE_VIEWER, NULL);
    RSTTO_PICTURE_VIEWER(widget)->priv->navigator = navigator;
    g_signal_connect(G_OBJECT(navigator), "iter-changed", G_CALLBACK(cb_rstto_picture_viewer_nav_iter_changed), widget);

    return widget;
}

void
rstto_picture_viewer_set_scale(RsttoPictureViewer *viewer, gdouble scale)
{
    RsttoNavigatorEntry *entry = rstto_navigator_get_file(viewer->priv->navigator);
    if (entry)
    {
        rstto_navigator_entry_set_fit_to_screen (entry, FALSE);
        rstto_navigator_entry_set_scale(entry, scale);

        if(rstto_picture_viewer_refresh(viewer))
        {
            rstto_picture_viewer_paint(GTK_WIDGET(viewer));
        }
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
                    viewer->vadjustment->value = viewer->vadjustment->upper - viewer->vadjustment->page_size - 1;
                    vadjustment_changed = TRUE;
                }
            }


            GdkPixbuf *tmp_pixbuf = NULL;
            if (viewer->vadjustment && viewer->hadjustment)
            {
                if (scale < 1.0)
                {
                    tmp_pixbuf = gdk_pixbuf_new_subpixbuf(viewer->priv->src_pixbuf,
                                                      viewer->hadjustment->value / scale >= 0?
                                                        viewer->hadjustment->value / scale : 0,
                                                      viewer->vadjustment->value / scale >= 0?
                                                        viewer->vadjustment->value / scale : 0,
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

static gboolean
cb_rstto_picture_viewer_update_image(RsttoPictureViewer *viewer)
{
    if (viewer->priv->iter)
    {
        if(gdk_pixbuf_animation_iter_advance(viewer->priv->iter, NULL))
        {
            GdkPixbuf *src_pixbuf = gdk_pixbuf_animation_iter_get_pixbuf(viewer->priv->iter);
            
            if (src_pixbuf)
            {
                RsttoNavigatorEntry *entry = rstto_navigator_get_file(viewer->priv->navigator);
                if (viewer->priv->src_pixbuf)
                    gdk_pixbuf_unref(viewer->priv->src_pixbuf);
                viewer->priv->src_pixbuf = gdk_pixbuf_rotate_simple(src_pixbuf, rstto_navigator_entry_get_rotation(entry));

                if (rstto_navigator_entry_get_flip(entry, FALSE))
                {
                    src_pixbuf = viewer->priv->src_pixbuf;
                    viewer->priv->src_pixbuf = gdk_pixbuf_flip(src_pixbuf, FALSE);
                    gdk_pixbuf_unref(src_pixbuf);
                }

                if (rstto_navigator_entry_get_flip(entry, TRUE))
                {
                    src_pixbuf = viewer->priv->src_pixbuf;
                    viewer->priv->src_pixbuf = gdk_pixbuf_flip(src_pixbuf, TRUE);
                    gdk_pixbuf_unref(src_pixbuf);
                }
            }

            rstto_picture_viewer_refresh(viewer);
            rstto_picture_viewer_paint(GTK_WIDGET(viewer));

            gint time = gdk_pixbuf_animation_iter_get_delay_time(viewer->priv->iter);
            if (time != -1)
            {
                viewer->priv->timeout_id = g_timeout_add(time, (GSourceFunc)cb_rstto_picture_viewer_update_image, viewer);
            }

            return FALSE;
        }
    }
    return TRUE;
}

static void
cb_rstto_picture_viewer_nav_iter_changed(RsttoNavigator *nav, gint nr, RsttoNavigatorEntry *entry, RsttoPictureViewer *viewer)
{
    GtkWidget *widget = GTK_WIDGET(viewer);
    if(entry)
    { 
        if (GTK_WIDGET_REALIZED(widget))
        {
            GdkCursor *cursor = gdk_cursor_new(GDK_WATCH);
            gdk_window_set_cursor(widget->window, cursor);
            gdk_cursor_unref(cursor);
        }
        if (viewer->priv->loader)
        {
            g_signal_handlers_disconnect_by_func(viewer->priv->loader , cb_rstto_picture_viewer_area_prepared, viewer);
            g_signal_handlers_disconnect_by_func(viewer->priv->loader , cb_rstto_picture_viewer_area_updated, viewer);
            gdk_pixbuf_loader_close(viewer->priv->loader, NULL);
            g_object_unref(viewer->priv->loader);
            if(viewer->priv->animation)
            {
                g_object_unref(viewer->priv->animation);
                viewer->priv->animation = NULL;
            }
            if(viewer->priv->src_pixbuf)
            {
                gdk_pixbuf_unref(viewer->priv->src_pixbuf);
                viewer->priv->src_pixbuf = NULL;
            }
            if (viewer->priv->iter)
            {
                viewer->priv->iter = NULL;
            }
        }
        viewer->priv->loader = gdk_pixbuf_loader_new();

        g_signal_connect(viewer->priv->loader, "area-prepared", G_CALLBACK(cb_rstto_picture_viewer_area_prepared), viewer);
        g_signal_connect(viewer->priv->loader, "area-updated", G_CALLBACK(cb_rstto_picture_viewer_area_updated), viewer);
        g_signal_connect(viewer->priv->loader, "closed", G_CALLBACK(cb_rstto_picture_viewer_closed), viewer);
        ThunarVfsInfo *info = rstto_navigator_entry_get_info(entry);
        gchar *path = thunar_vfs_path_dup_string(info->path);

        viewer->priv->io_channel = g_io_channel_new_file(path, "r", NULL);
        g_io_channel_set_encoding(viewer->priv->io_channel, NULL, NULL);
        g_io_add_watch(viewer->priv->io_channel, G_IO_IN | G_IO_PRI, (GIOFunc)cb_rstto_picture_viewer_read_file, viewer);
    }
    else
    {
        if (viewer->priv->src_pixbuf)
        {
            g_object_unref(viewer->priv->src_pixbuf);
            viewer->priv->src_pixbuf = NULL;
        }
        if (viewer->priv->dst_pixbuf)
        {
            g_object_unref(viewer->priv->dst_pixbuf);
            viewer->priv->dst_pixbuf = NULL;
        }
        if (GTK_WIDGET_REALIZED(widget))
        {
            rstto_picture_viewer_paint(GTK_WIDGET(viewer));
        }
    }
}

static void
cb_rstto_picture_viewer_scroll_event (RsttoPictureViewer *viewer, GdkEventScroll *event)
{
    RsttoNavigatorEntry *entry = rstto_navigator_get_file(viewer->priv->navigator);
    gdouble scale = rstto_navigator_entry_get_scale(entry);
    switch(event->direction)
    {
        case GDK_SCROLL_UP:
        case GDK_SCROLL_LEFT:
            if (scale <= 0.05)
                return;
            rstto_navigator_entry_set_scale(entry, scale / 1.2);
            rstto_navigator_entry_set_fit_to_screen (entry, FALSE);
            break;
        case GDK_SCROLL_DOWN:
        case GDK_SCROLL_RIGHT:
            if (scale >= 16)
                return;
            viewer->vadjustment->value = ((viewer->vadjustment->value + event->y) - viewer->vadjustment->page_size/2) * 1.2;
            viewer->hadjustment->value = ((viewer->hadjustment->value + event->x) - viewer->hadjustment->page_size/2) * 1.2;

            rstto_navigator_entry_set_scale(entry, scale * 1.2);
            rstto_navigator_entry_set_fit_to_screen (entry, FALSE);
            break;
    }

    if(rstto_picture_viewer_refresh(viewer))
    {
        rstto_picture_viewer_paint(GTK_WIDGET(viewer));
    }
}

void
rstto_picture_viewer_set_timeout(RsttoPictureViewer *viewer, gboolean timeout)
{
    viewer->priv->timeout = timeout;
}

static gboolean
cb_rstto_picture_viewer_read_file(GIOChannel *io_channel, GIOCondition cond, RsttoPictureViewer *viewer)
{
    gchar buffer[1024];
    gsize bytes_read = 0;
    GError *error = NULL;


    GIOStatus status;
    if (viewer->priv->io_channel == io_channel)
    {
        status = g_io_channel_read_chars(io_channel, buffer, 1024, &bytes_read,  &error);

        switch (status)
        {
            case G_IO_STATUS_NORMAL:
                if(gdk_pixbuf_loader_write(viewer->priv->loader, (const guchar *)buffer, bytes_read, NULL) == FALSE)
                {
                    gdk_pixbuf_loader_close(viewer->priv->loader, NULL);
                    viewer->priv->io_channel = NULL;
                    return FALSE;
                }
                return TRUE;
                break;
            case G_IO_STATUS_EOF:
                gdk_pixbuf_loader_write(viewer->priv->loader, (const guchar *)buffer, bytes_read, NULL);
                gdk_pixbuf_loader_close(viewer->priv->loader, NULL);
                viewer->priv->io_channel = NULL;
                return FALSE;
                break;
            case G_IO_STATUS_ERROR:
                gdk_pixbuf_loader_close(viewer->priv->loader, NULL);
                viewer->priv->io_channel = NULL;
                return FALSE;
                break;
            case G_IO_STATUS_AGAIN:
                return TRUE;
                break;
        }
    }
    g_io_channel_shutdown(io_channel, FALSE, NULL);
    return FALSE;
}

static void
cb_rstto_picture_viewer_area_prepared(GdkPixbufLoader *loader, RsttoPictureViewer *viewer)
{
    viewer->priv->animation = gdk_pixbuf_loader_get_animation(loader);
    viewer->priv->iter = gdk_pixbuf_animation_get_iter(viewer->priv->animation, NULL);
    if (viewer->priv->src_pixbuf)
    {
        gdk_pixbuf_unref(viewer->priv->src_pixbuf);
        viewer->priv->src_pixbuf = NULL;
    }

    gint time = gdk_pixbuf_animation_iter_get_delay_time(viewer->priv->iter);

    if (viewer->priv->src_pixbuf)
    {
        gdk_pixbuf_ref(viewer->priv->src_pixbuf);
    }

    rstto_picture_viewer_refresh(viewer);
    rstto_picture_viewer_paint(GTK_WIDGET(viewer));

    if (time != -1)
    {
        /* update frame */
        viewer->priv->timeout_id = g_timeout_add(time, (GSourceFunc)cb_rstto_picture_viewer_update_image, viewer);
    }   
}

static void
cb_rstto_picture_viewer_area_updated(GdkPixbufLoader *loader, gint x, gint y, gint width, gint height, RsttoPictureViewer *viewer)
{
    if (1)
    {
        /* Current Frame being updated?! */
        if (gdk_pixbuf_animation_iter_on_currently_loading_frame(viewer->priv->iter) == TRUE)
        {
            /* Is it inside the viewport? */
            RsttoNavigatorEntry *entry = rstto_navigator_get_file(viewer->priv->navigator);
            gdouble scale = rstto_navigator_entry_get_scale(entry);
            if (((viewer->vadjustment->value > (y * scale)) && (viewer->vadjustment->value < ((y+height) * scale))) &&
                ((viewer->hadjustment->value > (x * scale)) && (viewer->hadjustment->value < ((x+width) * scale))))
            {
            
            /* Update */
            /*
            gdk_pixbuf_unref(viewer->priv->src_pixbuf);
            viewer->priv->src_pixbuf = gdk_pixbuf_animation_iter_get_pixbuf(viewer->priv->iter);
            gdk_pixbuf_ref(viewer->priv->src_pixbuf);
            rstto_picture_viewer_refresh(viewer);
            rstto_picture_viewer_paint(viewer);
            */
            }
        }
    }
}

static void
cb_rstto_picture_viewer_closed(GdkPixbufLoader *loader, RsttoPictureViewer *viewer)
{
    GtkWidget *widget = GTK_WIDGET(viewer);

    if (viewer->priv->src_pixbuf)
    {
        gdk_pixbuf_unref(viewer->priv->src_pixbuf);
        viewer->priv->src_pixbuf = NULL;
    }
    if (viewer->priv->iter)
    {
        viewer->priv->src_pixbuf = gdk_pixbuf_animation_iter_get_pixbuf(viewer->priv->iter);
    }
    if (viewer->priv->src_pixbuf)
    {
        viewer->priv->src_pixbuf = gdk_pixbuf_copy(viewer->priv->src_pixbuf);
    }

    rstto_picture_viewer_refresh(viewer);
    rstto_picture_viewer_paint(GTK_WIDGET(viewer));
    if (GTK_WIDGET_REALIZED(widget))
    {
        gdk_window_set_cursor(widget->window, NULL);
    }
}
