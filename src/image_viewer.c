/*
 *  Copyright (c) Stephan Arts 2006-2011 <stephan@xfce.org>
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
#include <gio/gio.h>
#include <libexif/exif-data.h>

#include "image.h"
#include "image_list.h"

#include "image_viewer.h"
#include "settings.h"
#include "main_window.h"
#include "marshal.h"

#ifndef RSTTO_IMAGE_VIEWER_BUFFER_SIZE
/* #define RSTTO_IMAGE_VIEWER_BUFFER_SIZE 1024 */
#define RSTTO_IMAGE_VIEWER_BUFFER_SIZE 131072
#endif

typedef enum
{
    RSTTO_PICTURE_VIEWER_MOTION_STATE_NORMAL = 0,
    RSTTO_PICTURE_VIEWER_MOTION_STATE_BOX_ZOOM,
    RSTTO_PICTURE_VIEWER_MOTION_STATE_MOVE
} RsttoPictureViewerMotionState;

typedef struct _RsttoImageViewerTransaction RsttoImageViewerTransaction;

struct _RsttoImageViewerPriv
{
    GFile                       *file;
    RsttoSettings               *settings;

    RsttoImageViewerTransaction *transaction;
    GdkPixbuf                   *pixbuf;
    GdkPixbuf                   *dst_pixbuf;

    /* Animation data for animated images (like .gif/.mng) */
    /*******************************************************/
    GdkPixbufAnimation     *animation;
    GdkPixbufAnimationIter *iter;
    gint                    animation_timeout_id;

    gdouble                 scale;
    gboolean                auto_scale;

    struct
    {
        gint idle_id;
        gboolean refresh;
    } repaint;

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

    /* CALLBACKS */
    /*************/
    void (*cb_value_changed)(GtkAdjustment *, RsttoImageViewer *);
};


struct _RsttoImageViewerTransaction
{
    RsttoImageViewer *viewer;
    GFile            *file;
    GCancellable     *cancellable;
    GdkPixbufLoader  *loader;

    /* File I/O data */
    /*****************/
    guchar           *buffer;
};


static void
rstto_image_viewer_init(RsttoImageViewer *);
static void
rstto_image_viewer_class_init(RsttoImageViewerClass *);
static void
rstto_image_viewer_destroy(GtkObject *object);

static void
rstto_image_viewer_size_request(GtkWidget *, GtkRequisition *);
static void
rstto_image_viewer_size_allocate(GtkWidget *, GtkAllocation *);
static void
rstto_image_viewer_realize(GtkWidget *);
static gboolean 
rstto_image_viewer_expose(GtkWidget *, GdkEventExpose *);
static void
rstto_image_viewer_paint (GtkWidget *widget);

static gboolean
rstto_image_viewer_set_scroll_adjustments(RsttoImageViewer *, GtkAdjustment *, GtkAdjustment *);
static void
rstto_image_viewer_queued_repaint (RsttoImageViewer *viewer, gboolean);
static void
rstto_image_viewer_paint_checkers (GdkDrawable *drawable, GdkGC *gc, gint width, gint height, gint x_offset, gint y_offset);

static void
cb_rstto_image_viewer_value_changed(GtkAdjustment *adjustment, RsttoImageViewer *viewer);
static void
cb_rstto_image_viewer_read_file_ready (GObject *source_object, GAsyncResult *result, gpointer user_data);
static void
cb_rstto_image_viewer_read_input_stream_ready (GObject *source_object, GAsyncResult *result, gpointer user_data);
static void
cb_rstto_image_loader_area_prepared (GdkPixbufLoader *, RsttoImageViewerTransaction *);
static void
cb_rstto_image_loader_size_prepared (GdkPixbufLoader *, gint , gint , RsttoImageViewerTransaction *);
static void
cb_rstto_image_loader_closed (GdkPixbufLoader *loader, RsttoImageViewerTransaction *transaction);
static gboolean
cb_rstto_image_viewer_update_pixbuf (RsttoImageViewer *viewer);
static gboolean 
cb_rstto_image_viewer_queued_repaint (RsttoImageViewer *viewer);

