/*
 *  Copyright (c) Stephan Arts 2006-2012 <stephan@xfce.org>
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version 2
 *  of the License, or (at your option) any later version.
 * 
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 * 
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 *  02110-1301, USA.
 */

#include <config.h>
#include <gtk/gtk.h>
#include <string.h>
#include <math.h>

#include "monitor_chooser.h"

#define RSTTO_MAX_MONITORS 9

typedef struct {
    gint width;
    gint height;

    cairo_surface_t *image_surface;
} Monitor;

typedef struct {
    guint x;
    guint y;
    guint width;
    guint height;
} MonitorPosition;

struct _RsttoMonitorChooserPriv
{
    Monitor **monitors;
    gint n_monitors;
    gint selected;

    MonitorPosition monitor_positions[RSTTO_MAX_MONITORS];
};

static GtkWidgetClass *parent_class = NULL;

static void
rstto_monitor_chooser_init(RsttoMonitorChooser *);

static void
rstto_monitor_chooser_class_init(RsttoMonitorChooserClass *);

static void
rstto_monitor_chooser_finalize(GObject *object);

static void
rstto_monitor_chooser_realize(GtkWidget *widget);
static void
rstto_monitor_chooser_size_request(GtkWidget *, GtkRequisition *);
static void
rstto_monitor_chooser_size_allocate(GtkWidget *, GtkAllocation *);
static gboolean 
rstto_monitor_chooser_expose(GtkWidget *, GdkEventExpose *);
static gboolean
rstto_monitor_chooser_paint(GtkWidget *widget);

static void
cb_rstto_button_press_event (GtkWidget *, GdkEventButton *event);

static void
paint_monitor ( GtkWidget *widget,
                cairo_t *cr,
                gdouble x,
                gdouble y,
                gdouble width,
                gdouble height,
                gchar *label,
                Monitor *monitor,
                gboolean active);

enum
{
    RSTTO_MONITOR_CHOOSER_SIGNAL_CHANGED = 0,
    RSTTO_MONITOR_CHOOSER_SIGNAL_COUNT
};

static gint
rstto_monitor_chooser_signals[RSTTO_MONITOR_CHOOSER_SIGNAL_COUNT];

GType
rstto_monitor_chooser_get_type (void)
{
    static GType rstto_monitor_chooser_type = 0;

    if (!rstto_monitor_chooser_type)
    {
        static const GTypeInfo rstto_monitor_chooser_info = 
        {
            sizeof (RsttoMonitorChooserClass),
            (GBaseInitFunc) NULL,
            (GBaseFinalizeFunc) NULL,
            (GClassInitFunc) rstto_monitor_chooser_class_init,
            (GClassFinalizeFunc) NULL,
            NULL,
            sizeof (RsttoMonitorChooser),
            0,
            (GInstanceInitFunc) rstto_monitor_chooser_init,
            NULL
        };

        rstto_monitor_chooser_type = g_type_register_static (GTK_TYPE_WIDGET, "RsttoMonitorChooser", &rstto_monitor_chooser_info, 0);
    }
    return rstto_monitor_chooser_type;
}

static void
rstto_monitor_chooser_init(RsttoMonitorChooser *chooser)
{
    chooser->priv = g_new0(RsttoMonitorChooserPriv, 1);
    chooser->priv->selected = -1;
    chooser->priv->monitors = g_new0 (Monitor *, 1);

    gtk_widget_set_double_buffered (GTK_WIDGET(chooser), TRUE);

    g_signal_connect(G_OBJECT(chooser), "button-press-event", G_CALLBACK(cb_rstto_button_press_event), NULL);

    gtk_widget_set_redraw_on_allocate(GTK_WIDGET(chooser), TRUE);
    gtk_widget_set_events (GTK_WIDGET(chooser),
                           GDK_POINTER_MOTION_MASK);
}

