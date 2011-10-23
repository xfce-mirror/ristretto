/*
 *  Copyright (C) Stephan Arts 2011 <stephan@xfce.org>
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
#include <math.h>

#include "monitor_chooser.h"

typedef struct {
    gint width;
    gint height;
    GdkPixbuf *pixbuf;
    RsttoMonitorStyle style;
} Monitor;

struct _RsttoMonitorChooserPriv
{
    GSList *monitors;
    gint selected;
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
paint_monitor ( cairo_t *cr,
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

    g_return_if_fail (widget != NULL);
    g_return_if_fail (RSTTO_IS_MONITOR_CHOOSER (widget));

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


static void
rstto_monitor_chooser_size_request(GtkWidget *widget, GtkRequisition *requisition)
{
    requisition->height = 200;
    requisition->width = 400;
}

static void
rstto_monitor_chooser_size_allocate(GtkWidget *widget, GtkAllocation *allocation)
{
    widget->allocation = *allocation;
    if (GTK_WIDGET_REALIZED (widget))
    {
        gdk_window_move_resize (widget->window,
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
    GSList *iter = chooser->priv->monitors;
    cairo_t *ctx = gdk_cairo_create (widget->window);
    Monitor *monitor;
    gdouble width, height;
    gchar *label = NULL;
    gint row_width = 0;
    gint id = 0;

    gdk_cairo_set_source_color (
            ctx,
            &(widget->style->bg[GTK_STATE_NORMAL]));
    cairo_rectangle (
            ctx,
            0.0,
            0.0,
            (gdouble)widget->allocation.width,
            (gdouble)widget->allocation.height);
    cairo_fill (ctx);

    if (g_slist_length (chooser->priv->monitors) > 1)
    {
        while (NULL != iter)
        {
            monitor = iter->data;
            /* Render the selected monitor a little bigger */
            if (chooser->priv->selected == g_slist_index (chooser->priv->monitors, monitor))
            {
                if (monitor->width > monitor->height)
                {
                    width = widget->allocation.width*0.4;
                    height = (gdouble)width / (gdouble)monitor->width * (gdouble)monitor->height;
                } 
                else
                {
                    height = widget->allocation.width*0.4;
                    width = (gdouble)height / (gdouble)monitor->height * (gdouble)monitor->width;
                }
                label = g_strdup_printf("%d", g_slist_index (chooser->priv->monitors, monitor)+1);
                paint_monitor (ctx,
                        ((gdouble)widget->allocation.width/4) - (width/2.0),
                        ((gdouble)widget->allocation.height - height)/2.0,
                        width,
                        height,
                        label,
                        monitor,
                        TRUE);
                g_free (label);
            }
            else
            {
                row_width = sqrt (g_slist_length(chooser->priv->monitors));

                if (monitor->width > monitor->height)
                {
                    width = widget->allocation.width*(0.4/((gdouble)row_width+1));
                    height = (gdouble)width / (gdouble)monitor->width * (gdouble)monitor->height;
                } 
                else
                {
                    height = widget->allocation.width*(0.4/g_slist_length(chooser->priv->monitors));
                    width = (gdouble)height / (gdouble)monitor->height * (gdouble)monitor->width;
                }
            

                label = g_strdup_printf("%d", g_slist_index (chooser->priv->monitors, monitor)+1);
                paint_monitor (ctx,
                        ((gdouble)widget->allocation.width/2)+
                            (((gdouble)widget->allocation.width/2)/
                            (row_width+1))*(id%(row_width)+1)-
                            (width/2.0),
                        ((gdouble)widget->allocation.height/
                            (row_width+2)*(id/row_width+1))-
                            (height/2.0),
                        width,
                        height,
                        label,
                        monitor,
                        FALSE);
                g_free (label);

                id++;
            }
            iter = g_slist_next(iter);
        }
    }
    else
    {
        if (iter)
        {
            monitor = iter->data;
            if (monitor->width > monitor->height)
            {
                width = 200;
                height = (gdouble)width / (gdouble)monitor->width * (gdouble)monitor->height;
            } 
            else
            {
                height = 200;
                width = (gdouble)height / (gdouble)monitor->height * (gdouble)monitor->width;
            }
            paint_monitor (ctx,
                    ((gdouble)widget->allocation.width - width)/2.0,
                    ((gdouble)widget->allocation.height - height)/2.0,
                    width,
                    height,
                    "1",
                    monitor,
                    TRUE);
        }
    }
    return FALSE;
}

