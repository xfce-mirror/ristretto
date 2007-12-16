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
};

static GtkWidgetClass *parent_class = NULL;

static void
rstto_thumbnail_init(RsttoThumbnail *);
static void
rstto_thumbnail_class_init(RsttoThumbnailClass *);
static void
rstto_thumbnail_destroy(GtkObject *object);

static void
rstto_thumbnail_size_request(GtkWidget *, GtkRequisition *);
static void
rstto_thumbnail_size_allocate(GtkWidget *, GtkAllocation *);
static gboolean 
rstto_thumbnail_expose(GtkWidget *, GdkEventExpose *);

static void
rstto_thumbnail_paint(RsttoThumbnail *thumb);

static void
cb_rstto_thumbnail_toggled(RsttoThumbnail *thumb, gpointer user_data);

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
                           GDK_BUTTON_PRESS_MASK);
}

static void
rstto_thumbnail_class_init(RsttoThumbnailClass *thumb_class)
{
    GtkWidgetClass *widget_class;
    GtkObjectClass *object_class;

    widget_class = (GtkWidgetClass*)thumb_class;
    object_class = (GtkObjectClass*)thumb_class;

    parent_class = g_type_class_peek_parent(thumb_class);

    widget_class->expose_event = rstto_thumbnail_expose;

    widget_class->size_request = rstto_thumbnail_size_request;
    widget_class->size_allocate = rstto_thumbnail_size_allocate;

    object_class->destroy = rstto_thumbnail_destroy;
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
    RsttoThumbnail *thumb = RSTTO_THUMBNAIL(widget);
    gint border_width =  0;
    widget->allocation = *allocation;

    if (GTK_WIDGET_REALIZED(widget))
    {
         gdk_window_move_resize (widget->window,
            allocation->x + border_width,
            allocation->y + border_width,
            allocation->width - border_width * 2,
            allocation->height - border_width * 2);
    }
}

static gboolean
rstto_thumbnail_expose(GtkWidget *widget, GdkEventExpose *event)
{
    RsttoThumbnail *thumb = RSTTO_THUMBNAIL(widget);

    if (GTK_WIDGET_REALIZED (widget))
    {
        rstto_thumbnail_paint(thumb);
    }

    return FALSE;
}

static void
rstto_thumbnail_destroy(GtkObject *object)
{

}

static void
rstto_thumbnail_paint(RsttoThumbnail *thumb)
{
    GdkPixmap *pixmap = NULL;
    GtkWidget *widget = GTK_WIDGET(thumb);

    GdkGC *gc = gdk_gc_new(GDK_DRAWABLE(widget->window));
    GdkGC *gc_bg_normal = gdk_gc_new(GDK_DRAWABLE(widget->window));
    GdkGC *gc_bg_selected = gdk_gc_new(GDK_DRAWABLE(widget->window));

    gdk_gc_set_foreground(gc_bg_selected,
                        &(widget->style->bg[GTK_STATE_SELECTED]));
    gdk_gc_set_foreground(gc_bg_normal,
                        &(widget->style->bg[GTK_STATE_NORMAL]));

    if(thumb->priv->entry)
    {
        GdkPixbuf *pixbuf = rstto_navigator_entry_get_thumb(
                                thumb->priv->entry,
                                widget->allocation.height - 4);

        pixmap = gdk_pixmap_new(widget->window, widget->allocation.width, widget->allocation.height, -1);

        gdk_gc_set_foreground(gc, &widget->style->fg[GTK_STATE_NORMAL]);

        if(thumb->priv->selected == TRUE)
        {
            gdk_draw_rectangle(GDK_DRAWABLE(pixmap),
                            gc_bg_selected,
                            TRUE,
                            0, 0, 
                            widget->allocation.width,
                            widget->allocation.height);
        }
        else
        {
            gdk_draw_rectangle(GDK_DRAWABLE(pixmap),
                            gc_bg_normal,
                            TRUE,
                            0, 0, 
                            widget->allocation.width,
                            widget->allocation.height);
        }

        if(pixbuf)
        {
            gdk_draw_pixbuf(GDK_DRAWABLE(pixmap),
                            gc,
                            pixbuf,
                            0, 0,
                            (0.5 * (widget->allocation.width - gdk_pixbuf_get_width(pixbuf))),
                            (0.5 * (widget->allocation.height - gdk_pixbuf_get_height(pixbuf))),
                            -1, -1,
                            GDK_RGB_DITHER_NORMAL,
                            0, 0);
        }

        gdk_draw_drawable(GDK_DRAWABLE(widget->window),
            gc,
            pixmap,
            0, 0,
            widget->allocation.x, widget->allocation.y,
            widget->allocation.width,
            widget->allocation.height);

    }
}

GtkWidget *
rstto_thumbnail_new(RsttoNavigatorEntry *entry)
{
    g_return_val_if_fail(entry != NULL, NULL);

    RsttoThumbnail *thumb = g_object_new(RSTTO_TYPE_THUMBNAIL, NULL);

    thumb->priv->entry = entry;

#if GTK_CHECK_VERSION(2,12,0)
    ThunarVfsInfo *info = rstto_navigator_entry_get_info(thumb->priv->entry);
    
    gtk_widget_set_tooltip_text(GTK_WIDGET(thumb), thunar_vfs_path_dup_string(info->path));
#endif

    return GTK_WIDGET(thumb);
}

/* CALLBACKS */
/*************/

static void
cb_rstto_thumbnail_toggled(RsttoThumbnail *thumb, gpointer user_data)
{

}
