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

#include <gio/gio.h>

#include <libxfce4ui/libxfce4ui.h>
#include <libexif/exif-data.h>

#include "util.h"
#include "file.h"
#include "image_list.h"
#include "thumbnail.h"
#include "thumbnail_bar.h"
#include "thumbnailer.h"

struct _RsttoThumbnailBarPriv
{
    GtkOrientation  orientation;
    gint dimension;
    gint offset;
    gboolean auto_center;
    gint begin;
    gint end;

    RsttoImageList     *image_list;
    RsttoImageListIter *iter;
    RsttoImageListIter *internal_iter;
    GList *thumbs;
    gint scroll_speed;
    struct
    {
        gdouble current_x;
        gdouble current_y;
        gint offset;
        gboolean motion;
    } motion;

    RsttoThumbnailer *thumbnailer;
};

static void
rstto_thumbnail_bar_init(RsttoThumbnailBar *);
static void
rstto_thumbnail_bar_class_init(RsttoThumbnailBarClass *);

static void
rstto_thumbnail_bar_size_request(GtkWidget *, GtkRequisition *);
static void
rstto_thumbnail_bar_size_allocate(GtkWidget *, GtkAllocation *);
static gboolean 
rstto_thumbnail_bar_expose(GtkWidget *, GdkEventExpose *);
static void
rstto_thumbnail_bar_realize(GtkWidget *widget);
static void
rstto_thumbnail_bar_unrealize(GtkWidget *widget);

static void
cb_rstto_thumbnail_bar_image_list_new_file (
        RsttoImageList *image_list,
        RsttoFile *file,
        gpointer user_data);
static void
cb_rstto_thumbnail_bar_image_list_remove_file (
        RsttoImageList *image_list,
        RsttoFile *file,
        gpointer user_data);
static void
cb_rstto_thumbnail_bar_image_list_remove_all (RsttoImageList *image_list, gpointer user_data);
void
cb_rstto_thumbnail_bar_image_list_iter_changed (RsttoImageListIter *iter, gpointer user_data);

static gboolean
cb_rstto_thumbnail_bar_thumbnail_button_press_event (GtkWidget *thumb, GdkEventButton *event);
static gboolean
cb_rstto_thumbnail_bar_thumbnail_button_release_event (GtkWidget *thumb, GdkEventButton *event);
static gboolean 
cb_rstto_thumbnail_bar_thumbnail_motion_notify_event (GtkWidget *thumb,
                                             GdkEventMotion *event,
                                             gpointer user_data);

static gboolean
cb_rstto_thumbnail_bar_scroll_event (RsttoThumbnailBar *bar,
                                     GdkEventScroll *event,
                                     gpointer *user_data);

static void
rstto_thumbnail_bar_add(GtkContainer *container, GtkWidget *child);
static void
rstto_thumbnail_bar_remove(GtkContainer *container, GtkWidget *child);
static void
rstto_thumbnail_bar_forall(GtkContainer *container, gboolean include_internals, GtkCallback callback, gpointer callback_data);
static GType
rstto_thumbnail_bar_child_type(GtkContainer *container);

static GtkWidgetClass *parent_class = NULL;

static void
cb_rstto_thumbnail_bar_thumbnail_clicked (GtkWidget *thumb, RsttoThumbnailBar *bar);

static gint
cb_rstto_thumbnail_bar_compare (GtkWidget *a, GtkWidget *b, gpointer);

GType
rstto_thumbnail_bar_get_type (void)
{
    static GType rstto_thumbnail_bar_type = 0;

    if (!rstto_thumbnail_bar_type)
    {
        static const GTypeInfo rstto_thumbnail_bar_info = 
        {
            sizeof (RsttoThumbnailBarClass),
            (GBaseInitFunc) NULL,
            (GBaseFinalizeFunc) NULL,
            (GClassInitFunc) rstto_thumbnail_bar_class_init,
            (GClassFinalizeFunc) NULL,
            NULL,
            sizeof (RsttoThumbnailBar),
            0,
            (GInstanceInitFunc) rstto_thumbnail_bar_init,
            NULL
        };

        rstto_thumbnail_bar_type = g_type_register_static (GTK_TYPE_CONTAINER, "RsttoThumbnailBar", &rstto_thumbnail_bar_info, 0);
    }
    return rstto_thumbnail_bar_type;
}