static void
paint_monitor ( cairo_t *cr,
                gdouble x,
                gdouble y,
                gdouble width,
                gdouble height,
                gchar *label,
                Monitor *monitor,
                gboolean active)
{
    gdouble line_width = 3.0;
    double corner_radius = height / 10;
    double radius = corner_radius / 1.0;
    double degrees = M_PI / 180.0;
    gint text_width = 0.0;
    gint text_height = 0.0;
    gdouble hscale = 1.0;
    gdouble vscale = 1.0;
    PangoLayout *layout;
    PangoFontDescription *font_description;
    GdkPixbuf *dst_pixbuf = NULL;

    g_return_if_fail (NULL != monitor);
    
    /*
     * Set path for monitor outline and background-color.
     */
    cairo_new_sub_path (cr);
    cairo_arc (cr, x + width - radius, y + radius, radius, -90 * degrees, 0 * degrees);
    cairo_arc (cr, x + width - radius, y + height - radius, radius, 0 * degrees, 90 * degrees);
    cairo_arc (cr, x + radius, y + height - radius, radius, 90 * degrees, 180 * degrees);
    cairo_arc (cr, x + radius, y + radius, radius, 180 * degrees, 270 * degrees);
    cairo_close_path (cr);

    /* Fill the background-color */
    cairo_set_source_rgb (cr, 0.9, 0.9, 0.9);
    cairo_fill_preserve (cr);

    /* Paint the outside border */
    cairo_set_source_rgba (cr, 0.2, 0.2, 0.2, 1.0);
    cairo_set_line_width (cr, line_width);
    cairo_stroke (cr);

    cairo_new_sub_path (cr);
    cairo_arc (cr, x + width - radius-line_width, y + radius+line_width, radius, -90 * degrees, 0 * degrees);
    cairo_arc (cr, x + width - radius-line_width, y + height - radius-line_width, radius, 0 * degrees, 90 * degrees);
    cairo_arc (cr, x + radius+line_width, y + height - radius-line_width, radius, 90 * degrees, 180 * degrees);
    cairo_arc (cr, x + radius+line_width, y + radius+line_width, radius, 180 * degrees, 270 * degrees);
    cairo_close_path (cr);

    /* Paint the inside border */
    cairo_set_source_rgba (cr, 1.0, 1.0, 1.0, 1.0);
    cairo_set_line_width (cr, line_width);
    cairo_stroke (cr);

    /* Set the path that limits the image-size */
    cairo_new_sub_path (cr);
    cairo_move_to (cr, x + height / 10.0, y + height / 10.0);
    cairo_line_to (cr, x + width - height / 10, y + height / 10.0);
    cairo_line_to (cr, x + width - height / 10, y + height - height / 10.0);
    cairo_line_to (cr, x + height / 10.0, y + height - height / 10.0);
    cairo_close_path (cr);

    /* Color the background black */
    cairo_set_source_rgba (cr, 0.0, 0.0, 0.0, 1.0);
    cairo_fill_preserve (cr);

    cairo_clip_preserve (cr);
    if (monitor->pixbuf)
    {
        switch (monitor->style)
        {
            case MONITOR_STYLE_STRETCHED:
                hscale = (width) / (gdk_pixbuf_get_width(monitor->pixbuf));
                vscale = (height) / (gdk_pixbuf_get_height(monitor->pixbuf));
                cairo_scale (cr, hscale, vscale);

                gdk_cairo_set_source_pixbuf (cr,
                                             monitor->pixbuf,
                                             x/hscale+((width/hscale)-gdk_pixbuf_get_width(monitor->pixbuf))/2,
                                             y/vscale+((height/vscale)-gdk_pixbuf_get_height(monitor->pixbuf))/2);
                break;
            case MONITOR_STYLE_SCALED:
                hscale = (width) / (gdk_pixbuf_get_width(monitor->pixbuf));
                vscale = (height) / (gdk_pixbuf_get_height(monitor->pixbuf));
                if (hscale < vscale)
                {
                    vscale = hscale;
                }
                else
                {
                    hscale = vscale;
                }
                cairo_scale (cr, hscale, vscale);

                gdk_cairo_set_source_pixbuf (cr,
                                             monitor->pixbuf,
                                             x/hscale+((width/hscale)-gdk_pixbuf_get_width(monitor->pixbuf))/2,
                                             y/vscale+((height/vscale)-gdk_pixbuf_get_height(monitor->pixbuf))/2);

                break;
            case MONITOR_STYLE_ZOOMED:
                hscale = (width) / (gdk_pixbuf_get_width(monitor->pixbuf));
                vscale = (height) / (gdk_pixbuf_get_height(monitor->pixbuf));
                if (hscale > vscale)
                {
                    vscale = hscale;
                }
                else
                {
                    hscale = vscale;
                }
                cairo_scale (cr, hscale, vscale);

                gdk_cairo_set_source_pixbuf (cr,
                                             monitor->pixbuf,
                                             x/hscale+((width/hscale)-gdk_pixbuf_get_width(monitor->pixbuf))/2,
                                             y/vscale+((height/vscale)-gdk_pixbuf_get_height(monitor->pixbuf))/2);
                break;
            default:
                dst_pixbuf = gdk_pixbuf_copy (monitor->pixbuf);
                gdk_pixbuf_saturate_and_pixelate (monitor->pixbuf, dst_pixbuf, 0.0, TRUE);
                hscale = width / (gdk_pixbuf_get_width(monitor->pixbuf));
                vscale = hscale;
                cairo_scale (cr, hscale, vscale);

                gdk_cairo_set_source_pixbuf (cr,
                                             dst_pixbuf,
                                             x/hscale+((width/hscale)-gdk_pixbuf_get_width(monitor->pixbuf))/2,
                                             y/vscale+((height/vscale)-gdk_pixbuf_get_height(monitor->pixbuf))/2);
                break;
        }
    }
    cairo_paint(cr);
    cairo_reset_clip(cr);
    cairo_scale (cr, 1/hscale, 1/vscale);
    if (NULL != dst_pixbuf)
    {
        g_object_unref (dst_pixbuf);
        dst_pixbuf = NULL;
    }

    if (FALSE == active)
    {
        cairo_set_source_rgba (cr, 1.0, 1.0, 1.0, 0.6);
        cairo_fill_preserve (cr);
    }

    cairo_set_source_rgba (cr, 0.5, 0.5, 0.5, 1.0);
    cairo_set_line_width (cr, 2.0);
    cairo_stroke (cr);


    font_description = pango_font_description_new ();
    pango_font_description_set_family (font_description, "sans");
    pango_font_description_set_weight (font_description, PANGO_WEIGHT_BOLD);
    pango_font_description_set_absolute_size (font_description, height*0.4 * PANGO_SCALE);


    layout = pango_cairo_create_layout (cr);
    pango_layout_set_font_description (layout, font_description);
    pango_layout_set_text (layout, label, -1);
    pango_layout_set_alignment (layout, PANGO_ALIGN_CENTER);
    pango_cairo_update_layout (cr, layout);
    pango_layout_get_pixel_size (layout, &text_width, &text_height); 

    cairo_set_source_rgba (cr, 1.0, 1.0, 1.0, 0.9);
    cairo_move_to (cr, x+(width-(gdouble)text_width) / 2, y+(height/2 + ((gdouble)text_height / 3)));
    pango_cairo_show_layout_line (cr, pango_layout_get_line (layout, 0));

    g_object_unref (layout);
    pango_font_description_free (font_description);
}

