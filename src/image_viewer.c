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

#include "image_viewer.h"
#include "settings.h"
#include "marshal.h"

#ifndef RSTTO_IMAGE_VIEWER_BUFFER_SIZE
/* #define RSTTO_IMAGE_VIEWER_BUFFER_SIZE 1024 */
#define RSTTO_IMAGE_VIEWER_BUFFER_SIZE 131072
#endif

#ifndef RSTTO_MAX_SCALE
#define RSTTO_MAX_SCALE 4.0
#endif

typedef enum
{
    RSTTO_IMAGE_VIEWER_MOTION_STATE_NORMAL = 0,
    RSTTO_IMAGE_VIEWER_MOTION_STATE_BOX_ZOOM,
    RSTTO_IMAGE_VIEWER_MOTION_STATE_MOVE
} RsttoImageViewerMotionState;

typedef struct _RsttoImageViewerTransaction RsttoImageViewerTransaction;

struct _RsttoImageViewerPriv
{
    GFile                       *file;
    RsttoSettings               *settings;
    GdkVisual                   *visual;
    GdkColormap                 *colormap;

    RsttoImageViewerTransaction *transaction;
    GdkPixbuf                   *pixbuf;
    GdkPixbuf                   *dst_pixbuf;
    RsttoImageViewerOrientation  orientation;
    gdouble                      quality;


    GtkMenu                     *menu;
    /* */
    /***/
    gboolean                     revert_zoom_direction;

    /* Animation data for animated images (like .gif/.mng) */
    /*******************************************************/
    GdkPixbufAnimation     *animation;
    GdkPixbufAnimationIter *iter;
    gint                    animation_timeout_id;

    gdouble                 scale;
    gboolean                auto_scale;

    gdouble                 image_scale;
    gint                    image_width;
    gint                    image_height;

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
        RsttoImageViewerMotionState state;
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

    gint              image_width;
    gint              image_height;
    gdouble           image_scale;
    gdouble           scale;

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

static void
rstto_image_viewer_set_motion_state (RsttoImageViewer *viewer, RsttoImageViewerMotionState state);
static RsttoImageViewerMotionState
rstto_image_viewer_get_motion_state (RsttoImageViewer *viewer);

static gboolean
rstto_image_viewer_set_scroll_adjustments(RsttoImageViewer *, GtkAdjustment *, GtkAdjustment *);
static void
rstto_image_viewer_queued_repaint (RsttoImageViewer *viewer, gboolean);
static void
rstto_image_viewer_paint_checkers (RsttoImageViewer *viewer, GdkDrawable *drawable, GdkGC *gc, gint width, gint height, gint x_offset, gint y_offset);
static void
rstto_image_viewer_paint_selection (RsttoImageViewer *viewer, GdkDrawable *drawable, GdkGC *gc, gint width, gint height, gint x_offset, gint y_offset);


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

static gboolean
rstto_scroll_event (
        GtkWidget *widget,
        GdkEventScroll *event);

static gboolean 
rstto_motion_notify_event (
        GtkWidget *widget,
        GdkEventMotion *event);

static gboolean
rstto_button_press_event (
        GtkWidget *widget,
        GdkEventButton *event);

static gboolean
rstto_button_release_event (
        GtkWidget *widget,
        GdkEventButton *event);
static gboolean
rstto_popup_menu (
        GtkWidget *widget);

static void
cb_rstto_bgcolor_changed (
        GObject *settings,
        GParamSpec *pspec,
        gpointer user_data);
static void
cb_rstto_zoom_direction_changed (
        GObject *settings,
        GParamSpec *pspec,
        gpointer user_data);

static void
rstto_image_viewer_load_image (
        RsttoImageViewer *viewer,
        GFile *file,
        gdouble scale);
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
    viewer->priv->image_width = 0;
    viewer->priv->image_height = 0;
    viewer->priv->visual = gdk_rgb_get_visual();
    viewer->priv->colormap = gdk_colormap_new (viewer->priv->visual, TRUE);

    g_signal_connect (
            G_OBJECT(viewer->priv->settings),
            "notify::bgcolor",
            G_CALLBACK (cb_rstto_bgcolor_changed),
            viewer);

    g_signal_connect (
            G_OBJECT(viewer->priv->settings),
            "notify::bgcolor-override",
            G_CALLBACK (cb_rstto_bgcolor_changed),
            viewer);
    g_signal_connect (
            G_OBJECT(viewer->priv->settings),
            "notify::revert-zoom-direction",
            G_CALLBACK (cb_rstto_zoom_direction_changed),
            viewer);

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
    widget_class->scroll_event = rstto_scroll_event;

    widget_class->button_press_event = rstto_button_press_event;
    widget_class->button_release_event = rstto_button_release_event;
    widget_class->motion_notify_event = rstto_motion_notify_event;
    widget_class->popup_menu = rstto_popup_menu;

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

