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

struct _RsttoThumbnailPriv
{
    RsttoNavigatorEntry *entry;
    gboolean             selected;
    GSList              *group;
};

static GtkWidgetClass *parent_class = NULL;

static void
rstto_thumbnail_init(RsttoThumbnail *);
static void
rstto_thumbnail_class_init(RsttoThumbnailClass *);
static void
rstto_thumbnail_finalize(GObject *object);

static void
rstto_thumbnail_size_request(GtkWidget *, GtkRequisition *);
static void
rstto_thumbnail_size_allocate(GtkWidget *, GtkAllocation *);
static gboolean 
rstto_thumbnail_expose(GtkWidget *, GdkEventExpose *);

static void
rstto_thumbnail_paint(RsttoThumbnail *thumb);

static void
rstto_thumbnail_clicked(GtkButton *);

GType
rstto_thumbnail_get_type ()
{
    static GType rstto_thumbnail_type = 0;

    if (!rstto_thumbnail_type)
    {
        static const GTypeInfo rstto_thumbnail_info = 
        {
            sizeof (RsttoThumbnailClass),
            (GBaseInitFunc) NULL,
            (GBaseFinalizeFunc) NULL,
            (GClassInitFunc) rstto_thumbnail_class_init,
            (GClassFinalizeFunc) NULL,
            NULL,
            sizeof (RsttoThumbnail),
            0,
            (GInstanceInitFunc) rstto_thumbnail_init,
            NULL
        };

        rstto_thumbnail_type = g_type_register_static (GTK_TYPE_BUTTON, "RsttoThumbnail", &rstto_thumbnail_info, 0);
    }
    return rstto_thumbnail_type;
}

static void
rstto_thumbnail_init(RsttoThumbnail *thumb)
{
    thumb->priv = g_new0(RsttoThumbnailPriv, 1);

    gtk_widget_set_redraw_on_allocate(GTK_WIDGET(thumb), TRUE);
    gtk_widget_set_events (GTK_WIDGET(thumb),
                           GDK_POINTER_MOTION_MASK);
}

static void
rstto_thumbnail_class_init(RsttoThumbnailClass *thumb_class)
{
    GtkWidgetClass *widget_class;
    GObjectClass *object_class;
    GtkButtonClass *button_class;

    widget_class = (GtkWidgetClass*)thumb_class;
    object_class = (GObjectClass*)thumb_class;
    button_class = (GtkButtonClass*)thumb_class;

    parent_class = g_type_class_peek_parent(thumb_class);

    widget_class->expose_event = rstto_thumbnail_expose;

    widget_class->size_request = rstto_thumbnail_size_request;
    widget_class->size_allocate = rstto_thumbnail_size_allocate;

    button_class->clicked = rstto_thumbnail_clicked;

    object_class->finalize = rstto_thumbnail_finalize;
}

static void
rstto_thumbnail_size_request(GtkWidget *widget, GtkRequisition *requisition)
{
    requisition->height = 70;
    requisition->width = 70;
}

static void
rstto_thumbnail_size_allocate(GtkWidget *widget, GtkAllocation *allocation)
{
    widget->allocation = *allocation;

    parent_class->size_allocate(widget, allocation);
}

static gboolean
rstto_thumbnail_expose(GtkWidget *widget, GdkEventExpose *event)
{
    RsttoThumbnail *thumb = RSTTO_THUMBNAIL(widget);

    if (GTK_WIDGET_REALIZED (widget))
    {
        GdkRegion *region = event->region;

        gdk_window_begin_paint_region(widget->window, region);
        rstto_thumbnail_paint(thumb);
        gdk_window_end_paint(widget->window);
    }

    return FALSE;
}