static void
rstto_monitor_chooser_class_init(RsttoMonitorChooserClass *chooser_class)
{
    GtkWidgetClass *widget_class;
    GObjectClass *object_class;

    widget_class = (GtkWidgetClass*)chooser_class;
    object_class = (GObjectClass*)chooser_class;

    parent_class = g_type_class_peek_parent(chooser_class);

    widget_class->expose_event = rstto_monitor_chooser_expose;
    widget_class->realize = rstto_monitor_chooser_realize;
    widget_class->size_request = rstto_monitor_chooser_size_request;
    widget_class->size_allocate = rstto_monitor_chooser_size_allocate;

    object_class->finalize = rstto_monitor_chooser_finalize;

    rstto_monitor_chooser_signals[RSTTO_MONITOR_CHOOSER_SIGNAL_CHANGED] = g_signal_new("changed",
            G_TYPE_FROM_CLASS(chooser_class),
            G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
            0,
            NULL,
            NULL,
            g_cclosure_marshal_VOID__VOID,
            G_TYPE_NONE,
            0,
            NULL);
}

static void
rstto_monitor_chooser_finalize(GObject *object)
{
}

/**
 * rstto_monitor_chooser_realize:
 * @widget:
 *
 */
static void
rstto_monitor_chooser_realize(GtkWidget *widget)
{
    GdkWindowAttr attributes;
    gint attributes_mask;
    GtkAllocation allocation;
    GdkWindow *window;
    GtkStyle *style;

    g_return_if_fail (widget != NULL);
    g_return_if_fail (RSTTO_IS_MONITOR_CHOOSER (widget));

    GTK_WIDGET_SET_FLAGS (widget, GTK_REALIZED);

    gtk_widget_get_allocation (widget, &allocation);

    attributes.x = allocation.x;
    attributes.y = allocation.y;
    attributes.width = allocation.width;
    attributes.height = allocation.height;
    attributes.wclass = GDK_INPUT_OUTPUT;
    attributes.window_type = GDK_WINDOW_CHILD;
    attributes.event_mask = gtk_widget_get_events (widget) | GDK_EXPOSURE_MASK | GDK_BUTTON_PRESS_MASK;
    attributes.visual = gtk_widget_get_visual (widget);
    attributes.colormap = gtk_widget_get_colormap (widget);

    attributes_mask = GDK_WA_X | GDK_WA_Y | GDK_WA_VISUAL | GDK_WA_COLORMAP;
    window = gdk_window_new (gtk_widget_get_parent_window (widget), &attributes, attributes_mask);
    gtk_widget_set_window (widget, window);

    style = gtk_style_attach (gtk_widget_get_style (widget), window);
    gtk_widget_set_style (widget, style);
    gdk_window_set_user_data (window, widget);

    gtk_style_set_background (gtk_widget_get_style (widget), window, GTK_STATE_ACTIVE);
}


static void
rstto_monitor_chooser_size_request(GtkWidget *widget, GtkRequisition *requisition)
{
    requisition->height = 200;
    requisition->width = 400;
}

static void
rstto_monitor_chooser_size_allocate(GtkWidget *widget, GtkAllocation *allocation)
{
    gtk_widget_set_allocation (widget, allocation);
    if (GTK_WIDGET_REALIZED (widget))
    {
        gdk_window_move_resize (gtk_widget_get_window (widget),
            allocation->x,
            allocation->y,
            allocation->width,
            allocation->height);
    }
}

static gboolean
rstto_monitor_chooser_expose(GtkWidget *widget, GdkEventExpose *event)
{
    rstto_monitor_chooser_paint (widget);
    return FALSE;
}