static void
rstto_thumbnail_bar_init(RsttoThumbnailBar *bar)
{
    bar->priv = g_new0(RsttoThumbnailBarPriv, 1);

    bar->priv->auto_center = TRUE;
    bar->priv->thumbnailer = rstto_thumbnailer_new();

	GTK_WIDGET_UNSET_FLAGS(bar, GTK_NO_WINDOW);
	gtk_widget_set_redraw_on_allocate(GTK_WIDGET(bar), TRUE);
    gtk_widget_set_events (GTK_WIDGET(bar),
                           GDK_SCROLL_MASK);

    bar->priv->orientation = GTK_ORIENTATION_VERTICAL;
    bar->priv->offset = 0;
    bar->priv->scroll_speed = 20;


    g_signal_connect(G_OBJECT(bar), "scroll_event", G_CALLBACK(cb_rstto_thumbnail_bar_scroll_event), NULL);

}

static void
rstto_thumbnail_bar_class_init(RsttoThumbnailBarClass *bar_class)
{
    GtkWidgetClass *widget_class;
    GtkContainerClass *container_class;

    widget_class = (GtkWidgetClass*)bar_class;
    container_class = (GtkContainerClass*)bar_class;

    parent_class = g_type_class_peek_parent(bar_class);

    widget_class->size_request = rstto_thumbnail_bar_size_request;
    widget_class->size_allocate = rstto_thumbnail_bar_size_allocate;
    widget_class->expose_event = rstto_thumbnail_bar_expose;
    widget_class->realize = rstto_thumbnail_bar_realize;
    widget_class->unrealize = rstto_thumbnail_bar_unrealize;

	container_class->add = rstto_thumbnail_bar_add;
	container_class->remove = rstto_thumbnail_bar_remove;
	container_class->forall = rstto_thumbnail_bar_forall;
	container_class->child_type = rstto_thumbnail_bar_child_type;

	gtk_widget_class_install_style_property (widget_class,
		g_param_spec_int ("spacing",
		_("Spacing"),
		_("The amount of space between the thumbnails"),
		0, G_MAXINT, 3,
		G_PARAM_READABLE));

	gtk_widget_class_install_style_property (widget_class,
		g_param_spec_int ("border-width",
		_("border width"),
		_("the border width of the thumbnail-bar"),
		0, G_MAXINT, 0,
		G_PARAM_READABLE));

}

static void
rstto_thumbnail_bar_size_request(GtkWidget *widget, GtkRequisition *requisition)
{
    RsttoThumbnailBar *bar = RSTTO_THUMBNAIL_BAR(widget);
    gint border_width;
    GList *iter;
	GtkRequisition child_requisition;

    gtk_widget_style_get (widget, "border-width", &border_width, NULL);

    requisition->height = 70;
    requisition->width = 70;

    for(iter = bar->priv->thumbs; iter; iter = g_list_next(iter))
    {
		gtk_widget_size_request(GTK_WIDGET(iter->data), &child_requisition);
		requisition->width = MAX(child_requisition.width, requisition->width);
		requisition->height = MAX(child_requisition.height, requisition->height);
    }

    switch (bar->priv->orientation)
    {
        case GTK_ORIENTATION_HORIZONTAL:
            requisition->height += (border_width * 2);
            requisition->width += (border_width * 2);
            break;
        case GTK_ORIENTATION_VERTICAL:
            requisition->height += (border_width * 2);
            requisition->width += (border_width * 2);
            break;
    }

	widget->requisition = *requisition;
    GTK_CONTAINER(bar)->border_width = border_width;
}