static void
rstto_thumbnail_finalize(GObject *object)
{
    RsttoThumbnail *thumb = RSTTO_THUMBNAIL(object);

    if (thumb->priv->entry)
    {
        GSList *group = g_slist_remove(thumb->priv->group, thumb);
        GSList *iter = group;

        while(iter)
        {
            RsttoThumbnail *thumb_iter = iter->data;
            thumb_iter->priv->group = group;
            iter = g_slist_next(iter);
        }
        thumb->priv->entry = NULL;
    }
}

static void
rstto_thumbnail_paint(RsttoThumbnail *thumb)
{
    GtkWidget *widget = GTK_WIDGET(thumb);
    gint border_width = 0;

    GdkGC *gc = gdk_gc_new(GDK_DRAWABLE(widget->window));

    GtkStateType state = GTK_WIDGET_STATE(widget);

    if(thumb->priv->entry)
    {

        if (GTK_WIDGET_STATE(widget) != GTK_STATE_PRELIGHT)
        {
            if (rstto_navigator_entry_is_selected(thumb->priv->entry))
            {
                gtk_widget_set_state(widget, GTK_STATE_SELECTED);
            }
            else
            {
                gtk_widget_set_state(widget, GTK_STATE_NORMAL);
            }
        }

        GdkPixbuf *pixbuf = rstto_navigator_entry_get_thumb(
                                thumb->priv->entry,
                                widget->allocation.height - 4);

        gtk_paint_box(widget->style,
                      widget->window,
                      state,
                      GTK_SHADOW_ETCHED_IN,
                      NULL,
                      widget,
                      NULL,
                      widget->allocation.x, widget->allocation.y,
                      widget->allocation.width, widget->allocation.height);

        if(pixbuf)
        {
            gdk_draw_pixbuf(GDK_DRAWABLE(widget->window),
                            NULL,
                            pixbuf,
                            0, 0,
                            (0.5 * (widget->allocation.width - gdk_pixbuf_get_width(pixbuf))) + widget->allocation.x,
                            (0.5 * (widget->allocation.height - gdk_pixbuf_get_height(pixbuf))) + widget->allocation.y,
                            -1, -1,
                            GDK_RGB_DITHER_NORMAL,
                            0, 0);
        }
    }
}

GtkWidget *
rstto_thumbnail_new(RsttoNavigatorEntry *entry, GSList *thumb_list)
{
    g_return_val_if_fail(entry != NULL, NULL);

    RsttoThumbnail *thumb = g_object_new(RSTTO_TYPE_THUMBNAIL, NULL);

    thumb->priv->entry = entry;

#if GTK_CHECK_VERSION(2,12,0)
    ThunarVfsInfo *info = rstto_navigator_entry_get_info(thumb->priv->entry);
    
    gtk_widget_set_tooltip_text(GTK_WIDGET(thumb), thunar_vfs_path_dup_string(info->path));
#else
    /* TODO: gtktooltip stuff */
#endif

    {
        thumb->priv->group = g_slist_prepend(thumb_list, thumb);
        GSList *iter = thumb->priv->group;
        while(iter)
        {
            RsttoThumbnail *iter_thumb = iter->data;
            iter_thumb->priv->group = thumb->priv->group;
            iter = iter->next;
        }
    }

    return GTK_WIDGET(thumb);
}

GtkWidget *
rstto_thumbnail_new_from_widget(RsttoNavigatorEntry *entry, RsttoThumbnail *sibling)
{
    return rstto_thumbnail_new(entry, sibling->priv->group);
}

RsttoNavigatorEntry *
rstto_thumbnail_get_entry (RsttoThumbnail *thumb)
{
    return thumb->priv->entry;
}

/* CALLBACKS */
/*************/

static void
rstto_thumbnail_clicked(GtkButton *button)
{
    RsttoThumbnail *thumb = RSTTO_THUMBNAIL(button);
    RsttoNavigatorEntry *entry = rstto_thumbnail_get_entry(thumb);

    rstto_navigator_entry_select(entry);

    gtk_widget_queue_draw (GTK_WIDGET (button));
}