static gboolean
rstto_monitor_chooser_paint(GtkWidget *widget)
{
    RsttoMonitorChooser *chooser = RSTTO_MONITOR_CHOOSER (widget);
    cairo_t *ctx = gdk_cairo_create (gtk_widget_get_window (widget));
    Monitor *monitor;
    GtkAllocation allocation;

    gdouble width, height;
    gchar *label = NULL;
    gint row_width = 0;
    gint id = 0;

    gtk_widget_get_allocation (widget, &allocation);

    gdk_cairo_set_source_color (
            ctx,
            &(gtk_widget_get_style (widget)->bg[GTK_STATE_NORMAL]));
    cairo_rectangle (
            ctx,
            0.0,
            0.0,
            (gdouble)allocation.width,
            (gdouble)allocation.height);
    cairo_fill (ctx);

    if (chooser->priv->n_monitors > 1)
    {
        for (; chooser->priv->monitors[id]; ++id)
        {
            monitor = chooser->priv->monitors[id];

            /* Render the selected monitor a little bigger */
            if (id == chooser->priv->selected)
            {
                if (monitor->width > monitor->height)
                {
                    width = allocation.width*0.4;
                    height = width;
                } 
                else
                {
                    height = allocation.width*0.4;
                    width = height;
                }
                label = g_strdup_printf("%d", id+1);
                cairo_save (ctx);
                paint_monitor (
                        widget,
                        ctx,
                        ((gdouble)allocation.width/4) - (width/2.0),
                        ((gdouble)allocation.height - height)/2.0,
                        width,
                        height,
                        label,
                        monitor,
                        TRUE);
                cairo_restore (ctx);
                g_free (label);
            }
            else
            {
                row_width = sqrt (chooser->priv->n_monitors);

                if (monitor->width > monitor->height)
                {
                    width = allocation.width*(0.4/((gdouble)row_width+1));
                    height = width;
                } 
                else
                {
                    height = allocation.width*(0.4/chooser->priv->n_monitors);
                    width = height;
                }
            

                label = g_strdup_printf("%d", id+1);
                cairo_save (ctx);
                if (id < chooser->priv->selected)
                {
                    paint_monitor (
                            widget,
                            ctx,
                            ((gdouble)allocation.width/2)+
                                (((gdouble)allocation.width/2)/
                                (row_width+1))*(id%(row_width)+1)-
                                (width/2.0),
                            ((gdouble)allocation.height/
                                (row_width+2)*(id/row_width+1))-
                                (height/2.0),
                            width,
                            height,
                            label,
                            monitor,
                            FALSE);
                }
                else
                {
                    paint_monitor (
                            widget,
                            ctx,
                            ((gdouble)allocation.width/2)+
                                (((gdouble)allocation.width/2)/
                                (row_width+1))*((id-1)%(row_width)+1)-
                                (width/2.0),
                            ((gdouble)allocation.height/
                                (row_width+2)*((id-1)/row_width+1))-
                                (height/2.0),
                            width,
                            height,
                            label,
                            monitor,
                            FALSE);
                }
                cairo_restore (ctx);
                g_free (label);
            }
        }
    }
    else
    {
        if (chooser->priv->monitors[0])
        {
            monitor = chooser->priv->monitors[0];
            if (monitor->width > monitor->height)
            {
                width = 200;
                height = 200;
            } 
            else
            {
                height = 200;
                width = 200;
            }
            cairo_save (ctx);
            paint_monitor (
                    widget,
                    ctx,
                    ((gdouble)allocation.width - width)/2.0,
                    ((gdouble)allocation.height - height)/2.0,
                    width,
                    height,
                    "1",
                    monitor,
                    TRUE);
            cairo_restore (ctx);
        }
    }

    cairo_destroy (ctx);

    return FALSE;
}