static void
rstto_thumbnail_bar_size_allocate(GtkWidget *widget, GtkAllocation *allocation)
{
    RsttoThumbnailBar *bar = RSTTO_THUMBNAIL_BAR(widget);
    gint border_width = GTK_CONTAINER(bar)->border_width;
    gint spacing = 0;
    GtkAllocation child_allocation;
    GtkRequisition child_requisition;
    GList *iter = bar->priv->thumbs;

	gtk_widget_style_get(widget, "spacing", &spacing, NULL);
    widget->allocation = *allocation;

    child_allocation.x = border_width;
    child_allocation.y = border_width;
    child_allocation.height = border_width * 2;
    child_allocation.width = border_width * 2;


    if (GTK_WIDGET_REALIZED(widget))
    {
        gdk_window_move_resize (widget->window,
                                allocation->x,
                                allocation->y,
                                allocation->width,
                                allocation->height);
    }


    switch(bar->priv->orientation)
    {
        case GTK_ORIENTATION_HORIZONTAL:
            if (bar->priv->auto_center == TRUE)
            {
                bar->priv->offset = 0 - (allocation->width / 2);
            }
            while(iter)
            {
                gtk_widget_get_child_requisition(GTK_WIDGET(iter->data), &child_requisition);
                allocation->height = MAX(child_requisition.height + (border_width * 2), allocation->height);

                if (bar->priv->auto_center == TRUE)
                {
                    if (g_list_position (bar->priv->thumbs, iter) < rstto_image_list_iter_get_position (bar->priv->iter))
                    {
                        bar->priv->offset += allocation->height + spacing;
                    }
                    if (g_list_position (bar->priv->thumbs, iter) == rstto_image_list_iter_get_position (bar->priv->iter))
                    {
                        bar->priv->offset +=  0.5 * allocation->height;
                    }
                }

                iter = g_list_next(iter);
            }

            child_allocation.x -= bar->priv->offset;
            
            iter = bar->priv->thumbs;

            while(iter)
            {
                gtk_widget_get_child_requisition(GTK_WIDGET(iter->data), &child_requisition);
                child_allocation.height = allocation->height - (border_width * 2);
                child_allocation.width = child_allocation.height;

                if ((child_allocation.x < (allocation->x + allocation->width)) &&
                    ((child_allocation.x + child_allocation.width) > allocation->x + border_width))
                {
                    gtk_widget_set_child_visible(GTK_WIDGET(iter->data), TRUE);
                    gtk_widget_size_allocate(GTK_WIDGET(iter->data), &child_allocation);

                    /* Do thumbnailing stuff */
                    rstto_thumbnailer_queue_thumbnail (bar->priv->thumbnailer, iter->data);
                }
                else
                {
                    gtk_widget_set_child_visible(GTK_WIDGET(iter->data), FALSE);

                    rstto_thumbnailer_dequeue_thumbnail (bar->priv->thumbnailer, iter->data);
                }

                child_allocation.x += child_allocation.width + spacing;
                iter = g_list_next(iter);
            }
            break;
        case GTK_ORIENTATION_VERTICAL:
            if (bar->priv->auto_center == TRUE)
            {
                bar->priv->offset = 0 - (allocation->height / 2);
            }
            while(iter)
            {
                gtk_widget_get_child_requisition(GTK_WIDGET(iter->data), &child_requisition);
                allocation->width = MAX(child_requisition.width + (border_width * 2), allocation->width);

                if (bar->priv->auto_center == TRUE)
                {
                    if (g_list_position (bar->priv->thumbs, iter) < rstto_image_list_iter_get_position (bar->priv->iter))
                    {
                        bar->priv->offset += allocation->width + spacing;
                    }
                    if (g_list_position (bar->priv->thumbs, iter) == rstto_image_list_iter_get_position (bar->priv->iter))
                    {
                        bar->priv->offset +=  0.5 * allocation->width;
                    }
                }

                iter = g_list_next(iter);
            }

            child_allocation.y -= bar->priv->offset;
            
            iter = bar->priv->thumbs;

            while(iter)
            {

                gtk_widget_get_child_requisition(GTK_WIDGET(iter->data), &child_requisition);
                child_allocation.width = allocation->width - (border_width * 2);
                child_allocation.height = child_allocation.width;

                if (child_allocation.y < (allocation->y + allocation->height))
                {
                    gtk_widget_set_child_visible(GTK_WIDGET(iter->data), TRUE);
                    gtk_widget_size_allocate(GTK_WIDGET(iter->data), &child_allocation);

                    /* Do thumbnailing stuff */
                    rstto_thumbnailer_queue_thumbnail (bar->priv->thumbnailer, iter->data);
                }
                else
                {
                    gtk_widget_set_child_visible(GTK_WIDGET(iter->data), FALSE);
                    rstto_thumbnailer_dequeue_thumbnail (bar->priv->thumbnailer, iter->data);
                }

                gtk_widget_size_allocate(GTK_WIDGET(iter->data), &child_allocation);
                child_allocation.y += child_allocation.height + spacing;
                iter = g_list_next(iter);
            }
            break;
    }
}

