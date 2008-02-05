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
#include "thumbnail.h"
#include "thumbnail_bar.h"

struct _RsttoThumbnailBarPriv
{
    GtkOrientation  orientation;
    RsttoNavigator *navigator;
    gint dimension;
    gint offset;
    gboolean auto_center;
    gint begin;
    gint end;
    GSList *thumbs;
    struct
    {
        gdouble current_x;
        gdouble current_y;
        gint offset;
        gboolean motion;
    } motion;
    GdkWindow *client_window;
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

static gboolean
cb_rstto_thumbnail_bar_thumbnail_button_press_event (RsttoThumbnail *thumb, GdkEventButton *event);
static gboolean
cb_rstto_thumbnail_bar_thumbnail_button_release_event (RsttoThumbnail *thumb, GdkEventButton *event);
static gboolean 
cb_rstto_thumbnail_bar_thumbnail_motion_notify_event (RsttoThumbnail *thumb,
                                             GdkEventMotion *event,
                                             gpointer user_data);
static gboolean
cb_rstto_thumbnail_bar_thumbnail_scroll_event (RsttoThumbnail *thumb,
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
cb_rstto_thumbnail_bar_nav_new_entry (RsttoNavigator *nav,
                                    gint nr,
                                    RsttoNavigatorEntry *entry,
                                    RsttoThumbnailBar *bar);
static void
cb_rstto_thumbnail_bar_nav_iter_changed (RsttoNavigator *nav,
                                       gint nr,
                                       RsttoNavigatorEntry *entry,
                                       RsttoThumbnailBar *bar);
static void
cb_rstto_thumbnail_bar_nav_reordered (RsttoNavigator *nav,
                                    RsttoThumbnailBar *bar);
static void
cb_rstto_thumbnail_bar_nav_entry_removed(RsttoNavigator *nav,
                                    RsttoNavigatorEntry *entry,
                                    RsttoThumbnailBar *bar);

static void
cb_rstto_thumbnail_bar_thumbnail_toggled (RsttoThumbnail *thumb, RsttoThumbnailBar *bar);

static gint
cb_rstto_thumbnail_bar_compare (RsttoThumbnail *a, RsttoThumbnail *b);

GType
rstto_thumbnail_bar_get_type ()
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

	GTK_WIDGET_UNSET_FLAGS(bar, GTK_NO_WINDOW);
	gtk_widget_set_redraw_on_allocate(GTK_WIDGET(bar), FALSE);

    bar->priv->orientation = GTK_ORIENTATION_HORIZONTAL;
    bar->priv->offset = 0;

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
}

static void
rstto_thumbnail_bar_size_request(GtkWidget *widget, GtkRequisition *requisition)
{
    RsttoThumbnailBar *bar = RSTTO_THUMBNAIL_BAR(widget);
    gint border_width = GTK_CONTAINER(bar)->border_width;
    GSList *iter;

	GtkRequisition child_requisition;

    requisition->height = 70;
    requisition->width = 70;

    for(iter = bar->priv->thumbs; iter; iter = g_slist_next(iter))
    {
		gtk_widget_size_request(GTK_WIDGET(iter->data), &child_requisition);
		requisition->width = MAX(child_requisition.width, requisition->width);
		requisition->height = MAX(child_requisition.height, requisition->height);
    }

    requisition->height += (border_width * 2);
    requisition->width += (border_width * 2);

	widget->requisition = *requisition;
}

static void
rstto_thumbnail_bar_size_allocate(GtkWidget *widget, GtkAllocation *allocation)
{
    RsttoThumbnailBar *bar = RSTTO_THUMBNAIL_BAR(widget);
    gint border_width = GTK_CONTAINER(bar)->border_width;
    gint spacing = 0;
	gtk_widget_style_get(widget, "spacing", &spacing, NULL);
    widget->allocation = *allocation;
    GtkAllocation child_allocation;
    GtkRequisition child_requisition;

    child_allocation.x = allocation->x + border_width;
    child_allocation.y = allocation->y + border_width;
    child_allocation.height = 0;
    child_allocation.width = 0;

    GSList *iter = bar->priv->thumbs;

    if (GTK_WIDGET_REALIZED(widget))
    {
        gdk_window_move_resize (widget->window,
                                allocation->x,
                                allocation->y,
                                allocation->width,
                                allocation->height);
        gdk_window_move_resize (bar->priv->client_window,
                                0,
                                0,
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
                    if (g_slist_position(bar->priv->thumbs, iter) < rstto_navigator_get_position(bar->priv->navigator))
                    {
                        bar->priv->offset += child_requisition.width + spacing;
                    }
                    if (g_slist_position(bar->priv->thumbs, iter) == rstto_navigator_get_position(bar->priv->navigator))
                    {
                        bar->priv->offset += (0.5 * child_requisition.width);
                    }
                }

                iter = g_slist_next(iter);
            }

            child_allocation.x -= bar->priv->offset;
            
            iter = bar->priv->thumbs;

            while(iter)
            {
                gtk_widget_get_child_requisition(GTK_WIDGET(iter->data), &child_requisition);
                child_allocation.height = allocation->height - (border_width * 2);
                child_allocation.width = child_requisition.width;

                if ((child_allocation.x < (allocation->x + allocation->width)) &&
                    ((child_allocation.x + child_allocation.width) > allocation->x + border_width))
                {
                    gtk_widget_set_child_visible(GTK_WIDGET(iter->data), TRUE);
                    gtk_widget_size_allocate(GTK_WIDGET(iter->data), &child_allocation);
                }
                else
                    gtk_widget_set_child_visible(GTK_WIDGET(iter->data), FALSE);

                child_allocation.x += child_requisition.width + spacing;
                iter = g_slist_next(iter);
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
                    if (g_slist_position(bar->priv->thumbs, iter) < rstto_navigator_get_position(bar->priv->navigator))
                    {
                        bar->priv->offset += child_requisition.height + spacing;
                    }
                    if (g_slist_position(bar->priv->thumbs, iter) == rstto_navigator_get_position(bar->priv->navigator))
                    {
                        bar->priv->offset += (0.5 * child_requisition.height);
                    }
                }

                iter = g_slist_next(iter);
            }