static void
rstto_image_viewer_load_image (RsttoImageViewer *viewer, GFile *file);
static void
rstto_image_viewer_transaction_free (RsttoImageViewerTransaction *tr);

static GtkWidgetClass *parent_class = NULL;
static GdkScreen      *default_screen = NULL;

GType
rstto_image_viewer_get_type (void)
{
    static GType rstto_image_viewer_type = 0;

    if (!rstto_image_viewer_type)
    {
        static const GTypeInfo rstto_image_viewer_info = 
        {
            sizeof (RsttoImageViewerClass),
            (GBaseInitFunc) NULL,
            (GBaseFinalizeFunc) NULL,
            (GClassInitFunc) rstto_image_viewer_class_init,
            (GClassFinalizeFunc) NULL,
            NULL,
            sizeof (RsttoImageViewer),
            0,
            (GInstanceInitFunc) rstto_image_viewer_init,
            NULL
        };

        rstto_image_viewer_type = g_type_register_static (GTK_TYPE_WIDGET, "RsttoImageViewer", &rstto_image_viewer_info, 0);
    }
    return rstto_image_viewer_type;
}

static void
rstto_image_viewer_init(RsttoImageViewer *viewer)
{
    if (default_screen == NULL)
    {
        default_screen = gdk_screen_get_default();
    }

    viewer->priv = g_new0(RsttoImageViewerPriv, 1);
    viewer->priv->cb_value_changed = cb_rstto_image_viewer_value_changed;
    viewer->priv->settings = rstto_settings_new();

    gtk_widget_set_double_buffered (GTK_WIDGET(viewer), TRUE);

    /* Set to false, experimental...
     * improves performance, but I am not sure what will give.
     */
    gtk_widget_set_redraw_on_allocate(GTK_WIDGET(viewer), FALSE);

    gtk_widget_set_events (GTK_WIDGET(viewer),
                           GDK_BUTTON_PRESS_MASK |
                           GDK_BUTTON_RELEASE_MASK |
                           GDK_BUTTON1_MOTION_MASK |
                           GDK_ENTER_NOTIFY_MASK |
                           GDK_POINTER_MOTION_MASK);

    /*
    gtk_drag_dest_set(GTK_WIDGET(viewer), 0, drop_targets, G_N_ELEMENTS(drop_targets),
                      GDK_ACTION_COPY | GDK_ACTION_LINK | GDK_ACTION_MOVE | GDK_ACTION_PRIVATE);
    */
}

/**
 * rstto_image_viewer_class_init:
 * @viewer_class:
 *
 * Initialize imageviewer class
 */
static void
rstto_image_viewer_class_init(RsttoImageViewerClass *viewer_class)
{
    GtkWidgetClass *widget_class;
    GtkObjectClass *object_class;

    widget_class = (GtkWidgetClass*)viewer_class;
    object_class = (GtkObjectClass*)viewer_class;

    parent_class = g_type_class_peek_parent(viewer_class);

    viewer_class->set_scroll_adjustments = rstto_image_viewer_set_scroll_adjustments;

    widget_class->realize = rstto_image_viewer_realize;
    widget_class->expose_event = rstto_image_viewer_expose;
    widget_class->size_request = rstto_image_viewer_size_request;
    widget_class->size_allocate = rstto_image_viewer_size_allocate;

    object_class->destroy = rstto_image_viewer_destroy;

    widget_class->set_scroll_adjustments_signal =
                  g_signal_new ("set_scroll_adjustments",
                                G_TYPE_FROM_CLASS (object_class),
                                G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
                                G_STRUCT_OFFSET (RsttoImageViewerClass, set_scroll_adjustments),
                                NULL, NULL,
                                rstto_marshal_VOID__OBJECT_OBJECT,
                                G_TYPE_NONE, 2,
                                GTK_TYPE_ADJUSTMENT,
                                GTK_TYPE_ADJUSTMENT);
}