static gboolean 
rstto_thumbnail_bar_expose(GtkWidget *widget, GdkEventExpose *ex)
{
    RsttoThumbnailBar *bar = RSTTO_THUMBNAIL_BAR(widget);

    GList *iter = bar->priv->thumbs;

    GdkEventExpose *n_ex = g_new0(GdkEventExpose, 1);

    GdkGC *gc = gdk_gc_new(GDK_DRAWABLE(widget->window));
    gint border_width = GTK_CONTAINER(bar)->border_width;
    GdkColor color, dot_color, bar_color;
    color.red= 0;
    color.green= 0;
    color.blue= 0;
    dot_color.red = 0xffff;
    dot_color.green = 0xffff;
    dot_color.blue = 0xffff;
    bar_color.red = 0x4444;
    bar_color.green = 0x4444;
    bar_color.blue = 0x4444;

    n_ex->type = ex->type;
    n_ex->window = ex->window;
    n_ex->send_event = ex->send_event;
    n_ex->area.x = border_width;
    n_ex->area.y = border_width;
    n_ex->area.width = ex->area.width;
    n_ex->area.height = ex->area.height;
    n_ex->count = ex->count;

    gdk_colormap_alloc_color (gdk_gc_get_colormap (gc), &color, FALSE, TRUE);
    gdk_colormap_alloc_color (gdk_gc_get_colormap (gc), &dot_color, FALSE, TRUE);
    gdk_colormap_alloc_color (gdk_gc_get_colormap (gc), &bar_color, FALSE, TRUE);
    gdk_gc_set_rgb_fg_color (gc, &color);

    while(iter)
    {

        if (GTK_WIDGET_VISIBLE(iter->data) == TRUE)
        {
            switch (bar->priv->orientation)
            {
                case GTK_ORIENTATION_HORIZONTAL:
                    /* why are these widgets not filtered out with the GTK_WIDGET_VISIBLE macro?*/
                    if (GTK_WIDGET(iter->data)->allocation.x > GTK_WIDGET(bar)->allocation.width)
                        break;
                    if ((GTK_WIDGET(iter->data)->allocation.x + GTK_WIDGET(iter->data)->allocation.width) < 0)
                        break;

                    /* first (partially) visible thumbnail */
                    if (GTK_WIDGET(iter->data)->allocation.x < 0)
                    {
                        n_ex->area.x = 0;
                        n_ex->area.width = GTK_WIDGET(iter->data)->allocation.width;
                    }
                    else
                    {
                        /* last (partially) visible thumbnail */
                        if ((GTK_WIDGET(bar)->allocation.x + (GTK_WIDGET(bar)->allocation.width)) <
                            (GTK_WIDGET(iter->data)->allocation.x + GTK_WIDGET(iter->data)->allocation.width))
                        {
                            n_ex->area.x = GTK_WIDGET(iter->data)->allocation.x;
                            n_ex->area.width = GTK_WIDGET(bar)->allocation.x + GTK_WIDGET(bar)->allocation.width - n_ex->area.x;
                        }
                        else
                        {
                            /* everything in between */
                            n_ex->area.x = GTK_WIDGET(iter->data)->allocation.x;
                            n_ex->area.width = GTK_WIDGET(bar)->allocation.width - n_ex->area.x;
                        }

                    }
                    if (n_ex->region)
                        gdk_region_destroy(n_ex->region);
                    n_ex->region = gdk_region_rectangle(&(n_ex->area));
                    gtk_container_propagate_expose(GTK_CONTAINER(widget), GTK_WIDGET(iter->data), n_ex);
                    break;
                case GTK_ORIENTATION_VERTICAL:
                    /* why are these widgets not filtered out with the GTK_WIDGET_VISIBLE macro?*/
                    if (GTK_WIDGET(iter->data)->allocation.y > GTK_WIDGET(bar)->allocation.height)
                        break;
                    if ((GTK_WIDGET(iter->data)->allocation.y + GTK_WIDGET(iter->data)->allocation.height) < 0 )
                        break;

                    /* first (partially) visible thumbnail */
                    if (GTK_WIDGET(iter->data)->allocation.y < 0)
                    {
                        n_ex->area.y = 0;
                        n_ex->area.height = GTK_WIDGET(iter->data)->allocation.height;
                    }
                    else
                    {
                        /* last (partially) visible thumbnail */
                        if ((GTK_WIDGET(bar)->allocation.y + (GTK_WIDGET(bar)->allocation.height)) <
                            (GTK_WIDGET(iter->data)->allocation.y + GTK_WIDGET(iter->data)->allocation.height))
                        {
                            n_ex->area.y = GTK_WIDGET(iter->data)->allocation.y;
                            n_ex->area.height = GTK_WIDGET(bar)->allocation.y + GTK_WIDGET(bar)->allocation.height - n_ex->area.y;
                        }
                        else
                        {
                            /* everything in between */
                            n_ex->area.y = GTK_WIDGET(iter->data)->allocation.y;
                            n_ex->area.height = GTK_WIDGET(bar)->allocation.height - n_ex->area.y;
                        }

                    }
                    if (n_ex->region)
                        gdk_region_destroy(n_ex->region);
                    n_ex->region = gdk_region_rectangle(&(n_ex->area));
                    gtk_container_propagate_expose(GTK_CONTAINER(widget), GTK_WIDGET(iter->data), n_ex);
                    break;
            }
        }
        iter = g_list_next(iter);
    }

    return FALSE;
}