GtkWidget *
rstto_monitor_chooser_new ( void )
{
    RsttoMonitorChooser *chooser;

    chooser = g_object_new(RSTTO_TYPE_MONITOR_CHOOSER, NULL);

    return GTK_WIDGET(chooser);
}

gint
rstto_monitor_chooser_add ( 
        RsttoMonitorChooser *chooser,
        gint width,
        gint height)
{
    Monitor *monitor = g_new0 (Monitor, 1);
    monitor->width = width;
    monitor->height = height;
    if (NULL == chooser->priv->monitors)
    {
        chooser->priv->selected = 0;
    }
    

    chooser->priv->monitors = g_slist_append (chooser->priv->monitors, monitor);

    return g_slist_index (chooser->priv->monitors, monitor);
}

gint
rstto_monitor_chooser_set_pixbuf (
        RsttoMonitorChooser *chooser,
        gint monitor_id,
        GdkPixbuf *pixbuf,
        GError **error)
{
    Monitor *monitor = g_slist_nth_data (chooser->priv->monitors, monitor_id);
    gint retval = -1;

    if (monitor)
    {
        if (monitor->pixbuf)
        {
            g_object_unref (monitor->pixbuf);
        }
        monitor->pixbuf = pixbuf;
        retval = monitor_id;
    }
    if (GTK_WIDGET_REALIZED (GTK_WIDGET(chooser)))
    {
        rstto_monitor_chooser_paint (GTK_WIDGET(chooser));
    }

    return retval;
}