/**
 * rstto_image_viewer_realize:
 * @widget:
 *
 */
static void
rstto_image_viewer_realize(GtkWidget *widget)
{
    GdkWindowAttr attributes;
    gint attributes_mask;

    g_return_if_fail (widget != NULL);
    g_return_if_fail (RSTTO_IS_IMAGE_VIEWER(widget));

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
 * rstto_image_viewer_size_request:
 * @widget:
 * @requisition:
 *
 * Request a default size of 300 by 400 pixels
 */
static void
rstto_image_viewer_size_request(GtkWidget *widget, GtkRequisition *requisition)
{
    requisition->width = 400;
    requisition->height= 300;
}


/**
 * rstto_image_viewer_size_allocate:
 * @widget:
 * @allocation:
 *
 *
 */
static void
rstto_image_viewer_size_allocate(GtkWidget *widget, GtkAllocation *allocation)
{
    RsttoImageViewer *viewer = RSTTO_IMAGE_VIEWER(widget);
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
     * This is required for the occasions the widget is 
     * resized and shrunk, because that won't trigger an expose event.
     */
    rstto_image_viewer_queued_repaint (viewer, TRUE);
}

/**
 * rstto_image_viewer_expose:
 * @widget:
 * @event:
 *
 */
static gboolean
rstto_image_viewer_expose(GtkWidget *widget, GdkEventExpose *event)
{
    RsttoImageViewer *viewer = RSTTO_IMAGE_VIEWER (widget);

    /** 
     * TODO: Check if we really nead a refresh
     */
    rstto_image_viewer_queued_repaint (viewer, TRUE);
    return FALSE;
}

static void
rstto_image_viewer_destroy(GtkObject *object)
{
    RsttoImageViewer *viewer = RSTTO_IMAGE_VIEWER(object);

    if (viewer->priv->settings)
    {
        g_object_unref (viewer->priv->settings);
        viewer->priv->settings = NULL;
    }
}

static gboolean  
rstto_image_viewer_set_scroll_adjustments(RsttoImageViewer *viewer, GtkAdjustment *hadjustment, GtkAdjustment *vadjustment)
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
rstto_image_viewer_paint (GtkWidget *widget)
{

    RsttoImageViewer *viewer = RSTTO_IMAGE_VIEWER (widget);

    GdkColor *bg_color = NULL;
    GValue val_bg_color = {0, }, val_bg_color_override = {0, }, val_bg_color_fs = {0, };
    GdkPixbuf *pixbuf = viewer->priv->dst_pixbuf;
    /** BELOW THIS LINE THE VARIABLE_NAMES GET MESSY **/
    GdkColor line_color;
    GdkPixbuf *n_pixbuf = NULL;
    gint width, height;
    gdouble m_x1, m_x2, m_y1, m_y2;
    gint x1, x2, y1, y2;
    gint i, a;
    /** ABOVE THIS LINE THE VARIABLE_NAMES GET MESSY **/

    g_value_init (&val_bg_color, GDK_TYPE_COLOR);
    g_value_init (&val_bg_color_fs, GDK_TYPE_COLOR);
    g_value_init (&val_bg_color_override, G_TYPE_BOOLEAN);

    g_object_get_property (G_OBJECT(viewer->priv->settings), "bgcolor", &val_bg_color);
    g_object_get_property (G_OBJECT(viewer->priv->settings), "bgcolor-override", &val_bg_color_override);
    g_object_get_property (G_OBJECT(viewer->priv->settings), "bgcolor-fullscreen", &val_bg_color_fs);

    /* required for transparent pixbufs... add double buffering to fix flickering*/
    if(GTK_WIDGET_REALIZED(widget))
    {          
        GdkPixmap *buffer = gdk_pixmap_new(NULL, widget->allocation.width, widget->allocation.height, gdk_drawable_get_depth(widget->window));
        GdkGC *gc = gdk_gc_new(GDK_DRAWABLE(buffer));

        if(gdk_window_get_state(gdk_window_get_toplevel(GTK_WIDGET(viewer)->window)) & GDK_WINDOW_STATE_FULLSCREEN)
        {
           bg_color = g_value_get_boxed (&val_bg_color_fs);
        }
        else
        {
            if (g_value_get_boxed (&val_bg_color) && g_value_get_boolean (&val_bg_color_override))
            {
                bg_color = g_value_get_boxed (&val_bg_color);
            }
            else
            {
                bg_color = &(widget->style->bg[GTK_STATE_NORMAL]);
            }
        }

        gdk_window_set_background (widget->window, bg_color);

        gdk_colormap_alloc_color (gdk_gc_get_colormap (gc), bg_color, FALSE, TRUE);
        gdk_gc_set_rgb_bg_color (gc, bg_color);

        gdk_draw_rectangle(GDK_DRAWABLE(buffer), gc, TRUE, 0, 0, widget->allocation.width, widget->allocation.height);

        /* Check if there is a destination pixbuf */
        if(pixbuf)
        {
            x1 = (widget->allocation.width-gdk_pixbuf_get_width(pixbuf))<0?0:(widget->allocation.width-gdk_pixbuf_get_width(pixbuf))/2;
            y1 = (widget->allocation.height-gdk_pixbuf_get_height(pixbuf))<0?0:(widget->allocation.height-gdk_pixbuf_get_height(pixbuf))/2;
            x2 = gdk_pixbuf_get_width(pixbuf);
            y2 = gdk_pixbuf_get_height(pixbuf);
            
            /* We only need to paint a checkered background if the image is transparent */
            if(gdk_pixbuf_get_has_alpha(pixbuf))
            {
                /* TODO: give these variables correct names */
                rstto_image_viewer_paint_checkers (GDK_DRAWABLE(buffer), gc, x2, y2, x1, y1);
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
        else
        {
            /* HACK HACK HACK HACK */
            guint size = 0;
            if ((GTK_WIDGET (viewer)->allocation.width) < (GTK_WIDGET (viewer)->allocation.height))
            {
                size = GTK_WIDGET (viewer)->allocation.width;
            }
            else
            {
                size = GTK_WIDGET (viewer)->allocation.height;
            }
            pixbuf = gtk_icon_theme_load_icon (gtk_icon_theme_get_default(), 
                                               "ristretto", 
                                               (size*0.8),
                                               GTK_ICON_LOOKUP_FORCE_SIZE, NULL);
            if (pixbuf)
            {
                gdk_pixbuf_saturate_and_pixelate (pixbuf, pixbuf, 0, TRUE);
                n_pixbuf = gdk_pixbuf_composite_color_simple (pixbuf, (size*0.8), (size*0.8), GDK_INTERP_BILINEAR, 40, 40, bg_color->pixel, bg_color->pixel);
                g_object_unref (pixbuf);
                pixbuf = n_pixbuf;

                x1 = (widget->allocation.width-gdk_pixbuf_get_width(pixbuf))<0?0:(widget->allocation.width-gdk_pixbuf_get_width(pixbuf))/2;
                y1 = (widget->allocation.height-gdk_pixbuf_get_height(pixbuf))<0?0:(widget->allocation.height-gdk_pixbuf_get_height(pixbuf))/2;
                x2 = gdk_pixbuf_get_width(pixbuf);
                y2 = gdk_pixbuf_get_height(pixbuf);

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
rstto_image_viewer_paint_checkers (GdkDrawable *drawable, GdkGC *gc, gint width, gint height, gint x_offset, gint y_offset)
{
    gint x, y;
    gint block_width, block_height;
    GdkColor color;

    /* This is to remind me of a bug in this function, 
     * the top-left square is colored red, it shouldn't
     */
    color.pixel = 0xeeee0000;

    for(x = 0; x <= width/10; x++)
    {
        if(x == width/10)
        {
            block_width = width-10*x;
        }
        else
        {   
            block_width = 10;
        }
        for(y = 0; y <= height/10; y++)
        {
            gdk_gc_set_foreground(gc, &color);
            if(y == height/10)
            {
                block_height = height-10*y;
            }
            else
            {   
                block_height = 10;
            }

            if((y%2?x%2:!(x%2))) {
                color.pixel = 0xcccccccc;
            }
            else
            {
                color.pixel = 0xdddddddd;
            }

            gdk_draw_rectangle(GDK_DRAWABLE(drawable),
                            gc,
                            TRUE,
                            x_offset+10*x,
                            y_offset+10*y,
                            block_width,
                            block_height);
        }
    }
    
}

static void
rstto_image_viewer_queued_repaint (RsttoImageViewer *viewer, gboolean refresh)
{
    if (viewer->priv->repaint.idle_id > 0)
    {
        g_source_remove(viewer->priv->repaint.idle_id);
    }
    if (refresh)
    {
        viewer->priv->repaint.refresh = TRUE;
    }
    viewer->priv->repaint.idle_id = g_idle_add((GSourceFunc)cb_rstto_image_viewer_queued_repaint, viewer);
}


GtkWidget *
rstto_image_viewer_new (void)
{
    GtkWidget *widget;

    widget = g_object_new(RSTTO_TYPE_IMAGE_VIEWER, NULL);

    return widget;
}

/*
 * rstto_image_viewer_set_file:
 * @viewer:
 * @file:
 *
 * Set the file that should be displayed in the image_viewer.
 * 
 * TODO:
 *  - cancellable...
 */
void
rstto_image_viewer_set_file (RsttoImageViewer *viewer, GFile *file)
{

    /*
     * Check if a file is set, or unset.
     */
    if (file)
    {
        /*
         * Should the 'old' file be cleaned up?
         */
        if (viewer->priv->file)
        {
            /*
             * If the old, and new file are equal, do nothing.
             */
            if (!g_file_equal (viewer->priv->file, file))
            {
                /*
                 * This will first need to return to the 'main' loop before it cleans up after itself.
                 * We can forget about the transaction, once it's cancelled, it will clean-up itself. -- (it should)
                 */
                if (viewer->priv->transaction)
                {
                    g_cancellable_cancel (viewer->priv->transaction->cancellable);
                    viewer->priv->transaction = NULL;
                } 

                g_object_unref (viewer->priv->file);
                viewer->priv->file = g_file_dup(file);
                viewer->priv->scale = -1;
                rstto_image_viewer_load_image (viewer, viewer->priv->file);
            }
        }
        else
        {
            viewer->priv->file = g_file_dup(file);
            viewer->priv->scale = -1;
            rstto_image_viewer_load_image (viewer, viewer->priv->file);
        }
    } 
    else
    {
        if (viewer->priv->file)
        {
            g_object_unref (viewer->priv->file);
            viewer->priv->file = NULL;
            if (viewer->priv->pixbuf)
            {
                g_object_unref (viewer->priv->pixbuf);
                viewer->priv->pixbuf = NULL;
            }
            rstto_image_viewer_queued_repaint (viewer, TRUE);
        }
    }
}

static void
rstto_image_viewer_load_image (RsttoImageViewer *viewer, GFile *file)
{

    /*
     * This will first need to return to the 'main' loop before it cleans up after itself.
     * We can forget about the transaction, once it's cancelled, it will clean-up itself. -- (it should)
     */
    if (viewer->priv->transaction)
    {
        g_cancellable_cancel (viewer->priv->transaction->cancellable);
        viewer->priv->transaction = NULL;
    }

    RsttoImageViewerTransaction *transaction = g_new0 (RsttoImageViewerTransaction, 1);

    transaction->loader = gdk_pixbuf_loader_new();
    transaction->cancellable = g_cancellable_new();
    transaction->buffer = g_new0 (guchar, RSTTO_IMAGE_VIEWER_BUFFER_SIZE);
    transaction->file = file;
    transaction->viewer = viewer;

    g_signal_connect(transaction->loader, "area-prepared", G_CALLBACK(cb_rstto_image_loader_area_prepared), transaction);
    g_signal_connect(transaction->loader, "size-prepared", G_CALLBACK(cb_rstto_image_loader_size_prepared), transaction);
    g_signal_connect(transaction->loader, "closed", G_CALLBACK(cb_rstto_image_loader_closed), transaction);

    viewer->priv->transaction = transaction;

    g_file_read_async (transaction->file,
                       0,
                       transaction->cancellable,
                       (GAsyncReadyCallback)cb_rstto_image_viewer_read_file_ready,
                       transaction);
}

static void
rstto_image_viewer_transaction_free (RsttoImageViewerTransaction *tr)
{
    /*
     * Check if this transaction is current,
     * if so, remove the reference from the viewer.
     */
    if (tr->viewer->priv->transaction == tr)
    {
        tr->viewer->priv->transaction = NULL;
    }
    g_object_unref (tr->cancellable);
    g_object_unref (tr->loader);
    g_object_unref (tr->file);
    g_free (tr->buffer);
    g_free (tr);
}

void
rstto_image_viewer_set_scale (RsttoImageViewer *viewer, gdouble scale)
{
    viewer->priv->scale = scale;
    if (scale == 0)
    {
        viewer->priv->auto_scale = TRUE;
    }
    else
    {
        viewer->priv->auto_scale = FALSE;
    }

    rstto_image_viewer_queued_repaint (viewer, TRUE);
}

gdouble
rstto_image_viewer_get_scale (RsttoImageViewer *viewer)
{
    return viewer->priv->scale;
}


/************************/
/** CALLBACK FUNCTIONS **/
/************************/

static void
cb_rstto_image_viewer_value_changed(GtkAdjustment *adjustment, RsttoImageViewer *viewer)
{
}

static void
cb_rstto_image_viewer_read_file_ready (GObject *source_object, GAsyncResult *result, gpointer user_data)
{
    GFile *file = G_FILE (source_object);
    RsttoImageViewerTransaction *transaction = (RsttoImageViewerTransaction *)user_data;

    GFileInputStream *file_input_stream = g_file_read_finish (file, result, NULL);

    /* TODO: FIXME -- Clean up the transaction*/
    if (file_input_stream == NULL)
    {
        rstto_image_viewer_transaction_free (transaction);
        return;
    }

    g_input_stream_read_async (G_INPUT_STREAM (file_input_stream),
                               transaction->buffer,
                               RSTTO_IMAGE_VIEWER_BUFFER_SIZE,
                               G_PRIORITY_DEFAULT,
                               transaction->cancellable,
                               (GAsyncReadyCallback) cb_rstto_image_viewer_read_input_stream_ready,
                               transaction);
}

static void
cb_rstto_image_viewer_read_input_stream_ready (GObject *source_object, GAsyncResult *result, gpointer user_data)
{
    GError *error = NULL;
    RsttoImageViewerTransaction *transaction = (RsttoImageViewerTransaction *)user_data;
    gssize read_bytes = g_input_stream_read_finish (G_INPUT_STREAM (source_object), result, &error);

    /* TODO: FIXME -- Clean up the transaction*/
    if (read_bytes == -1)
    {
        gdk_pixbuf_loader_close (transaction->loader, NULL);
        rstto_image_viewer_transaction_free (transaction);
        return;
    }

    if (read_bytes > 0)
    {
        if(gdk_pixbuf_loader_write (transaction->loader, (const guchar *)transaction->buffer, read_bytes, &error) == FALSE)
        {
            g_input_stream_close (G_INPUT_STREAM (source_object), NULL, NULL);

            gdk_pixbuf_loader_close (transaction->loader, NULL);
            rstto_image_viewer_transaction_free (transaction);
        }
        else
        {
            g_input_stream_read_async (G_INPUT_STREAM (source_object),
                                       transaction->buffer,
                                       RSTTO_IMAGE_VIEWER_BUFFER_SIZE,
                                       G_PRIORITY_DEFAULT,
                                       transaction->cancellable,
                                       (GAsyncReadyCallback) cb_rstto_image_viewer_read_input_stream_ready,
                                       transaction);
        }
    }
    else {
        /* Loading complete, transaction should not be free-ed */
        g_input_stream_close (G_INPUT_STREAM (source_object), NULL, NULL);
        gdk_pixbuf_loader_close (transaction->loader, NULL);

        if (g_cancellable_is_cancelled(transaction->cancellable))
        {
            rstto_image_viewer_transaction_free (transaction);
        }
    }
}


static void
cb_rstto_image_loader_area_prepared (GdkPixbufLoader *loader, RsttoImageViewerTransaction *transaction)
{
    gint timeout = 0;
    RsttoImageViewer *viewer = transaction->viewer;

    if (viewer->priv->transaction == transaction)
    {
        viewer->priv->animation = gdk_pixbuf_loader_get_animation (loader);
        viewer->priv->iter = gdk_pixbuf_animation_get_iter (viewer->priv->animation, NULL);

        g_object_ref (viewer->priv->animation);

        timeout = gdk_pixbuf_animation_iter_get_delay_time (viewer->priv->iter);
    }

    if (timeout != -1)
    {
        /* fix borked stuff */
        if (timeout == 0)
        {
            g_warning("timeout == 0: defaulting to 40ms");
            timeout = 40;
        }

        viewer->priv->animation_timeout_id = g_timeout_add(timeout, (GSourceFunc)cb_rstto_image_viewer_update_pixbuf, viewer);
    }   
    else
    {
        /* This is a single-frame image, there is no need to copy the pixbuf since it won't change.
         */
        viewer->priv->pixbuf = gdk_pixbuf_animation_iter_get_pixbuf (viewer->priv->iter);
        g_object_ref (viewer->priv->pixbuf);
    }
}

static void
cb_rstto_image_loader_size_prepared (GdkPixbufLoader *loader, gint width, gint height, RsttoImageViewerTransaction *transaction)
{
    gint s_width = gdk_screen_get_width (default_screen);
    gint s_height = gdk_screen_get_height (default_screen);

    /*
     * Set the maximum size of the loaded image to the screen-size.
     * TODO: Add some 'smart-stuff' here
     */
    if (s_width < width || s_height < height)
    {
        gdk_pixbuf_loader_set_size (loader, s_width, s_height); 
    }

}

static void
cb_rstto_image_loader_closed (GdkPixbufLoader *loader, RsttoImageViewerTransaction *transaction)
{

    rstto_image_viewer_queued_repaint (transaction->viewer, TRUE);
}

static gboolean
cb_rstto_image_viewer_update_pixbuf (RsttoImageViewer *viewer)
{
    gint timeout = 0;

    if (viewer->priv->iter)
    {
        if(gdk_pixbuf_animation_iter_advance (viewer->priv->iter, NULL))
        {
            /* Cleanup old image */
            if (viewer->priv->pixbuf)
            {
                g_object_unref (viewer->priv->pixbuf);
                viewer->priv->pixbuf = NULL;
            }

            /* The pixbuf returned by the GdkPixbufAnimationIter might be reused, 
             * lets make a copy for myself just in case
             */
            viewer->priv->pixbuf = gdk_pixbuf_copy (gdk_pixbuf_animation_iter_get_pixbuf (viewer->priv->iter));
        }

        timeout = gdk_pixbuf_animation_iter_get_delay_time (viewer->priv->iter);

        if (timeout != -1)
        {
            if (timeout == 0)
            {
                g_warning("timeout == 0: defaulting to 40ms");
                timeout = 40;
            }
            viewer->priv->animation_timeout_id = g_timeout_add(timeout, (GSourceFunc)cb_rstto_image_viewer_update_pixbuf, viewer);
        }

        rstto_image_viewer_queued_repaint (viewer, TRUE);

        return FALSE;
    }
    return FALSE;
}

static gboolean 
cb_rstto_image_viewer_queued_repaint (RsttoImageViewer *viewer)
{
    gint width, height;
    g_source_remove (viewer->priv->repaint.idle_id);
    viewer->priv->repaint.idle_id = -1;
    viewer->priv->repaint.refresh = FALSE;

    if (viewer->priv->pixbuf)
    {
        width = gdk_pixbuf_get_width (viewer->priv->pixbuf);
        height = gdk_pixbuf_get_height (viewer->priv->pixbuf);

        /*
         * Scale == -1, this means the image is not loaded before, 
         * and it should be rendered at the best possible quality.
         * This means:
         *      - Images smaller then the widget-size: at 100%
         *      - Images larger then the widget-size, scaled to fit.
         */
        if (viewer->priv->scale == -1)
        {
            /**
             * Check if the image should scale to the horizontal
             * or vertical dimensions of the widget.
             *
             * This is done by calculating the ratio between the 
             * widget-dimensions and image-dimensions, the smallest 
             * one is used for scaling the image.
             */
            if ((gdouble)(GTK_WIDGET(viewer)->allocation.width/(gdouble)width) > 
                (gdouble)(GTK_WIDGET(viewer)->allocation.height/(gdouble)height))
            { 
                viewer->priv->scale = (gdouble)(GTK_WIDGET (viewer)->allocation.height) / (gdouble)height;
            }
            else
            {
                viewer->priv->scale = (gdouble)(GTK_WIDGET (viewer)->allocation.width) / (gdouble)width;
            }

            viewer->priv->auto_scale = TRUE;

            /*
             * If the scale is greater then 1.0 (meaning the image is smaller then the window)
             * reset the scale to 1.0, so the image won't be blown-up.
             * Also, disable 'auto_scale'.
             */
            if (viewer->priv->scale > 1.0)
            {
                viewer->priv->scale = 1.0;
                viewer->priv->auto_scale = FALSE;
            }
        }
        else
        {
            /*
             * if auto_scale == true, calculate the scale
             */
            if (viewer->priv->auto_scale)
            {
                /**
                 * Check if the image should scale to the horizontal
                 * or vertical dimensions of the widget.
                 *
                 * This is done by calculating the ratio between the 
                 * widget-dimensions and image-dimensions, the smallest 
                 * one is used for scaling the image.
                 */
                if ((gdouble)(GTK_WIDGET(viewer)->allocation.width/(gdouble)width) > 
                    (gdouble)(GTK_WIDGET(viewer)->allocation.height/(gdouble)height))
                { 
                    viewer->priv->scale = (gdouble)(GTK_WIDGET (viewer)->allocation.height) / (gdouble)height;
                }
                else
                {
                    viewer->priv->scale = (gdouble)(GTK_WIDGET (viewer)->allocation.width) / (gdouble)width;
                }
            }
        }

        viewer->priv->dst_pixbuf = gdk_pixbuf_scale_simple (viewer->priv->pixbuf,
                                   (gint)((gdouble)width*viewer->priv->scale),
                                   (gint)((gdouble)height*viewer->priv->scale),
                                   GDK_INTERP_BILINEAR);
    }
    else
    {
        if (viewer->priv->dst_pixbuf)
        {
            g_object_unref (viewer->priv->dst_pixbuf);
            viewer->priv->dst_pixbuf = NULL;
        }
    }
    rstto_image_viewer_paint (GTK_WIDGET (viewer));
}