static void
rstto_thumbnail_bar_realize(GtkWidget *widget)
{
    RsttoThumbnailBar *bar = RSTTO_THUMBNAIL_BAR(widget);

    GdkWindowAttr attributes;
    gint attributes_mask;
    gint border_width = 0;

    GTK_WIDGET_SET_FLAGS (widget, GTK_REALIZED);

    attributes.x = widget->allocation.x + border_width;
    attributes.y = widget->allocation.y + border_width;
    attributes.width = widget->allocation.width - border_width * 2;
    attributes.height = widget->allocation.height - border_width * 2;
    attributes.window_type = GDK_WINDOW_CHILD;
    attributes.wclass = GDK_INPUT_OUTPUT;
    attributes.visual = gtk_widget_get_visual (widget);
    attributes.colormap = gtk_widget_get_colormap (widget);
    attributes.event_mask = gtk_widget_get_events (widget) | GDK_EXPOSURE_MASK;

    attributes_mask = GDK_WA_X | GDK_WA_Y | GDK_WA_VISUAL | GDK_WA_COLORMAP;

    widget->window = gdk_window_new (gtk_widget_get_parent_window (widget),
                   &attributes, attributes_mask);
    gdk_window_set_user_data (widget->window, bar);

    attributes.x = 0;
    attributes.y = 0;
    widget->style = gtk_style_attach (widget->style, widget->window);
    gtk_style_set_background (widget->style, widget->window, GTK_STATE_NORMAL);
}

static void
rstto_thumbnail_bar_unrealize(GtkWidget *widget)
{

    if (GTK_WIDGET_CLASS (parent_class)->unrealize)
        (* GTK_WIDGET_CLASS (parent_class)->unrealize) (widget);

}

GtkWidget *
rstto_thumbnail_bar_new (RsttoImageList *nav)
{
    RsttoThumbnailBar *bar;

    bar = g_object_new(RSTTO_TYPE_THUMBNAIL_BAR, NULL);

    rstto_thumbnail_bar_set_image_list (bar, nav);

    return (GtkWidget *)bar;
}

void
rstto_thumbnail_bar_set_image_list (RsttoThumbnailBar *bar, RsttoImageList *nav)
{
    if (bar->priv->image_list)
    {
        g_object_unref (bar->priv->image_list);
        bar->priv->image_list = NULL;
    }

    bar->priv->image_list = nav;

    if (bar->priv->image_list)
    {
        g_signal_connect (G_OBJECT (bar->priv->image_list), "new-image", G_CALLBACK (cb_rstto_thumbnail_bar_image_list_new_file), bar);
        g_signal_connect (G_OBJECT (bar->priv->image_list), "remove-image", G_CALLBACK (cb_rstto_thumbnail_bar_image_list_remove_file), bar);
        g_signal_connect (G_OBJECT (bar->priv->image_list), "remove-all", G_CALLBACK (cb_rstto_thumbnail_bar_image_list_remove_all), bar);
        g_object_ref (nav);
    }
}
/*
 * rstto_thumbnail_bar_set_orientation:
 *
 * @bar      : ThumbnailBar
 * @orientation :
 *
 */
void
rstto_thumbnail_bar_set_orientation (RsttoThumbnailBar *bar, GtkOrientation orientation)
{
    bar->priv->orientation = orientation;
}

/*
 * rstto_thumbnail_bar_get_orientation:
 *
 * @bar      : ThumbnailBar
 *
 * Return value : GtkOrientation
 *
 */
GtkOrientation
rstto_thumbnail_bar_get_orientation (RsttoThumbnailBar *bar)
{
    return bar->priv->orientation;
}

static void
rstto_thumbnail_bar_add(GtkContainer *container, GtkWidget *child)
{
    RsttoThumbnailBar *bar = RSTTO_THUMBNAIL_BAR(container);
	g_return_if_fail(GTK_IS_WIDGET(child));

	gtk_widget_set_parent(child, GTK_WIDGET(container));

    bar->priv->thumbs = g_list_insert_sorted_with_data (bar->priv->thumbs, child, (GCompareDataFunc)cb_rstto_thumbnail_bar_compare, bar);
}