        gtk_adjustment_set_page_size (viewer->hadjustment, ((gdouble)allocation->width));
        gtk_adjustment_set_page_size (viewer->vadjustment, ((gdouble)allocation->height));
    }

    /** 
     * TODO: Check if we really nead a refresh
     * This is required for the occasions the widget is 
     * resized and shrunk, because that apparently won't trigger an expose event.
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
        gtk_adjustment_set_lower (viewer->hadjustment, 0);
        gtk_adjustment_set_upper (viewer->hadjustment, 0);

        g_signal_connect(G_OBJECT(viewer->hadjustment), "value-changed", (GCallback)viewer->priv->cb_value_changed, viewer);
        g_object_ref(viewer->hadjustment);
    }
    if(viewer->vadjustment)
    {
        gtk_adjustment_set_lower (viewer->vadjustment, 0);
        gtk_adjustment_set_upper (viewer->vadjustment, 0);

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

    /*
     * Variables used for the selection-box.
     */
    gint box_x, box_y;
    gint box_width, box_height;

    /** BELOW THIS LINE THE VARIABLE_NAMES GET MESSY **/
    GdkColor line_color;
    GdkPixbuf *n_pixbuf = NULL;
    gint width, height;
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

        /*
         * Create a buffer to draw on
         */
        GdkPixmap *buffer = gdk_pixmap_new(NULL, widget->allocation.width, widget->allocation.height, gdk_drawable_get_depth(widget->window));
        GdkGC *gc = gdk_gc_new(GDK_DRAWABLE(buffer));

        /*
         * Determine the background-color and draw the background.
         */

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
        gdk_gc_set_foreground (gc, bg_color);

        gdk_draw_rectangle(GDK_DRAWABLE(buffer), gc, TRUE, 0, 0, widget->allocation.width, widget->allocation.height);


        /*
         * Check if there is a source image
         */
        if(pixbuf)
        {
            /*
             * Draw the image on the background
             */
            x1 = (widget->allocation.width-gdk_pixbuf_get_width(pixbuf))<0?0:(widget->allocation.width-gdk_pixbuf_get_width(pixbuf))/2;
            y1 = (widget->allocation.height-gdk_pixbuf_get_height(pixbuf))<0?0:(widget->allocation.height-gdk_pixbuf_get_height(pixbuf))/2;
            x2 = gdk_pixbuf_get_width(pixbuf);
            y2 = gdk_pixbuf_get_height(pixbuf);
            
            /* We only need to paint a checkered background if the image is transparent */
            if(gdk_pixbuf_get_has_alpha(pixbuf))
            {
                /* TODO: give these variables correct names */
                rstto_image_viewer_paint_checkers (viewer, GDK_DRAWABLE(buffer), gc, x2, y2, x1, y1);
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

            if(viewer->priv->motion.state == RSTTO_IMAGE_VIEWER_MOTION_STATE_BOX_ZOOM)
            {
                /*
                 * The user can create a selection-box when dragging from four corners.
                 * Check if the endpoint is before or after the starting-point,
                 * calculate the offset and box-size accordingly.
                 * 
                 * Perform the calculations for the vertical movement.
                 */
                if (viewer->priv->motion.y < viewer->priv->motion.current_y)
                {
                    box_y = viewer->priv->motion.y;
                    box_height = viewer->priv->motion.current_y - box_y;
                }
                else
                {
                    box_y = viewer->priv->motion.current_y;
                    box_height = viewer->priv->motion.y - box_y;
                }

                /*
                 * Above comment applies here aswell, for the horizontal movement.
                 */
                if (viewer->priv->motion.x < viewer->priv->motion.current_x)
                {
                    box_x = viewer->priv->motion.x;
                    box_width = viewer->priv->motion.current_x - box_x;
                }
                else
                {
                    box_x = viewer->priv->motion.current_x;
                    box_width = viewer->priv->motion.x - box_x;
                }

                /*
                 * Finally, paint the selection-box.
                 */
                rstto_image_viewer_paint_selection (viewer, GDK_DRAWABLE(buffer), gc, box_width, box_height, box_x, box_y);
            }
        }
        else
        {
            /*
             * There is no image, draw the ristretto icon on the background
             */
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

                /* Cleanup pixbuf */
                g_object_unref (pixbuf);
                pixbuf = NULL;
            }
        }

        /*
         * Draw the buffer on the window
         */

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
rstto_image_viewer_paint_checkers (RsttoImageViewer *viewer,
                                   GdkDrawable *drawable,
                                   GdkGC *gc,
                                   gint width,
                                   gint height,
                                   gint x_offset,
                                   gint y_offset)
{
    gint x, y;
    gint block_width, block_height;
    GdkColor color_a;
    GdkColor color_b;

    color_a.red   = 0xcccc;
    color_a.green = 0xcccc;
    color_a.blue  = 0xcccc;

    color_b.red   = 0xdddd;
    color_b.green = 0xdddd;
    color_b.blue  = 0xdddd;

    gdk_colormap_alloc_color (viewer->priv->colormap, &color_a, FALSE, TRUE);
    gdk_colormap_alloc_color (viewer->priv->colormap, &color_b, FALSE, TRUE);

    for(x = 0; x <= width/10; ++x)
    {
        if(x == width/10)
        {
            block_width = width-10*x;
        }
        else
        {   
            block_width = 10;
        }
        for(y = 0; y <= height/10; ++y)
        {
            if(y == height/10)
            {
                block_height = height-10*y;
            }
            else
            {   
                block_height = 10;
            }

            if ((y%2?x%2:!(x%2)))
            {
                gdk_gc_set_foreground(gc, &color_a);
            }
            else
            {
                gdk_gc_set_foreground(gc, &color_b);
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
rstto_image_viewer_paint_selection (RsttoImageViewer *viewer, GdkDrawable *drawable, GdkGC *gc, gint width, gint height, gint x_offset, gint y_offset)
{
    GtkWidget *widget = GTK_WIDGET(viewer);
    gint pixbuf_width;
    gint pixbuf_height;
    gint pixbuf_x_offset;
    gint pixbuf_y_offset;

    gint rel_x_offset;
    gint rel_y_offset;

    gdk_gc_set_foreground(gc,
            &(widget->style->fg[GTK_STATE_SELECTED]));

    pixbuf_width = gdk_pixbuf_get_width(viewer->priv->dst_pixbuf);
    pixbuf_height = gdk_pixbuf_get_height(viewer->priv->dst_pixbuf);
    pixbuf_x_offset = ((widget->allocation.width - pixbuf_width)/2);
    pixbuf_y_offset = ((widget->allocation.height - pixbuf_height)/2);

    /*
     * Constrain the selection-box to the left
     * and top sides of the image.
     */
    if (x_offset < pixbuf_x_offset)
    {
        width -= (pixbuf_x_offset - x_offset);
        x_offset = pixbuf_x_offset;
    }
    if (y_offset < pixbuf_y_offset)
    {
        height -= (pixbuf_y_offset - y_offset);
        y_offset = pixbuf_y_offset;
    }

    /*
     * Constrain the selection-box to the right
     * and bottom sides of the image.
     */
    if ((x_offset + width) > (pixbuf_x_offset+pixbuf_width))
    {
        width = (pixbuf_x_offset+pixbuf_width)-x_offset;
    }
    if ((y_offset + height) > (pixbuf_y_offset+pixbuf_height))
    {
        height = (pixbuf_y_offset+pixbuf_height)-y_offset;
    }


    /*
     * Calculate the offset relative to the position 
     * of the image in the window.
     */
    rel_x_offset = x_offset - pixbuf_x_offset;
    rel_y_offset = y_offset - pixbuf_y_offset;



    if ((width > 0) && (height > 0))
    {

        GdkPixbuf *sub = gdk_pixbuf_new_subpixbuf(viewer->priv->dst_pixbuf,
                                                 rel_x_offset,
                                                 rel_y_offset,
                                                 x_offset + width>(pixbuf_x_offset+pixbuf_width)?pixbuf_width-rel_x_offset:width,
                                                 y_offset + height>(pixbuf_y_offset+pixbuf_height)?pixbuf_height-rel_y_offset:height);
        if(sub)
        {
            sub = gdk_pixbuf_composite_color_simple(sub,
                                              x_offset + width>(pixbuf_x_offset+pixbuf_width)?pixbuf_width-rel_x_offset:width,
                                              y_offset + height>(pixbuf_y_offset+pixbuf_height)?pixbuf_height-rel_y_offset:height,
                                              GDK_INTERP_BILINEAR,
                                              200,
                                              200,
                                              widget->style->bg[GTK_STATE_SELECTED].pixel,
                                              widget->style->bg[GTK_STATE_SELECTED].pixel);

            gdk_draw_pixbuf(drawable,
                            gc,
                            sub,
                            0,0,
                            x_offset,
                            y_offset,
                            -1, -1,
                            GDK_RGB_DITHER_NONE,
                            0, 0);

            g_object_unref(sub);
            sub = NULL;
        }

        gdk_draw_rectangle(drawable,
                        gc,
                        FALSE,
                        x_offset,
                        y_offset,
                        width,
                        height);
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
rstto_image_viewer_set_file (RsttoImageViewer *viewer,
                             GFile *file,
                             gdouble scale,
                             RsttoImageViewerOrientation orientation)
{
    
    /*
     * Set the image-orientation
     */
    viewer->priv->orientation = orientation;

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
                    if (FALSE == g_cancellable_is_cancelled (viewer->priv->transaction->cancellable))
                    {
                        g_cancellable_cancel (viewer->priv->transaction->cancellable);
                    }
                    viewer->priv->transaction = NULL;
                } 

                g_object_unref (viewer->priv->file);
                viewer->priv->file = g_file_dup(file);
                rstto_image_viewer_load_image (
                        viewer,
                        viewer->priv->file,
                        scale);
            }
        }
        else
        {
            viewer->priv->file = g_file_dup(file);
            rstto_image_viewer_load_image (viewer, viewer->priv->file, scale); }
    } 
    else
    {
        if (viewer->priv->iter)
        {
            g_object_unref (viewer->priv->iter);
            viewer->priv->iter = NULL;
        }
        if (viewer->priv->animation)
        {
            g_object_unref (viewer->priv->animation);
            viewer->priv->animation = NULL;
        }
        if (viewer->priv->pixbuf)
        {
            g_object_unref (viewer->priv->pixbuf);
            viewer->priv->pixbuf = NULL;
        }
        if (viewer->priv->transaction)
        {
            if (FALSE == g_cancellable_is_cancelled (viewer->priv->transaction->cancellable))
            {
                g_cancellable_cancel (viewer->priv->transaction->cancellable);
            }
            viewer->priv->transaction = NULL;
        }
        if (viewer->priv->file)
        {
            g_object_unref (viewer->priv->file);
            viewer->priv->file = NULL;
            rstto_image_viewer_queued_repaint (viewer, TRUE);
        }
    }
}

static void
rstto_image_viewer_load_image (
        RsttoImageViewer *viewer,
        GFile *file,
        gdouble scale)
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
    transaction->scale = scale;

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
    g_free (tr->buffer);
    g_free (tr);
}

void
rstto_image_viewer_set_scale (RsttoImageViewer *viewer, gdouble scale)
{
    gdouble tmp_x, tmp_y;
    gint pixbuf_width = 0;
    gint pixbuf_height = 0;
    gint pixbuf_x_offset = 0;
    gint pixbuf_y_offset = 0;
    gboolean auto_scale = FALSE;

    if (scale == 0)
    {
        viewer->priv->auto_scale = TRUE;
    }
    else
    {
        /*
         * If the image is larger then the screen-dimensions,
         * there is no reason to zoom in beyond RSTTO_MAX_SCALE.
         *
         * If the image is smaller then the screen-dimensions,
         * zoom in to RSTTO_MAX_SCALE OR the window-size, whichever comes last.
         *
         */
        if (viewer->priv->image_scale < 1.0)
        {
            if (scale > RSTTO_MAX_SCALE)
            {
                scale = RSTTO_MAX_SCALE;
            }
        }
        else
        {
            /*
             * Assuming image_scale == 1.0
             */
            gdouble v_scale = (gdouble)(GTK_WIDGET (viewer)->allocation.height) / (gdouble)viewer->priv->image_height;
            gdouble h_scale = (gdouble)(GTK_WIDGET (viewer)->allocation.width) / (gdouble)viewer->priv->image_width;

            if ((h_scale > RSTTO_MAX_SCALE) || (v_scale > RSTTO_MAX_SCALE))
            {
                if(h_scale < v_scale)
                {
                    /* If the image is scaled beyond the window-size, 
                     * force the scale to fit the window and set auto_scale = TRUE.
                     */
                    if (scale > h_scale)
                    {
                        auto_scale = TRUE;
                        scale = h_scale;
                    }
                }
                else
                {
                    /* If the image is scaled beyond the window-size, 
                     * force the scale to fit the window and set auto_scale = TRUE.
                     */
                    if (scale > v_scale)
                    {
                        auto_scale = TRUE;
                        scale = v_scale;
                    }
                }
            }
            else
            {
                if (scale > RSTTO_MAX_SCALE)
                {
                    scale = RSTTO_MAX_SCALE;
                }
            }
        }

        /*
         * There is no need to zoom out beyond 32x32 pixels
         * unless, ofcourse the image itself is smaller then 32x32 pixels.
         */
        if (viewer->priv->image_width > viewer->priv->image_height)
        {
            if ((viewer->priv->image_width >= 32) && ((scale * viewer->priv->image_width) < 32))
            {
                scale = (32.0 / (gdouble)viewer->priv->image_width);
            }
            if ((viewer->priv->image_width < 32) && (scale < 1.0))
            {
                scale = 1.0; 
            }
        }
        else
        {
            if ((viewer->priv->image_height >= 32) && ((scale * viewer->priv->image_height) < 32))
            {
                scale = (32.0 / (gdouble)viewer->priv->image_height);
            }
            if ((viewer->priv->image_height < 32) && (scale < 1.0))
            {
                scale = 1.0; 
            }
        }

        viewer->priv->auto_scale = auto_scale;

        if (viewer->priv->dst_pixbuf)
        {
            pixbuf_width = gdk_pixbuf_get_width(viewer->priv->dst_pixbuf);
            pixbuf_height = gdk_pixbuf_get_height(viewer->priv->dst_pixbuf);
            pixbuf_x_offset = ((GTK_WIDGET(viewer)->allocation.width - pixbuf_width)/2);
            pixbuf_y_offset = ((GTK_WIDGET(viewer)->allocation.height - pixbuf_height)/2);
        }

        /*
         * Prevent the adjustments from emitting the 'changed' signal,
         * this way both the upper-limit and value can be changed before the
         * rest of the application is informed.
         */
        g_object_freeze_notify(G_OBJECT(viewer->hadjustment));
        g_object_freeze_notify(G_OBJECT(viewer->vadjustment));


        gtk_adjustment_set_upper (viewer->hadjustment, ((gdouble)viewer->priv->image_width)*scale);
        gtk_adjustment_set_upper (viewer->vadjustment, ((gdouble)viewer->priv->image_height)*scale);
        
        /*
         * When zooming in or out, 
         * try keeping the center of the viewport in the center.
         */
        tmp_y = (gtk_adjustment_get_value(viewer->vadjustment) + 
                        (gtk_adjustment_get_page_size (viewer->vadjustment) / 2) - pixbuf_y_offset) / viewer->priv->scale;
        gtk_adjustment_set_value (viewer->vadjustment, (tmp_y*scale - (gtk_adjustment_get_page_size(viewer->vadjustment)/2)));


        tmp_x = (gtk_adjustment_get_value(viewer->hadjustment) +
                        (gtk_adjustment_get_page_size (viewer->hadjustment) / 2) - pixbuf_x_offset) / viewer->priv->scale;
        gtk_adjustment_set_value (viewer->hadjustment, (tmp_x*scale - (gtk_adjustment_get_page_size(viewer->hadjustment)/2)));

        /*
         * Enable signals on the adjustments.
         */
        g_object_thaw_notify(G_OBJECT(viewer->vadjustment));
        g_object_thaw_notify(G_OBJECT(viewer->hadjustment));

        /*
         * Trigger the 'changed' signal, update the rest of
         * the appliaction.
         */
        gtk_adjustment_changed(viewer->hadjustment);
        gtk_adjustment_changed(viewer->vadjustment);

    }

    viewer->priv->scale = scale;

    rstto_image_viewer_queued_repaint (viewer, TRUE);
}

gdouble
rstto_image_viewer_get_scale (RsttoImageViewer *viewer)
{
    return viewer->priv->scale;
}

static void
rstto_image_viewer_set_motion_state (RsttoImageViewer *viewer, RsttoImageViewerMotionState state)
{
    viewer->priv->motion.state = state;
}

static RsttoImageViewerMotionState
rstto_image_viewer_get_motion_state (RsttoImageViewer *viewer)
{
    return viewer->priv->motion.state;
}

/*
 * rstto_image_viewer_set_orientation:
 * @viewer:
 * @orientation:
 *
 * Set orientation for the image shown here.
 */
void
rstto_image_viewer_set_orientation (
        RsttoImageViewer *viewer, 
        RsttoImageViewerOrientation orientation)
{
    viewer->priv->orientation = orientation;
    rstto_image_viewer_queued_repaint (viewer, TRUE);
}

RsttoImageViewerOrientation
rstto_image_viewer_get_orientation (RsttoImageViewer *viewer)
{
    return viewer->priv->orientation;
}

void
rstto_image_viewer_set_menu (
    RsttoImageViewer *viewer,
    GtkMenu *menu)
{
    if (viewer->priv->menu)
    {
        gtk_menu_detach(viewer->priv->menu);
        gtk_widget_destroy(GTK_WIDGET(viewer->priv->menu));
    }

    viewer->priv->menu = menu;

    if (viewer->priv->menu)
    {
        gtk_menu_attach_to_widget(
                viewer->priv->menu,
                GTK_WIDGET(viewer),
                NULL);
    }

}


/************************/
/** CALLBACK FUNCTIONS **/
/************************/

static void
cb_rstto_image_viewer_value_changed(GtkAdjustment *adjustment, RsttoImageViewer *viewer)
{
    rstto_image_viewer_queued_repaint (viewer, TRUE);
}

static void
cb_rstto_image_viewer_read_file_ready (GObject *source_object, GAsyncResult *result, gpointer user_data)
{
    GFile *file = G_FILE (source_object);
    RsttoImageViewerTransaction *transaction = (RsttoImageViewerTransaction *)user_data;

    GFileInputStream *file_input_stream = g_file_read_finish (file, result, NULL);

    if (file_input_stream == NULL)
    {
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

    if (read_bytes == -1)
    {
        gdk_pixbuf_loader_close (transaction->loader, NULL);
        return;
    }

    if (read_bytes > 0)
    {
        if(gdk_pixbuf_loader_write (transaction->loader, (const guchar *)transaction->buffer, read_bytes, &error) == FALSE)
        {
            /* Clean up the input-stream */
            g_input_stream_close (G_INPUT_STREAM (source_object), NULL, NULL);
            g_object_unref(source_object);

            gdk_pixbuf_loader_close (transaction->loader, NULL);
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
        gdk_pixbuf_loader_close (transaction->loader, NULL);

        /* Clean up the input-stream */
        g_input_stream_close (G_INPUT_STREAM (source_object), NULL, NULL);
        g_object_unref(source_object);
    }
}


static void
cb_rstto_image_loader_area_prepared (GdkPixbufLoader *loader, RsttoImageViewerTransaction *transaction)
{
    gint timeout = 0;
    RsttoImageViewer *viewer = transaction->viewer;

    if (viewer->priv->transaction == transaction)
    {
        if (viewer->priv->iter)
        {
            g_object_unref (viewer->priv->iter);
            viewer->priv->iter = NULL;
        }

        if (viewer->priv->pixbuf)
        {
            g_object_unref (viewer->priv->pixbuf);
            viewer->priv->pixbuf = NULL;
        }

        if (viewer->priv->animation)
        {
            g_object_unref (viewer->priv->animation);
            viewer->priv->animation = NULL;
        }

        viewer->priv->animation = gdk_pixbuf_loader_get_animation (loader);
        viewer->priv->iter = gdk_pixbuf_animation_get_iter (viewer->priv->animation, NULL);

        g_object_ref (viewer->priv->animation);

        timeout = gdk_pixbuf_animation_iter_get_delay_time (viewer->priv->iter);

        if (timeout > 0)
        {
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
}

static void
cb_rstto_image_loader_size_prepared (GdkPixbufLoader *loader, gint width, gint height, RsttoImageViewerTransaction *transaction)
{
    gint s_width = gdk_screen_get_width (default_screen);
    gint s_height = gdk_screen_get_height (default_screen);

    transaction->image_width = width;
    transaction->image_height = height;

    /*
     * Set the maximum size of the loaded image to the screen-size.
     * TODO: Add some 'smart-stuff' here
     */
    if (s_width < width || s_height < height)
    {
        /*
         * The image is loaded at the screen_size, calculate how this fits best.
         *  scale = MIN(width / screen_width, height / screen_height)
         *
         */
        if(((gdouble)width / (gdouble)s_width) < ((gdouble)height / (gdouble)s_height))
        {
            transaction->image_scale = (gdouble)s_width / (gdouble)width;
            gdk_pixbuf_loader_set_size (loader, s_width, (gint)((gdouble)height/(gdouble)width*(gdouble)s_width)); 
        }
        else
        {
            transaction->image_scale = (gdouble)s_height / (gdouble)height;
            gdk_pixbuf_loader_set_size (loader, (gint)((gdouble)width/(gdouble)height*(gdouble)s_width), s_height); 
        }
    }
    else
    {
        /*
         * Image-size won't be limited to screen-size (since it's smaller)
         * Set the image_scale to 1.0 (100%)
         */
        transaction->image_scale = 1.0;
    }
}

static void
cb_rstto_image_loader_closed (GdkPixbufLoader *loader, RsttoImageViewerTransaction *transaction)
{
    RsttoImageViewer *viewer = transaction->viewer;

    if (viewer->priv->transaction == transaction)
    {
        
        viewer->priv->scale = transaction->scale;
        viewer->priv->image_scale = transaction->image_scale;
        viewer->priv->image_width = transaction->image_width;
        viewer->priv->image_height = transaction->image_height;

        viewer->priv->transaction = NULL;
        rstto_image_viewer_queued_repaint (viewer, TRUE);
    }

    rstto_image_viewer_transaction_free (transaction);
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
             * lets make a copy for myself just in case. Since it's a copy, we
             * own the only reference to it. There is no need to add another.
             */
            viewer->priv->pixbuf = gdk_pixbuf_copy (gdk_pixbuf_animation_iter_get_pixbuf (viewer->priv->iter));
        }

        timeout = gdk_pixbuf_animation_iter_get_delay_time (viewer->priv->iter);

        if (timeout > 0)
        {
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
    gdouble v_scale, h_scale;
    GtkAdjustment *hadjustment, *vadjustment;
    gint subpixbuf_x_offset = 0;
    gint subpixbuf_y_offset = 0;
    gint subpixbuf_width = 0;
    gint subpixbuf_height = 0;

    /*
     * relative_scale contains viewer->priv->scale / viewer->priv->image_scale
     * this value is used a lot in this function, storing it once reduces 
     * nr of floating-point divisions
     *
     * It is the scale converted from the original image-size to the actual size of the source pixbuf.
     */
    gdouble relative_scale;

    g_source_remove (viewer->priv->repaint.idle_id);
    viewer->priv->repaint.idle_id = -1;
    viewer->priv->repaint.refresh = FALSE;


    g_object_freeze_notify(G_OBJECT(viewer->hadjustment));
    g_object_freeze_notify(G_OBJECT(viewer->vadjustment));

    if (viewer->priv->pixbuf)
    {
        switch (viewer->priv->orientation)
        {
            case RSTTO_IMAGE_VIEWER_ORIENT_NONE:
            case RSTTO_IMAGE_VIEWER_ORIENT_180:
                hadjustment = viewer->hadjustment;
                vadjustment = viewer->vadjustment;

                v_scale = (gdouble)(GTK_WIDGET (viewer)->allocation.height) / (gdouble)viewer->priv->image_height;
                h_scale = (gdouble)(GTK_WIDGET (viewer)->allocation.width) / (gdouble)viewer->priv->image_width;
                break;

            case RSTTO_IMAGE_VIEWER_ORIENT_90:
            case RSTTO_IMAGE_VIEWER_ORIENT_270:
                hadjustment = viewer->vadjustment;
                vadjustment = viewer->hadjustment;

                v_scale = (gdouble)(GTK_WIDGET (viewer)->allocation.height) / (gdouble)viewer->priv->image_width;
                h_scale = (gdouble)(GTK_WIDGET (viewer)->allocation.width) / (gdouble)viewer->priv->image_height;
                break;
        }

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
            if (h_scale < v_scale)
            { 
                viewer->priv->scale = h_scale;
            }
            else
            {
                viewer->priv->scale = v_scale;
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
             * if auto_scale == TRUE, calculate the scale
             */
            if (viewer->priv->auto_scale == TRUE)
            {
                /**
                 * Check if the image should scale to the horizontal
                 * or vertical dimensions of the widget.
                 *
                 * This is done by calculating the ratio between the 
                 * widget-dimensions and image-dimensions, the smallest 
                 * one is used for scaling the image.
                 */
                if (h_scale > v_scale)
                { 
                    viewer->priv->scale = v_scale;
                }
                else
                {
                    viewer->priv->scale = h_scale;
                }
            }
        }

        relative_scale = viewer->priv->scale / viewer->priv->image_scale;

        /*
         *
         *
         */
        switch (viewer->priv->orientation)
        {
            case RSTTO_IMAGE_VIEWER_ORIENT_NONE:
                if ((gtk_adjustment_get_page_size (vadjustment) + 
                        gtk_adjustment_get_value(vadjustment)) > (viewer->priv->image_height*viewer->priv->scale))
                {
                    gtk_adjustment_set_value (vadjustment,
                            (height*relative_scale) -
                            gtk_adjustment_get_page_size (vadjustment));
                }
                if ((gtk_adjustment_get_page_size (hadjustment) + 
                        gtk_adjustment_get_value(hadjustment)) > (viewer->priv->image_width*viewer->priv->scale))
                {
                    gtk_adjustment_set_value (hadjustment,
                            (width*relative_scale) - 
                            gtk_adjustment_get_page_size (hadjustment));
                }
                gtk_adjustment_set_upper (hadjustment, (gdouble)width*(viewer->priv->scale/viewer->priv->image_scale));
                gtk_adjustment_set_upper (vadjustment, (gdouble)height*(viewer->priv->scale/viewer->priv->image_scale));

                subpixbuf_x_offset = (gint)(gtk_adjustment_get_value (hadjustment) / relative_scale);
                subpixbuf_y_offset = (gint)(gtk_adjustment_get_value (vadjustment) / relative_scale);
                subpixbuf_width = (gint)((gtk_adjustment_get_page_size (hadjustment) / relative_scale) < width)?
                               (gtk_adjustment_get_page_size (hadjustment) / relative_scale)+1:(width);
                subpixbuf_height = (gint)((gtk_adjustment_get_page_size (vadjustment) / relative_scale) < height)?
                               (gtk_adjustment_get_page_size (vadjustment) / relative_scale)+1:(height);
                break;
            case RSTTO_IMAGE_VIEWER_ORIENT_180:
                gtk_adjustment_set_upper (hadjustment, (gdouble)width*(viewer->priv->scale/viewer->priv->image_scale));
                gtk_adjustment_set_upper (vadjustment, (gdouble)height*(viewer->priv->scale/viewer->priv->image_scale));
                subpixbuf_x_offset = (gint)((gtk_adjustment_get_upper(hadjustment) - 
                                             gtk_adjustment_get_page_size(hadjustment) - 
                                             gtk_adjustment_get_value (hadjustment)) / relative_scale);
                subpixbuf_y_offset = (gint)((gtk_adjustment_get_upper(vadjustment) - 
                                             gtk_adjustment_get_page_size(vadjustment) - 
                                             gtk_adjustment_get_value (vadjustment)) / relative_scale);
                subpixbuf_width = (gint)((gtk_adjustment_get_page_size (hadjustment) / relative_scale) < width)?
                               (gtk_adjustment_get_page_size (hadjustment) / relative_scale)+1:(width);
                subpixbuf_height = (gint)((gtk_adjustment_get_page_size (vadjustment) / relative_scale) < height)?
                               (gtk_adjustment_get_page_size (vadjustment) / relative_scale)+1:(height);
                break;
            case RSTTO_IMAGE_VIEWER_ORIENT_270:
                gtk_adjustment_set_upper (hadjustment, (gdouble)width*(viewer->priv->scale/viewer->priv->image_scale));
                gtk_adjustment_set_upper (vadjustment, (gdouble)height*(viewer->priv->scale/viewer->priv->image_scale));

                if ((gtk_adjustment_get_page_size (hadjustment) + 
                        gtk_adjustment_get_value(hadjustment)) > (viewer->priv->image_width*viewer->priv->scale))
                {
                    gtk_adjustment_set_value (hadjustment,
                            (width*relative_scale) -
                            gtk_adjustment_get_page_size (vadjustment));
                }
                if ((gtk_adjustment_get_page_size (vadjustment) + 
                        gtk_adjustment_get_value(vadjustment)) > (viewer->priv->image_height*viewer->priv->scale))
                {
                    gtk_adjustment_set_value (vadjustment,
                            (height*relative_scale) - 
                            gtk_adjustment_get_page_size (vadjustment));
                }

                subpixbuf_x_offset = (gint)((gtk_adjustment_get_upper(hadjustment) - 
                                             (gtk_adjustment_get_page_size(hadjustment) + 
                                             gtk_adjustment_get_value (hadjustment))) / relative_scale);
                subpixbuf_y_offset = (gint)(gtk_adjustment_get_value (vadjustment) / relative_scale);
                subpixbuf_width = (gint)((gtk_adjustment_get_page_size (hadjustment) / relative_scale) < width)?
                               (gtk_adjustment_get_page_size (hadjustment) / relative_scale)+1:(width);
                subpixbuf_height = (gint)((gtk_adjustment_get_page_size (vadjustment) / relative_scale) < height)?
                               (gtk_adjustment_get_page_size (vadjustment) / relative_scale)+1:(height);
                break;
            case RSTTO_IMAGE_VIEWER_ORIENT_90:
                gtk_adjustment_set_upper (hadjustment, (gdouble)width*(viewer->priv->scale/viewer->priv->image_scale));
                gtk_adjustment_set_upper (vadjustment, (gdouble)height*(viewer->priv->scale/viewer->priv->image_scale));

                if ((gtk_adjustment_get_page_size (hadjustment) + 
                        gtk_adjustment_get_value(hadjustment)) > (viewer->priv->image_width*viewer->priv->scale))
                {
                    gtk_adjustment_set_value (hadjustment,
                            (width*relative_scale) -
                            gtk_adjustment_get_page_size (vadjustment));
                }
                if ((gtk_adjustment_get_page_size (vadjustment) + 
                        gtk_adjustment_get_value(vadjustment)) > (viewer->priv->image_height*viewer->priv->scale))
                {
                    gtk_adjustment_set_value (vadjustment,
                            (height*relative_scale) - 
                            gtk_adjustment_get_page_size (vadjustment));
                }

                subpixbuf_x_offset = (gint)(gtk_adjustment_get_value (hadjustment) / relative_scale);
                subpixbuf_y_offset = (gint)((gtk_adjustment_get_upper(vadjustment) - 
                                             (gtk_adjustment_get_page_size(vadjustment) + 
                                             gtk_adjustment_get_value (vadjustment))) / relative_scale);
                subpixbuf_width = (gint)((gtk_adjustment_get_page_size (hadjustment) / relative_scale) < width)?
                               (gtk_adjustment_get_page_size (hadjustment) / relative_scale)+1:(width);
                subpixbuf_height = (gint)((gtk_adjustment_get_page_size (vadjustment) / relative_scale) < height)?
                               (gtk_adjustment_get_page_size (vadjustment) / relative_scale)+1:(height);
                break;
        }

        if (subpixbuf_x_offset < 0)
        {
            subpixbuf_x_offset = 0;
        }
        if (subpixbuf_y_offset < 0)
        {
            subpixbuf_y_offset = 0;
        }

        if (gtk_adjustment_get_page_size (vadjustment) > 0)
        {

            /*
             * Converting a double to an integer will result in rounding-errors,
             * everything behind the point will be cut off, this results in an 
             * image that won't fit nicely in the window.
             *
             * This will look like bg-color borders around the image.
             * Adding '1' when the tmp_pixbuf is smaller then the full width or
             * height of the image solves this.
             */
            GdkPixbuf *tmp_pixbuf2;
            GdkPixbuf *tmp_pixbuf;

            if (viewer->priv->dst_pixbuf)
            {
                g_object_unref (viewer->priv->dst_pixbuf);
                viewer->priv->dst_pixbuf = NULL;
            }

            tmp_pixbuf = gdk_pixbuf_new_subpixbuf (viewer->priv->pixbuf,
                    subpixbuf_x_offset,
                    subpixbuf_y_offset,
                    subpixbuf_width,
                    subpixbuf_height);

            switch (viewer->priv->orientation)
            {
                case RSTTO_IMAGE_VIEWER_ORIENT_180:
                    tmp_pixbuf2 = gdk_pixbuf_rotate_simple (tmp_pixbuf, GDK_PIXBUF_ROTATE_UPSIDEDOWN);
                    g_object_unref (tmp_pixbuf);
                    tmp_pixbuf = tmp_pixbuf2;
                    break;
                case RSTTO_IMAGE_VIEWER_ORIENT_270:
                    tmp_pixbuf2 = gdk_pixbuf_rotate_simple (tmp_pixbuf, GDK_PIXBUF_ROTATE_COUNTERCLOCKWISE);
                    g_object_unref (tmp_pixbuf);
                    tmp_pixbuf = tmp_pixbuf2;
                    break;
                case RSTTO_IMAGE_VIEWER_ORIENT_90:
                    tmp_pixbuf2 = gdk_pixbuf_rotate_simple (tmp_pixbuf, GDK_PIXBUF_ROTATE_CLOCKWISE);
                    g_object_unref (tmp_pixbuf);
                    tmp_pixbuf = tmp_pixbuf2;
                    break;
            }

            viewer->priv->dst_pixbuf = gdk_pixbuf_scale_simple (tmp_pixbuf,
                                       (gint)(gdk_pixbuf_get_width(tmp_pixbuf) * relative_scale),
                                       (gint)(gdk_pixbuf_get_height(tmp_pixbuf) * relative_scale),
                                       GDK_INTERP_BILINEAR);

            g_object_unref (tmp_pixbuf);
        }

        /* 
         * Set adjustments
         */
        gtk_adjustment_set_upper (hadjustment, (gdouble)width*(viewer->priv->scale/viewer->priv->image_scale));
        gtk_adjustment_set_upper (vadjustment, (gdouble)height*(viewer->priv->scale/viewer->priv->image_scale));
    }
    else
    {
        if (viewer->priv->dst_pixbuf)
        {
            g_object_unref (viewer->priv->dst_pixbuf);
            viewer->priv->dst_pixbuf = NULL;
        }

        /* 
         * Set adjustments
         */
        gtk_adjustment_set_upper (viewer->hadjustment, 0);
        gtk_adjustment_set_upper (viewer->vadjustment, 0);
    }

    g_object_thaw_notify(G_OBJECT(viewer->hadjustment));
    g_object_thaw_notify(G_OBJECT(viewer->vadjustment));

    gtk_adjustment_changed(viewer->hadjustment);
    gtk_adjustment_changed(viewer->vadjustment);


    rstto_image_viewer_paint (GTK_WIDGET (viewer));

    return FALSE;
}

static gboolean
rstto_scroll_event (
        GtkWidget *widget,
        GdkEventScroll *event)
{
    RsttoImageViewer *viewer = RSTTO_IMAGE_VIEWER (widget);
    gdouble tmp_x, tmp_y;
    gdouble scale;
    gint width, height;
    gint pixbuf_width = 0;
    gint pixbuf_height = 0;
    gint pixbuf_x_offset = 0;
    gint pixbuf_y_offset = 0;
    gboolean auto_scale = FALSE;
    gboolean revert_zoom_direction = viewer->priv->revert_zoom_direction;


    if ( NULL != viewer->priv->dst_pixbuf )
    {
        pixbuf_width = gdk_pixbuf_get_width(viewer->priv->dst_pixbuf);
        pixbuf_height = gdk_pixbuf_get_height(viewer->priv->dst_pixbuf);
        pixbuf_x_offset = ((GTK_WIDGET(viewer)->allocation.width - pixbuf_width)/2);
        pixbuf_y_offset = ((GTK_WIDGET(viewer)->allocation.height - pixbuf_height)/2);
    }

    if (event->state & (GDK_CONTROL_MASK))
    {
        if ( NULL != viewer->priv->file )
        {
            viewer->priv->auto_scale = FALSE;

            tmp_x = (gdouble)(gtk_adjustment_get_value(viewer->hadjustment) + (gdouble)event->x - pixbuf_x_offset) / viewer->priv->scale;
            tmp_y = (gdouble)(gtk_adjustment_get_value(viewer->vadjustment) + (gdouble)event->y - pixbuf_y_offset) / viewer->priv->scale;

            switch(event->direction)
            {
                case GDK_SCROLL_UP:
                case GDK_SCROLL_LEFT:
                    if (revert_zoom_direction)
                    {
                        scale = viewer->priv->scale / 1.1;
                    }
                    else
                    {
                        scale = viewer->priv->scale * 1.1;
                    }
                    break;
                case GDK_SCROLL_DOWN:
                case GDK_SCROLL_RIGHT:
                    if (revert_zoom_direction)
                    {
                        scale = viewer->priv->scale * 1.1;
                    }
                    else
                    {
                        scale = viewer->priv->scale / 1.1;
                    }
                    break;
            }

            /*
             * If the image is larger then the screen-dimensions,
             * there is no reason to zoom in beyond RSTTO_MAX_SCALE.
             *
             * If the image is smaller then the screen-dimensions,
             * zoom in to RSTTO_MAX_SCALE OR the window-size, whichever comes last.
             *
             */
            if (viewer->priv->image_scale < 1.0)
            {
                if (scale > RSTTO_MAX_SCALE)
                {
                    scale = RSTTO_MAX_SCALE;
                }
            }
            else
            {
                /*
                 * Assuming image_scale == 1.0
                 */

                gdouble v_scale = (gdouble)(GTK_WIDGET (viewer)->allocation.height) / (gdouble)viewer->priv->image_height;
                gdouble h_scale = (gdouble)(GTK_WIDGET (viewer)->allocation.width) / (gdouble)viewer->priv->image_width;

                if ((h_scale > RSTTO_MAX_SCALE) || (v_scale > RSTTO_MAX_SCALE))
                {
                    if(h_scale < v_scale)
                    {
                        /* If the image is scaled beyond the window-size, 
                         * force the scale to fit the window and set auto_scale = TRUE.
                         */
                        if (scale > h_scale)
                        {
                            auto_scale = TRUE;
                            scale = h_scale;
                        }
                    }
                    else
                    {
                        /* If the image is scaled beyond the window-size, 
                         * force the scale to fit the window and set auto_scale = TRUE.
                         */
                        if (scale > v_scale)
                        {
                            auto_scale = TRUE;
                            scale = v_scale;
                        }
                    }
                }
                else
                {
                    if (scale > RSTTO_MAX_SCALE)
                    {
                        scale = RSTTO_MAX_SCALE;
                    }

                }
            }

            /*
             * There is no need to zoom out beyond 32x32 pixels
             * unless, ofcourse the image itself is smaller then 32x32 pixels.
             */
            if (viewer->priv->image_width > viewer->priv->image_height)
            {
                if ((viewer->priv->image_width >= 32) && ((scale * viewer->priv->image_width) < 32))
                {
                    scale = (32.0 / (gdouble)viewer->priv->image_width);
                }
                if ((viewer->priv->image_width < 32) && (scale < 1.0))
                {
                    scale = 1.0; 
                }
            }
            else
            {
                if ((viewer->priv->image_height >= 32) && ((scale * viewer->priv->image_height) < 32))
                {
                    scale = (32.0 / (gdouble)viewer->priv->image_height);
                }
                if ((viewer->priv->image_height < 32) && (scale < 1.0))
                {
                    scale = 1.0; 
                }
            }

            viewer->priv->auto_scale = auto_scale;
            viewer->priv->scale = scale;

            g_object_freeze_notify(G_OBJECT(viewer->hadjustment));
            g_object_freeze_notify(G_OBJECT(viewer->vadjustment));

            if ( NULL != viewer->priv->pixbuf )
            {
                width = gdk_pixbuf_get_width (viewer->priv->pixbuf);
                height = gdk_pixbuf_get_height (viewer->priv->pixbuf);
            }

            gtk_adjustment_set_upper (viewer->hadjustment, (gdouble)width*(viewer->priv->scale/viewer->priv->image_scale));
            gtk_adjustment_set_upper (viewer->vadjustment, (gdouble)height*(viewer->priv->scale/viewer->priv->image_scale));


            gtk_adjustment_set_value (viewer->hadjustment, (tmp_x * scale - event->x));
            gtk_adjustment_set_value (viewer->vadjustment, (tmp_y * scale - event->y));

            g_object_thaw_notify(G_OBJECT(viewer->vadjustment));
            g_object_thaw_notify(G_OBJECT(viewer->hadjustment));

            gtk_adjustment_changed(viewer->hadjustment);
            gtk_adjustment_changed(viewer->vadjustment);

            rstto_image_viewer_queued_repaint (viewer, TRUE);
        }
        return TRUE;
    }
    return FALSE;
}

static gboolean 
rstto_motion_notify_event (
        GtkWidget *widget,
        GdkEventMotion *event)
{
    RsttoImageViewer *viewer = RSTTO_IMAGE_VIEWER (widget);

    if (event->state & GDK_BUTTON1_MASK)
    {
        viewer->priv->motion.current_x = event->x;
        viewer->priv->motion.current_y = event->y;

        switch (viewer->priv->motion.state)
        {
            case RSTTO_IMAGE_VIEWER_MOTION_STATE_MOVE:
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
            case RSTTO_IMAGE_VIEWER_MOTION_STATE_BOX_ZOOM:
                rstto_image_viewer_queued_repaint (viewer, FALSE);
                break;
            default:
                break;
        }
    }
    return FALSE;
}

static gboolean
rstto_button_press_event (
        GtkWidget *widget,
        GdkEventButton *event)
{
    RsttoImageViewer *viewer = RSTTO_IMAGE_VIEWER (widget);

    if(event->button == 1)
    {
        viewer->priv->motion.x = event->x;
        viewer->priv->motion.y = event->y;
        viewer->priv->motion.current_x = event->x;
        viewer->priv->motion.current_y = event->y;
        viewer->priv->motion.h_val = viewer->hadjustment->value;
        viewer->priv->motion.v_val = viewer->vadjustment->value;

        //if (viewer->priv->file != NULL && rstto_image_viewer_get_state (viewer) == RSTTO_IMAGE_VIEWER_STATE_NORMAL)
        if (viewer->priv->file != NULL )
        {
            if (!(event->state & (GDK_CONTROL_MASK)))
            {
                GtkWidget *widget = GTK_WIDGET(viewer);
                GdkCursor *cursor = gdk_cursor_new(GDK_FLEUR);
                gdk_window_set_cursor(widget->window, cursor);
                gdk_cursor_unref(cursor);
                rstto_image_viewer_set_motion_state (viewer, RSTTO_IMAGE_VIEWER_MOTION_STATE_MOVE);
            }

            if (event->state & GDK_CONTROL_MASK)
            {
                GtkWidget *widget = GTK_WIDGET(viewer);
                GdkCursor *cursor = gdk_cursor_new(GDK_UL_ANGLE);
                gdk_window_set_cursor(widget->window, cursor);
                gdk_cursor_unref(cursor);

                rstto_image_viewer_set_motion_state (viewer, RSTTO_IMAGE_VIEWER_MOTION_STATE_BOX_ZOOM);
            }
        }
        return TRUE;
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
        return TRUE;
    }
    return FALSE;
}

static gboolean
rstto_button_release_event (
        GtkWidget *widget,
        GdkEventButton *event)
{
    RsttoImageViewer *viewer = RSTTO_IMAGE_VIEWER (widget);
    gint box_x;
    gint box_y;
    gint box_width;
    gint box_height;
    gint pixbuf_width;
    gint pixbuf_height;
    gint pixbuf_x_offset;
    gint pixbuf_y_offset;
    gint width;
    gint height;

    if ( NULL != viewer->priv->dst_pixbuf )
    {
        pixbuf_width = gdk_pixbuf_get_width(viewer->priv->dst_pixbuf);
        pixbuf_height = gdk_pixbuf_get_height(viewer->priv->dst_pixbuf);
        pixbuf_x_offset = ((widget->allocation.width - pixbuf_width)/2);
        pixbuf_y_offset = ((widget->allocation.height - pixbuf_height)/2);

        width = gdk_pixbuf_get_width (viewer->priv->pixbuf);
        height = gdk_pixbuf_get_height (viewer->priv->pixbuf);

        if (viewer->priv->motion.y < viewer->priv->motion.current_y)
        {
            box_y = viewer->priv->motion.y;
            box_height = viewer->priv->motion.current_y - box_y;
        }
        else
        {
            box_y = viewer->priv->motion.current_y;
            box_height = viewer->priv->motion.y - box_y;
        }

        if (viewer->priv->motion.x < viewer->priv->motion.current_x)
        {
            box_x = viewer->priv->motion.x;
            box_width = viewer->priv->motion.current_x - box_x;
        }
        else
        {
            box_x = viewer->priv->motion.current_x;
            box_width = viewer->priv->motion.x - box_x;
        }
    }


    switch (event->button)
    {
        case 1:
            gdk_window_set_cursor(widget->window, NULL);
            switch (viewer->priv->motion.state)
            {
                case RSTTO_IMAGE_VIEWER_MOTION_STATE_BOX_ZOOM:
                    /*
                     * Constrain the selection-box to the left
                     * and top sides of the image.
                     */
                    if (box_x < pixbuf_x_offset)
                    {
                        box_width -= (pixbuf_x_offset - box_x);
                        box_x = pixbuf_x_offset;
                    }
                    if (box_y < pixbuf_y_offset)
                    {
                        box_height -= (pixbuf_y_offset - box_y);
                        box_y = pixbuf_y_offset;
                    }

                    /*
                     * Constrain the selection-box to the right
                     * and bottom sides of the image.
                     */
                    if ((box_x + box_width) > (pixbuf_x_offset+pixbuf_width))
                    {
                        box_width = (pixbuf_x_offset+pixbuf_width)-box_x;
                    }
                    if ((box_y + box_height) > (pixbuf_y_offset+pixbuf_height))
                    {
                        box_height = (pixbuf_y_offset+pixbuf_height)-box_y;
                    }

                    if ((box_width > 0) && (box_height > 0))
                    {
                        /*
                         * Using BOX_ZOOM disables auto-scale (zoom-to-fit-widget)
                         */
                        viewer->priv->auto_scale = FALSE;
                        
                        /*
                         * Calculate the center of the selection-box.
                         */

                        gdouble tmp_y = (gtk_adjustment_get_value(viewer->vadjustment) + (gdouble)box_y + ((gdouble)box_height/ 2) - pixbuf_y_offset) / viewer->priv->scale;

                        gdouble tmp_x = (gtk_adjustment_get_value(viewer->hadjustment) + (gdouble)box_x + ((gdouble)box_width / 2) - pixbuf_x_offset) / viewer->priv->scale;

                        /*
                         * Calculate the new scale
                         */
                        gdouble scale;
                        if ((gtk_adjustment_get_page_size(viewer->hadjustment) / box_width) < 
                            (gtk_adjustment_get_page_size(viewer->vadjustment) / box_height))
                        {
                            scale = viewer->priv->scale * (gtk_adjustment_get_page_size(viewer->hadjustment) / box_width);
                        }
                        else
                        {
                            scale = viewer->priv->scale * (gtk_adjustment_get_page_size(viewer->vadjustment) / box_height);
                        }

                        /*
                         * Prevent the widget from zooming in beyond the MAX_SCALE.
                         */
                        if (scale > RSTTO_MAX_SCALE)
                        {
                            scale = RSTTO_MAX_SCALE;
                        }

                        viewer->priv->scale = scale;

                        
                        /*
                         * Prevent the adjustments from emitting the 'changed' signal,
                         * this way both the upper-limit and value can be changed before the
                         * rest of the application is informed.
                         */
                        g_object_freeze_notify(G_OBJECT(viewer->hadjustment));
                        g_object_freeze_notify(G_OBJECT(viewer->vadjustment));

                        gtk_adjustment_set_upper (viewer->hadjustment, (gdouble)width*(viewer->priv->scale/viewer->priv->image_scale));
                        gtk_adjustment_set_value (viewer->hadjustment, (tmp_x * scale - ((gdouble)gtk_adjustment_get_page_size(viewer->hadjustment)/2)));
                        gtk_adjustment_set_upper (viewer->vadjustment, (gdouble)height*(viewer->priv->scale/viewer->priv->image_scale));
                        gtk_adjustment_set_value (viewer->vadjustment, (tmp_y * scale - ((gdouble)gtk_adjustment_get_page_size(viewer->vadjustment)/2)));

                        /*
                         * Enable signals on the adjustments.
                         */
                        g_object_thaw_notify(G_OBJECT(viewer->vadjustment));
                        g_object_thaw_notify(G_OBJECT(viewer->hadjustment));

                        /*
                         * Trigger the 'changed' signal, update the rest of
                         * the appliaction.
                         */
                        gtk_adjustment_changed(viewer->hadjustment);
                        gtk_adjustment_changed(viewer->vadjustment);

                    }
                    break;
                default:
                    break;
            }
            rstto_image_viewer_set_motion_state (viewer, RSTTO_IMAGE_VIEWER_MOTION_STATE_NORMAL);
            rstto_image_viewer_queued_repaint (viewer, FALSE);
            break;
    }
    return FALSE;
}

static void
cb_rstto_bgcolor_changed (
        GObject *settings,
        GParamSpec *pspec,
        gpointer user_data)
{
    RsttoImageViewer *viewer = RSTTO_IMAGE_VIEWER (user_data);
    rstto_image_viewer_queued_repaint (viewer, TRUE);
}

static void
cb_rstto_zoom_direction_changed (
        GObject *settings,
        GParamSpec *pspec,
        gpointer user_data)
{
    RsttoImageViewer *viewer = RSTTO_IMAGE_VIEWER (user_data);
    viewer->priv->revert_zoom_direction = rstto_settings_get_boolean_property (RSTTO_SETTINGS (settings), "revert-zoom-direction"); 
}
 
static gboolean
rstto_popup_menu (
        GtkWidget *widget)
{
    RsttoImageViewer *viewer = RSTTO_IMAGE_VIEWER (widget);

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
        return TRUE;
    }
    return FALSE;
}