            child_allocation.y -= bar->priv->offset;
            
            iter = bar->priv->thumbs;

            while(iter)
            {

                gtk_widget_get_child_requisition(GTK_WIDGET(iter->data), &child_requisition);
                child_allocation.width = allocation->width - (border_width * 2);
                child_allocation.height = child_requisition.height;

                if (child_allocation.y < (allocation->y + allocation->height))
                    gtk_widget_set_child_visible(GTK_WIDGET(iter->data), TRUE);
                else
                    gtk_widget_set_child_visible(GTK_WIDGET(iter->data), FALSE);

                gtk_widget_size_allocate(GTK_WIDGET(iter->data), &child_allocation);
                child_allocation.y += child_requisition.height + spacing;
                iter = g_slist_next(iter);
            }
            break;
    }
}

static gboolean 
rstto_thumbnail_bar_expose(GtkWidget *widget, GdkEventExpose *ex)
{
    RsttoThumbnailBar *bar = RSTTO_THUMBNAIL_BAR(widget);

    GSList *iter = bar->priv->thumbs;

    GdkEventExpose *n_ex = g_new0(GdkEventExpose, 1);

    n_ex->type = ex->type;
    n_ex->window = ex->window;
    n_ex->send_event = ex->send_event;
    n_ex->area.x = ex->area.x;
    n_ex->area.y = ex->area.y;
    n_ex->area.width = ex->area.width;
    n_ex->area.height = ex->area.height;
    n_ex->count = ex->count;

    if (ex->window == bar->priv->client_window)
    {
        while(iter)
        {
            if (GTK_WIDGET_VISIBLE(iter->data) == TRUE)
            {
                switch (bar->priv->orientation)
                {
                    case GTK_ORIENTATION_HORIZONTAL:
                        /* why are these widgets not filtered out with the GTK_WIDGET_VISIBLE macro?*/
                        if (GTK_WIDGET(iter->data)->allocation.x > (GTK_WIDGET(bar)->allocation.x + GTK_WIDGET(bar)->allocation.width))
                            break;
                        if ((GTK_WIDGET(iter->data)->allocation.x + GTK_WIDGET(iter->data)->allocation.width) <= (GTK_WIDGET(bar)->allocation.x))
                            break;

                        /* first (partially) visible thumbnail */
                        if (( GTK_WIDGET(iter->data)->allocation.x - GTK_WIDGET(bar)->allocation.x) < 0)
                        {
                            n_ex->area.x = GTK_WIDGET(bar)->allocation.x;
                            n_ex->area.width = GTK_WIDGET(iter->data)->allocation.width - (GTK_WIDGET(bar)->allocation.x - GTK_WIDGET(bar)->allocation.x);
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
                                n_ex->area.width = GTK_WIDGET(iter->data)->allocation.width;
                            }

                        }
                        if (n_ex->region)
                            gdk_region_destroy(n_ex->region);
                        n_ex->region = gdk_region_rectangle(&(n_ex->area));
                        gtk_container_propagate_expose(GTK_CONTAINER(widget), GTK_WIDGET(iter->data), n_ex);
                        break;
                    case GTK_ORIENTATION_VERTICAL:
                        /* why are these widgets not filtered out with the GTK_WIDGET_VISIBLE macro?*/
                        if (GTK_WIDGET(iter->data)->allocation.y > (GTK_WIDGET(bar)->allocation.y + GTK_WIDGET(bar)->allocation.height))
                            break;
                        if ((GTK_WIDGET(iter->data)->allocation.y + GTK_WIDGET(iter->data)->allocation.height) <= (GTK_WIDGET(bar)->allocation.y))
                            break;

                        /* first (partially) visible thumbnail */
                        if (( GTK_WIDGET(iter->data)->allocation.y - GTK_WIDGET(bar)->allocation.y) < 0)
                        {
                            n_ex->area.y = GTK_WIDGET(bar)->allocation.y;
                            n_ex->area.height = GTK_WIDGET(iter->data)->allocation.height - (GTK_WIDGET(bar)->allocation.y - GTK_WIDGET(bar)->allocation.y);
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
                                n_ex->area.height = GTK_WIDGET(iter->data)->allocation.height;
                            }

                        }
                        if (n_ex->region)
                            gdk_region_destroy(n_ex->region);
                        n_ex->region = gdk_region_rectangle(&(n_ex->area));
                        gtk_container_propagate_expose(GTK_CONTAINER(widget), GTK_WIDGET(iter->data), n_ex);
                        break;
                }
            }
            iter = g_slist_next(iter);
        }
    }

    return FALSE;
}