static void
rstto_thumbnail_bar_remove(GtkContainer *container, GtkWidget *child)
{
    RsttoThumbnailBar *bar = RSTTO_THUMBNAIL_BAR(container);
	gboolean widget_was_visible;

	g_return_if_fail(GTK_IS_WIDGET(child));


	widget_was_visible = GTK_WIDGET_VISIBLE(child);

    rstto_thumbnailer_dequeue_thumbnail (bar->priv->thumbnailer, RSTTO_THUMBNAIL(child));

    bar->priv->thumbs = g_list_remove(bar->priv->thumbs, child);

	gtk_widget_unparent(child);

	/* remove from list is somewhere else */
	if(widget_was_visible)
		gtk_widget_queue_resize(GTK_WIDGET(container));
}

static void
rstto_thumbnail_bar_forall(GtkContainer *container, gboolean include_internals, GtkCallback callback, gpointer callback_data)
{
    RsttoThumbnailBar *bar = RSTTO_THUMBNAIL_BAR(container);

    g_return_if_fail(callback != NULL);

    g_list_foreach(bar->priv->thumbs, (GFunc)callback, callback_data);

}

static GType
rstto_thumbnail_bar_child_type(GtkContainer *container)
{
    return GTK_TYPE_WIDGET;
}


static gint
cb_rstto_thumbnail_bar_compare (GtkWidget *a, GtkWidget *b, gpointer user_data)
{
    RsttoThumbnailBar *bar = RSTTO_THUMBNAIL_BAR (user_data);
    RsttoFile *a_i = rstto_thumbnail_get_file (RSTTO_THUMBNAIL (a));
    RsttoFile *b_i = rstto_thumbnail_get_file (RSTTO_THUMBNAIL (b));

    return rstto_image_list_get_compare_func (bar->priv->image_list) (a_i, b_i);
}

static gboolean
cb_rstto_thumbnail_bar_thumbnail_button_press_event (GtkWidget *thumb, GdkEventButton *event)
{
    if(event->button == 1)
    {
        RsttoThumbnailBar *bar = RSTTO_THUMBNAIL_BAR(gtk_widget_get_parent(GTK_WIDGET(thumb)));

        bar->priv->motion.offset = bar->priv->offset;
        if (bar->priv->orientation == GTK_ORIENTATION_HORIZONTAL)
        {
            gdouble x = event->x + GTK_WIDGET(thumb)->allocation.x;
            bar->priv->motion.current_x = x;
        }
        else
        {
            gdouble y = event->y + GTK_WIDGET(thumb)->allocation.y;
            bar->priv->motion.current_y = y;
        }
    }

    return FALSE;
}

static gboolean
cb_rstto_thumbnail_bar_thumbnail_button_release_event (GtkWidget *thumb, GdkEventButton *event)
{
    RsttoThumbnailBar *bar = RSTTO_THUMBNAIL_BAR(gtk_widget_get_parent(GTK_WIDGET(thumb)));
    if(event->button == 1)
    {
        GtkWidget *widget = GTK_WIDGET(thumb);
        gdk_window_set_cursor(widget->window, NULL);
        if (bar->priv->motion.motion == TRUE)
        {
            bar->priv->motion.motion = FALSE;
            return TRUE;
        }
    }
    return FALSE;
}