static void
paint_monitor ( GtkWidget *widget,
                cairo_t *cr,
                gdouble x,
                gdouble y,
                gdouble width,
                gdouble height,
                gchar *label,
                Monitor *monitor,
                gboolean active)
{
    /* Do we want the border_padding to be a percentage of the width
     * parmeter?
     */
    gdouble border_padding = 10.0;
    gdouble foot_height = height / 10.0;
    gdouble monitor_border_width = height * 0.04;

    /* Assumption: monitor-width is always larger then monitor-height */
    /******************************************************************/
    gdouble monitor_height = 
            ( width - ( 2 * ( border_padding + monitor_border_width ) ) ) /
            monitor->width * monitor->height;
    gdouble monitor_width = 
            width - ( 2 * ( border_padding + monitor_border_width ) );

    gdouble monitor_x = x + border_padding + monitor_border_width;

    gdouble monitor_y = y +
            ( height -
                    ( ( border_padding + monitor_border_width ) * 2 ) -
                    foot_height - monitor_height);

    gdouble line_width = 2.0;
    double radius = monitor_border_width * 0.5;
    double degrees = M_PI / 180.0;
    gint text_width = 0.0;
    gint text_height = 0.0;
    gdouble hscale = 1.0;
    gdouble vscale = 1.0;

    /*******************************************/
    PangoLayout *layout;
    PangoFontDescription *font_description;

    g_return_if_fail (NULL != monitor);
    
    /*
     * Set path for monitor outline and background-color.
     */
    cairo_new_sub_path (cr);
    cairo_arc (
            cr,
            monitor_x + monitor_width + monitor_border_width - radius,
            monitor_y - monitor_border_width + radius,
            radius,
            -90 * degrees,
            0);
    cairo_arc (
            cr,
            monitor_x + monitor_width + monitor_border_width - radius,
            monitor_y + monitor_height + monitor_border_width - radius,
            radius,
            0,
            90 * degrees);
    cairo_arc (
            cr,
            monitor_x - monitor_border_width + radius,
            monitor_y + monitor_height + monitor_border_width - radius,
            radius,
            90 * degrees,
            180 * degrees);
    cairo_arc (
            cr,
            monitor_x - monitor_border_width + radius,
            monitor_y - monitor_border_width + radius,
            radius,
            180 * degrees,
            270 * degrees);
    cairo_close_path (cr);

    /* Fill the background-color */
    gdk_cairo_set_source_color (
            cr,
            &(gtk_widget_get_style (widget)->base[GTK_STATE_NORMAL]));
    cairo_fill_preserve (cr);

    /* Paint the outside border */
    gdk_cairo_set_source_color (
            cr,
            &(gtk_widget_get_style (widget)->fg[GTK_STATE_NORMAL]));
    cairo_set_line_width (cr, line_width);
    cairo_stroke (cr);

    /* Draw a monitor foot */ 
    cairo_new_sub_path (cr);
    cairo_move_to (
            cr,
            monitor_x+(monitor_width-foot_height)/2.0,
            monitor_y+(monitor_height+monitor_border_width));
    cairo_line_to (
            cr,
            monitor_x+(monitor_width-foot_height)/2.0+foot_height,
            monitor_y+(monitor_height+monitor_border_width));
    cairo_line_to (
            cr,
            monitor_x+(monitor_width-foot_height)/2.0+foot_height,
            monitor_y+(monitor_height+monitor_border_width+foot_height*0.5));
    cairo_line_to (
            cr,
            monitor_x+(monitor_width-foot_height)/2.0+foot_height*2,
            monitor_y+(monitor_height+monitor_border_width+foot_height*0.5));
    cairo_line_to (
            cr,
            monitor_x+(monitor_width-foot_height)/2.0+foot_height*2,
            monitor_y+(monitor_height+monitor_border_width+foot_height));
    cairo_line_to (
            cr,
            monitor_x+(monitor_width-foot_height)/2.0-foot_height,
            monitor_y+(monitor_height+monitor_border_width+foot_height));
    cairo_line_to (
            cr,
            monitor_x+(monitor_width-foot_height)/2.0-foot_height,
            monitor_y+(monitor_height+monitor_border_width+foot_height*0.5));
    cairo_line_to (
            cr,
            monitor_x+(monitor_width-foot_height)/2.0,
            monitor_y+(monitor_height+monitor_border_width+foot_height*0.5));
    cairo_close_path (cr);
    gdk_cairo_set_source_color (
            cr,
            &(gtk_widget_get_style (widget)->base[GTK_STATE_NORMAL]));
    cairo_fill_preserve (cr);
    gdk_cairo_set_source_color (
            cr,
            &(gtk_widget_get_style (widget)->fg[GTK_STATE_NORMAL]));
    cairo_set_line_width (cr, line_width);
    cairo_stroke (cr);

    /* Draw a line around the image */
    cairo_new_sub_path (cr);
    cairo_move_to (cr, monitor_x, monitor_y);
    cairo_line_to (cr, monitor_x+monitor_width, monitor_y);
    cairo_line_to (cr, monitor_x+monitor_width, monitor_y + monitor_height);
    cairo_line_to (cr, monitor_x, monitor_y + monitor_height);
    cairo_close_path (cr);
    gdk_cairo_set_source_color (
            cr,
            &(gtk_widget_get_style (widget)->fg[GTK_STATE_NORMAL]));
    cairo_set_line_width (cr, line_width);
    cairo_stroke (cr);

    /* Set the path that limits the image-size */
    cairo_new_sub_path (cr);
    cairo_move_to (cr, monitor_x, monitor_y);
    cairo_line_to (cr, monitor_x+monitor_width, monitor_y);
    cairo_line_to (cr, monitor_x+monitor_width, monitor_y + monitor_height);
    cairo_line_to (cr, monitor_x, monitor_y + monitor_height);
    cairo_close_path (cr);

    /* Color the background black */
    cairo_set_source_rgba (cr, 0.0, 0.0, 0.0, 1.0);
    cairo_fill_preserve (cr);

    if (monitor->image_surface)
    {
        cairo_clip_preserve (cr);

        hscale = monitor_width / (cairo_image_surface_get_width(monitor->image_surface));
        vscale = monitor_height / (cairo_image_surface_get_height(monitor->image_surface));

        cairo_scale (cr, hscale, vscale);

        cairo_set_source_surface (
                cr,
                monitor->image_surface,
                monitor_x/hscale,
                monitor_y/vscale);
        cairo_paint(cr);

        cairo_reset_clip(cr);
        cairo_scale (cr, 1/hscale, 1/vscale);
    }

    if (FALSE == active)
    {
        cairo_set_source_rgba (cr, 1.0, 1.0, 1.0, 0.6);
        cairo_fill_preserve (cr);
    }

    cairo_set_source_rgba (cr, 0.5, 0.5, 0.5, 1.0);
    cairo_set_line_width (cr, line_width);
    cairo_stroke (cr);


    font_description = pango_font_description_new ();
    pango_font_description_set_family (font_description, "sans");
    pango_font_description_set_weight (font_description, PANGO_WEIGHT_BOLD);
    pango_font_description_set_absolute_size (font_description, height*0.3 * PANGO_SCALE);


    layout = pango_cairo_create_layout (cr);
    pango_layout_set_font_description (layout, font_description);
    pango_layout_set_text (layout, label, -1);
    pango_layout_set_alignment (layout, PANGO_ALIGN_CENTER);
    pango_cairo_update_layout (cr, layout);
    pango_layout_get_pixel_size (layout, &text_width, &text_height); 

    cairo_move_to (
            cr,
            monitor_x+(monitor_width-(gdouble)text_width) / 2,
            monitor_y+(monitor_height- (gdouble)text_height) / 2);
    pango_cairo_layout_path (cr, layout);
    cairo_set_source_rgba (cr, 1.0, 1.0, 1.0, 0.8);
    cairo_fill_preserve (cr);
    cairo_set_line_width (cr, line_width);
    cairo_set_source_rgba (cr, 0.0, 0.0, 0.0, 0.9);
    cairo_stroke (cr);

    g_object_unref (layout);
    pango_font_description_free (font_description);
}