static void
rstto_thumbnail_bar_realize(GtkWidget *widget)
{
    RsttoThumbnailBar *bar = RSTTO_THUMBNAIL_BAR(widget);

    GtkAllocation view_allocation;
    GdkWindowAttr attributes;
    gint attributes_mask;
    gint event_mask;
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
    bar->priv->client_window = gdk_window_new(widget->window,
                                              &attributes, attributes_mask);
    gdk_window_set_user_data (bar->priv->client_window, bar);

    widget->style = gtk_style_attach (widget->style, widget->window);

    gdk_window_show(bar->priv->client_window);

}

static void
rstto_thumbnail_bar_unrealize(GtkWidget *widget)
{
    RsttoThumbnailBar *bar = RSTTO_THUMBNAIL_BAR(widget);

    gdk_window_set_user_data (bar->priv->client_window, NULL);
    gdk_window_destroy (bar->priv->client_window);
    bar->priv->client_window = NULL;

    if (GTK_WIDGET_CLASS (parent_class)->unrealize)
        (* GTK_WIDGET_CLASS (parent_class)->unrealize) (widget);

}

GtkWidget *
rstto_thumbnail_bar_new(RsttoNavigator *navigator)
{
    RsttoThumbnailBar *bar;

    bar = g_object_new(RSTTO_TYPE_THUMBNAIL_BAR, NULL);

    bar->priv->navigator = navigator;

    g_signal_connect(G_OBJECT(navigator), "new-entry", G_CALLBACK(cb_rstto_thumbnail_bar_nav_new_entry), bar);
    g_signal_connect(G_OBJECT(navigator), "iter-changed", G_CALLBACK(cb_rstto_thumbnail_bar_nav_iter_changed), bar);
    g_signal_connect(G_OBJECT(navigator), "reordered", G_CALLBACK(cb_rstto_thumbnail_bar_nav_reordered), bar);
    g_signal_connect(G_OBJECT(navigator), "entry-removed", G_CALLBACK(cb_rstto_thumbnail_bar_nav_entry_removed), bar);

    return (GtkWidget *)bar;
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
	g_return_if_fail(GTK_IS_WIDGET(child));
    RsttoThumbnailBar *bar = RSTTO_THUMBNAIL_BAR(container);

	gtk_widget_set_parent(child, GTK_WIDGET(container));
	gtk_widget_set_parent_window(child, bar->priv->client_window);

    bar->priv->thumbs = g_slist_insert_sorted(bar->priv->thumbs, child, (GCompareFunc)cb_rstto_thumbnail_bar_compare);
}