static gboolean 
cb_rstto_thumbnail_bar_thumbnail_motion_notify_event (GtkWidget *thumb,
                                                      GdkEventMotion *event,
                                                      gpointer user_data)
{
    RsttoThumbnailBar *bar = RSTTO_THUMBNAIL_BAR(gtk_widget_get_parent(GTK_WIDGET(thumb)));
    gdouble x = event->x + GTK_WIDGET(thumb)->allocation.x;
    gdouble y = event->y + GTK_WIDGET(thumb)->allocation.y;
    gint thumb_size = GTK_WIDGET(bar->priv->thumbs->data)->allocation.width;
    gint border_width = 0;
    gint spacing;
    gint size = 0;

	gtk_widget_style_get(GTK_WIDGET (bar), "spacing", &spacing, NULL);
    size = thumb_size * g_list_length (bar->priv->thumbs) + spacing * (g_list_length (bar->priv->thumbs) - 1);

    if (event->state & GDK_BUTTON1_MASK)
    {
        GtkWidget *widget = GTK_WIDGET(thumb);
        GdkCursor *cursor = gdk_cursor_new(GDK_FLEUR);
        gdk_window_set_cursor(widget->window, cursor);
        gdk_cursor_unref(cursor);

        bar->priv->motion.motion = TRUE;
        bar->priv->auto_center = FALSE;
        if (bar->priv->orientation == GTK_ORIENTATION_HORIZONTAL)
        {
            bar->priv->offset = bar->priv->motion.offset + (bar->priv->motion.current_x - x);
            if ((thumb_size - GTK_WIDGET(bar)->allocation.width) >= bar->priv->offset)
                bar->priv->offset = thumb_size - GTK_WIDGET(bar)->allocation.width + border_width;
            if ((size - thumb_size) <= bar->priv->offset)
                bar->priv->offset = size - thumb_size;
        }
        else
        {
            bar->priv->offset = bar->priv->motion.offset + (bar->priv->motion.current_y - y);
            if ((thumb_size - GTK_WIDGET(bar)->allocation.height) >= bar->priv->offset)
                bar->priv->offset = thumb_size - GTK_WIDGET(bar)->allocation.height + border_width;
            if ((size - thumb_size) <= bar->priv->offset)
                bar->priv->offset = size - thumb_size;
        }


        bar->priv->motion.offset = bar->priv->offset;
        bar->priv->motion.current_x = x;
        bar->priv->motion.current_y = y;
        gtk_widget_queue_resize(GTK_WIDGET(bar));
    }
    return FALSE;
}

static gboolean
cb_rstto_thumbnail_bar_scroll_event (RsttoThumbnailBar *bar,
                                     GdkEventScroll *event,
                                     gpointer *user_data)
{
    gint thumb_size;
    GList *thumb;
    gint border_width = GTK_CONTAINER(bar)->border_width;
    gint spacing = 0;
    GtkWidget *widget = GTK_WIDGET (bar);

	gtk_widget_style_get(widget, "spacing", &spacing, NULL);

    switch(event->direction)
    {
        case GDK_SCROLL_UP:
        case GDK_SCROLL_LEFT:
            if (bar->priv->thumbs)
            {   
                bar->priv->auto_center = FALSE;
                bar->priv->offset -= bar->priv->scroll_speed;
                switch(bar->priv->orientation)
                {
                    case GTK_ORIENTATION_HORIZONTAL:
                        thumb_size = GTK_WIDGET(bar->priv->thumbs->data)->allocation.width;
                        if ((thumb_size - GTK_WIDGET(bar)->allocation.width) >= bar->priv->offset)
                            bar->priv->offset = thumb_size - GTK_WIDGET(bar)->allocation.width + border_width;
                        break;
                    case GTK_ORIENTATION_VERTICAL:
                        thumb_size = GTK_WIDGET(bar->priv->thumbs->data)->allocation.height;
                        if ((thumb_size - GTK_WIDGET(bar)->allocation.height) >= bar->priv->offset)
                            bar->priv->offset = thumb_size - GTK_WIDGET(bar)->allocation.height + border_width;
                        break;
                }
            }
            break;
        case GDK_SCROLL_DOWN:
        case GDK_SCROLL_RIGHT:
            if (bar->priv->thumbs)
            {   
                gint size = 0;
                bar->priv->auto_center = FALSE;
                bar->priv->offset +=  bar->priv->scroll_speed;
                switch(bar->priv->orientation)
                {
                    case GTK_ORIENTATION_HORIZONTAL:
                        thumb_size = GTK_WIDGET(bar->priv->thumbs->data)->allocation.width;
                        for (thumb = bar->priv->thumbs; thumb != NULL; thumb = g_list_next(thumb))
                        {
                            size += thumb_size * g_list_length (bar->priv->thumbs);
                            if (g_list_next (thumb))
                                size += spacing;
                        }
                        if ((size - thumb_size) <= bar->priv->offset)
                            bar->priv->offset = size - thumb_size;
                        break;
                    case GTK_ORIENTATION_VERTICAL:
                        thumb_size = GTK_WIDGET(bar->priv->thumbs->data)->allocation.height;
                        for (thumb = bar->priv->thumbs; thumb != NULL; thumb = g_list_next(thumb))
                        {
                            size += GTK_WIDGET(thumb->data)->allocation.height;
                            if (g_list_next (thumb))
                                size += spacing;
                        }
                        if ((size - thumb_size) <= bar->priv->offset)
                            bar->priv->offset = size - thumb_size;
                        break;
                }
            }
            break;
    }
    gtk_widget_queue_resize(GTK_WIDGET(bar));
    return FALSE;

}