/**
 * rstto_monitor_chooser_new:
 *
 * @Returns: a new monitor-chooser object.
 */
GtkWidget *
rstto_monitor_chooser_new ( void )
{
    RsttoMonitorChooser *chooser;

    chooser = g_object_new(RSTTO_TYPE_MONITOR_CHOOSER, NULL);

    return GTK_WIDGET(chooser);
}

/**
 * rstto_monitor_chooser_add:
 * @chooser: MonitorChooser
 * @width:   monitor-width (pixels)
 * @height:  monitor-height (pixels)
 *
 * Add a monitor to the monitor-chooser.
 */
gint
rstto_monitor_chooser_add ( 
        RsttoMonitorChooser *chooser,
        gint width,
        gint height)
{
    Monitor **monitors = g_new0 (Monitor *, chooser->priv->n_monitors+2);
    gint id = 0;

    Monitor *monitor = g_new0 (Monitor, 1);
    monitor->width = width;
    monitor->height = height;

    if (NULL == chooser->priv->monitors)
    {
        chooser->priv->selected = 0;
    }
    else
    {
        chooser->priv->selected = 0;

        for (id = 0; chooser->priv->monitors[id]; ++id)
        {
            monitors[id] = chooser->priv->monitors[id];
        }
        g_free (chooser->priv->monitors);
    }
    
    monitors[id] = monitor;

    chooser->priv->monitors = monitors;
    chooser->priv->n_monitors++;

    return id;
}