static void
rstto_thumbnail_bar_remove(GtkContainer *container, GtkWidget *child)
{
	g_return_if_fail(GTK_IS_WIDGET(child));

    RsttoThumbnailBar *bar = RSTTO_THUMBNAIL_BAR(container);
	gboolean widget_was_visible;

	widget_was_visible = GTK_WIDGET_VISIBLE(child);

    bar->priv->thumbs = g_slist_remove(bar->priv->thumbs, child);

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

    g_slist_foreach(bar->priv->thumbs, (GFunc)callback, callback_data);

}

static GType
rstto_thumbnail_bar_child_type(GtkContainer *container)
{
    return GTK_TYPE_WIDGET;
}

/*
 * cb_rstto_thumbnail_bar_nav_new_entry :
 *
 * @nav    : RsttoNavigator 
 * @nr     : nr
 * @entry  :
 * @bar :
 *
 */
static void
cb_rstto_thumbnail_bar_nav_new_entry(RsttoNavigator *nav, gint nr, RsttoNavigatorEntry *entry, RsttoThumbnailBar *bar)
{
    GtkWidget *thumb;
    if (g_slist_length(bar->priv->thumbs) > 0)
    {
        thumb = rstto_thumbnail_new_from_widget(entry, bar->priv->thumbs->data);
    }
    else
    {
        thumb = rstto_thumbnail_new(entry, NULL);
    }
    g_signal_connect(G_OBJECT(thumb), "toggled", G_CALLBACK(cb_rstto_thumbnail_bar_thumbnail_toggled), bar);
    g_signal_connect(G_OBJECT(thumb), "button_press_event", G_CALLBACK(cb_rstto_thumbnail_bar_thumbnail_button_press_event), NULL);
    g_signal_connect(G_OBJECT(thumb), "button_release_event", G_CALLBACK(cb_rstto_thumbnail_bar_thumbnail_button_release_event), NULL);
    g_signal_connect(G_OBJECT(thumb), "motion_notify_event", G_CALLBACK(cb_rstto_thumbnail_bar_thumbnail_motion_notify_event), NULL);
    g_signal_connect(G_OBJECT(thumb), "scroll_event", G_CALLBACK(cb_rstto_thumbnail_bar_thumbnail_scroll_event), NULL);
    gtk_container_add(GTK_CONTAINER(bar), thumb);
    gtk_widget_show(thumb);
}

/*
 * cb_rstto_thumbnail_bar_nav_iter_changed :
 *
 * @nav    : RsttoNavigator 
 * @nr     : nr
 * @entry  :
 * @bar :
 *
 */
static void
cb_rstto_thumbnail_bar_nav_iter_changed(RsttoNavigator *nav, gint nr, RsttoNavigatorEntry *entry, RsttoThumbnailBar *bar)
{
    if (nr == -1)
    {
        gtk_container_foreach(GTK_CONTAINER(bar), (GtkCallback)gtk_widget_destroy, NULL);
    }
    GSList *iter = bar->priv->thumbs;


    int i = 0;

    while (iter != NULL)
    {
        if (entry != rstto_thumbnail_get_entry(RSTTO_THUMBNAIL(iter->data)))
        {
            gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON(iter->data), FALSE);
        }
        else
        {
            gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON(iter->data), TRUE);
        }
        i++;
        iter = g_slist_next(iter);
    }
    /* If the children should be autocentered... resize */
    if (bar->priv->auto_center == TRUE)
        gtk_widget_queue_resize(GTK_WIDGET(bar));
}