void
rstto_thumbnail_bar_set_iter (RsttoThumbnailBar *bar, RsttoImageListIter *iter)
{
    if (bar->priv->iter)
    {
        g_signal_handlers_disconnect_by_func (bar->priv->iter, cb_rstto_thumbnail_bar_image_list_iter_changed, bar);

        g_object_unref (bar->priv->iter);
        g_object_unref (bar->priv->internal_iter);
        bar->priv->internal_iter = NULL;
    }

    bar->priv->iter = iter;

    if (bar->priv->iter)
    {
        g_object_ref (bar->priv->iter);
        bar->priv->internal_iter = rstto_image_list_iter_clone (bar->priv->iter);
        g_signal_connect (bar->priv->iter, "changed", G_CALLBACK (cb_rstto_thumbnail_bar_image_list_iter_changed), bar);
    }
}

void
cb_rstto_thumbnail_bar_image_list_iter_changed (RsttoImageListIter *iter, gpointer user_data)
{
    RsttoThumbnailBar *bar = RSTTO_THUMBNAIL_BAR (user_data);

    bar->priv->thumbs = g_list_sort_with_data (bar->priv->thumbs, (GCompareDataFunc)cb_rstto_thumbnail_bar_compare, bar);
    bar->priv->auto_center = TRUE;

    gtk_widget_queue_resize(GTK_WIDGET(bar));
    /* useless, but keepsthe compiler silent */
    bar->priv->begin=0;
}

static void
cb_rstto_thumbnail_bar_image_list_new_file (
        RsttoImageList *image_list,
        RsttoFile *file,
        gpointer user_data)
{
    RsttoThumbnailBar *bar = RSTTO_THUMBNAIL_BAR (user_data);
    GtkWidget *thumb;
    GList *iter;

    g_return_if_fail (rstto_image_list_iter_find_file (bar->priv->internal_iter, file));

    for (iter = bar->priv->thumbs; iter != NULL; iter = g_list_next (iter))
    {
        if (rstto_file_equal(file,rstto_thumbnail_get_file (iter->data)))
            return;
    }

    thumb = rstto_thumbnail_new (file);

    gtk_container_add (GTK_CONTAINER (bar), thumb);
    gtk_widget_show_all (thumb);

    g_signal_connect (thumb, "clicked", G_CALLBACK (cb_rstto_thumbnail_bar_thumbnail_clicked), bar);
    g_signal_connect (thumb, "button_press_event", G_CALLBACK (cb_rstto_thumbnail_bar_thumbnail_button_press_event), bar);
    g_signal_connect (thumb, "button_release_event", G_CALLBACK (cb_rstto_thumbnail_bar_thumbnail_button_release_event), bar);
    g_signal_connect (thumb, "motion_notify_event", G_CALLBACK (cb_rstto_thumbnail_bar_thumbnail_motion_notify_event), bar);
}

static void
cb_rstto_thumbnail_bar_image_list_remove_file (
        RsttoImageList *image_list,
        RsttoFile *file,
        gpointer user_data )
{
    RsttoThumbnailBar *bar = RSTTO_THUMBNAIL_BAR (user_data);
    GList *iter = bar->priv->thumbs;

    while (iter)
    {
        if (rstto_file_equal(rstto_thumbnail_get_file(iter->data), file))
        {
            GtkWidget *widget = iter->data;
            gtk_container_remove (GTK_CONTAINER (bar), widget);
            break;
        }
        iter = g_list_next (iter);
    }
}

static void
cb_rstto_thumbnail_bar_image_list_remove_all (RsttoImageList *image_list, gpointer user_data)
{
    RsttoThumbnailBar *bar = RSTTO_THUMBNAIL_BAR (user_data);
    if (bar->priv->thumbs)
    {
        g_list_foreach (bar->priv->thumbs, (GFunc)(gtk_widget_destroy), NULL);
        g_list_free (bar->priv->thumbs);
        bar->priv->thumbs = NULL;
    }
}



static void
cb_rstto_thumbnail_bar_thumbnail_clicked (GtkWidget *thumb, RsttoThumbnailBar *bar)
{
    RsttoFile *file;

    g_return_if_fail (bar->priv->iter);

    file = rstto_thumbnail_get_file (RSTTO_THUMBNAIL(thumb));
    rstto_image_list_iter_find_file (bar->priv->iter, file);
}