/**
 * rstto_monitor_chooser_set_image_surface:
 * @chooser:    Monitor chooser
 * @monitor_id: Monitor number
 * @surface:    Surface
 * @error:
 *
 * Set the image-surface for a specific monitor. (the image visible in
 * the monitor )
 */
gint
rstto_monitor_chooser_set_image_surface (
        RsttoMonitorChooser *chooser,
        gint monitor_id,
        cairo_surface_t *surface,
        GError **error)
{
    Monitor *monitor;
    gint retval = -1;

    g_return_val_if_fail (monitor_id < chooser->priv->n_monitors, retval);

    monitor = chooser->priv->monitors[monitor_id];

    if (monitor)
    {
        if (monitor->image_surface)
        {
            cairo_surface_destroy(monitor->image_surface);
        }

        monitor->image_surface = surface;

        retval = monitor_id;
    }
    if (GTK_WIDGET_REALIZED (GTK_WIDGET(chooser)))
    {
        rstto_monitor_chooser_paint (GTK_WIDGET(chooser));
    }

    return retval;
}

/**
 * cb_rstto_button_press_event:
 * @widget: Monitor-Chooser widget
 * @event:  Event
 *
 * Switch the monitor based on the location where a user clicks.
 */
static void
cb_rstto_button_press_event (
        GtkWidget *widget,
        GdkEventButton *event )
{
    RsttoMonitorChooser *chooser = RSTTO_MONITOR_CHOOSER(widget);
    gint row_width = 0;
    gint id = 0;
    gint width, height;
    GtkAllocation allocation;
    
    if (chooser->priv->n_monitors > 1)
    {
        row_width = sqrt (chooser->priv->n_monitors);

        gtk_widget_get_allocation (widget, &allocation);
        width = allocation.width*(0.4/((gdouble)row_width+1));
        height = width;

        for (id = 0; id < chooser->priv->n_monitors; ++id)
        {
            if ( (event->x > ((gdouble)allocation.width/2)+
                                (((gdouble)allocation.width/2)/
                                (row_width+1))*(id%(row_width)+1)-
                                (width/2.0)) &&
                 (event->x < ((gdouble)allocation.width/2)+
                                (((gdouble)allocation.width/2)/
                                (row_width+1))*(id%(row_width)+1)+
                                (width/2.0)) &&
                 (event->y > ((gdouble)allocation.height/
                                (row_width+2)*(id/row_width+1)-
                                (height/2.0))) &&
                 (event->y < ((gdouble)allocation.height/
                                (row_width+2)*(id/row_width+1)+
                                (height/2.0))))
            {
                if (id < chooser->priv->selected)
                {
                    chooser->priv->selected = id;
                }
                else
                {
                    if (id+1 != chooser->priv->n_monitors)
                    {
                        chooser->priv->selected = id+1;
                    }
                }

                g_signal_emit (G_OBJECT (chooser), rstto_monitor_chooser_signals[RSTTO_MONITOR_CHOOSER_SIGNAL_CHANGED], 0, NULL);

                rstto_monitor_chooser_paint (widget);
            }
        }

    }
}

/**
 * rstto_monitor_chooser_get_selected:
 * @chooser: The monitor-chooser widget
 *
 * Returns the id of the selected monitor.
 */
gint
rstto_monitor_chooser_get_selected (
        RsttoMonitorChooser *chooser )
{
    return chooser->priv->selected;
}

/**
 * rstto_monitor_chooser_get_dimensions:
 * @chooser: The monitor-chooser widget
 * @nr:      The monitor-number
 * @width:   A gint to store the width of the monitor (in pixels)
 * @height:  A gint to store the height of the monitor (in pixels)
 *
 * Returns the dimensions of a monitor identified by 'nr'.
 */
void
rstto_monitor_chooser_get_dimensions (
        RsttoMonitorChooser *chooser,
        gint nr,
        gint *width,
        gint *height)
{
    g_return_if_fail (nr < chooser->priv->n_monitors);

    *width = chooser->priv->monitors[nr]->width;
    *height = chooser->priv->monitors[nr]->height;
}