/*
 * cb_rstto_thumbnail_bar_nav_reordered :
 *
 * @nav    : RsttoNavigator 
 * @bar :
 *
 */
static void
cb_rstto_thumbnail_bar_nav_reordered (RsttoNavigator *nav, RsttoThumbnailBar *bar)
{
}

static void
cb_rstto_thumbnail_bar_thumbnail_toggled (RsttoThumbnail *thumb, RsttoThumbnailBar *bar)
{
    if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(thumb)) == TRUE)
    {
        bar->priv->auto_center = TRUE;
        rstto_navigator_entry_select (rstto_thumbnail_get_entry(thumb));
    }
}

static gint
cb_rstto_thumbnail_bar_compare (RsttoThumbnail *a, RsttoThumbnail *b)
{
    RsttoNavigatorEntry *_a = rstto_thumbnail_get_entry(a);
    RsttoNavigatorEntry *_b = rstto_thumbnail_get_entry(b);

    if (rstto_navigator_entry_get_position(_a) < rstto_navigator_entry_get_position(_b))
    {
        return -1;
    }
    else
    {
        return 1;
    }
}

static gboolean
cb_rstto_thumbnail_bar_thumbnail_button_press_event (RsttoThumbnail *thumb, GdkEventButton *event)
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
cb_rstto_thumbnail_bar_thumbnail_button_release_event (RsttoThumbnail *thumb, GdkEventButton *event)
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
cb_rstto_thumbnail_bar_thumbnail_motion_notify_event (RsttoThumbnail *thumb,
                                                      GdkEventMotion *event,
                                                      gpointer user_data)
{
    RsttoThumbnailBar *bar = RSTTO_THUMBNAIL_BAR(gtk_widget_get_parent(GTK_WIDGET(thumb)));
    gdouble x = event->x + GTK_WIDGET(thumb)->allocation.x;
    gdouble y = event->y + GTK_WIDGET(thumb)->allocation.y;

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
        }
        else
        {
            bar->priv->offset = bar->priv->motion.offset + (bar->priv->motion.current_y - y);
        }
        gtk_widget_queue_resize(GTK_WIDGET(bar));
    }
    return FALSE;
}

static gboolean
cb_rstto_thumbnail_bar_thumbnail_scroll_event (RsttoThumbnail *thumb,
                                               GdkEventScroll *event,
                                               gpointer *user_data)
{
    RsttoThumbnailBar *bar = RSTTO_THUMBNAIL_BAR(gtk_widget_get_parent(GTK_WIDGET(thumb)));
    switch(event->direction)
    {
        case GDK_SCROLL_UP:
        case GDK_SCROLL_LEFT:
            rstto_navigator_jump_back(bar->priv->navigator);
            break;
        case GDK_SCROLL_DOWN:
        case GDK_SCROLL_RIGHT:
            rstto_navigator_jump_forward(bar->priv->navigator);
            break;
    }
    return FALSE;

}

static void
cb_rstto_thumbnail_bar_nav_entry_removed(RsttoNavigator *nav, RsttoNavigatorEntry *entry, RsttoThumbnailBar *bar)
{
    GSList *iter = bar->priv->thumbs;

    while (iter != NULL)
    {
        if (entry == rstto_thumbnail_get_entry(RSTTO_THUMBNAIL(iter->data)))
        {
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(iter->data), FALSE);
            g_signal_handlers_disconnect_by_func(G_OBJECT(iter->data), G_CALLBACK(cb_rstto_thumbnail_bar_thumbnail_toggled), bar);
            gtk_widget_destroy(GTK_WIDGET(iter->data));
            break;
        }
        iter = g_slist_next(iter);
    }
}