static void
cb_rstto_button_press_event (
        GtkWidget *widget,
        GdkEventButton *event )
{
    RsttoMonitorChooser *chooser = RSTTO_MONITOR_CHOOSER(widget);
    gint row_width = 0;
    gint id = 0;
    gint width, height;
    
    if (g_slist_length (chooser->priv->monitors) > 1)
    {
        row_width = sqrt (g_slist_length(chooser->priv->monitors));

        width = widget->allocation.width*(0.4/((gdouble)row_width+1));
        height = width;

        for (id = 0; id < (gint)g_slist_length(chooser->priv->monitors); ++id)
        {
            if ( (event->x > ((gdouble)widget->allocation.width/2)+
                                (((gdouble)widget->allocation.width/2)/
                                (row_width+1))*(id%(row_width)+1)-
                                (width/2.0)) &&
                 (event->x < ((gdouble)widget->allocation.width/2)+
                                (((gdouble)widget->allocation.width/2)/
                                (row_width+1))*(id%(row_width)+1)+
                                (width/2.0)) &&
                 (event->y > ((gdouble)widget->allocation.height/
                                (row_width+2)*(id/row_width+1)-
                                (height/2.0))) &&
                 (event->y < ((gdouble)widget->allocation.height/
                                (row_width+2)*(id/row_width+1)+
                                (height/2.0))))
            {
                if (id < chooser->priv->selected)
                {
                    chooser->priv->selected = id;
                }
                else
                {
                    chooser->priv->selected = id+1;
                }

                g_signal_emit (G_OBJECT (chooser), rstto_monitor_chooser_signals[RSTTO_MONITOR_CHOOSER_SIGNAL_CHANGED], 0, NULL);

                rstto_monitor_chooser_paint (widget);
            }
        }

    }
}

gint
rstto_monitor_chooser_get_selected (
        RsttoMonitorChooser *chooser )
{
    return chooser->priv->selected;
}

gboolean
rstto_monitor_chooser_set_style (
        RsttoMonitorChooser *chooser,
        gint monitor_id,
        RsttoMonitorStyle style )
{
    GtkWidget *widget = GTK_WIDGET(chooser);
    Monitor *monitor = g_slist_nth_data (chooser->priv->monitors, monitor_id);
    g_return_val_if_fail (monitor != NULL, FALSE);
    if ( NULL != monitor )
    {
        monitor->style = style;
        if (GTK_WIDGET_REALIZED (widget))
            rstto_monitor_chooser_paint (GTK_WIDGET(chooser));
        return TRUE;
    }
    return FALSE;
}
