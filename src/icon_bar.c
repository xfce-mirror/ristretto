/*-
 * Copyright (c) 2004-2006 os-cillation e.K.
 * Copyright (c) 2012 Stephan Arts <stephan@xfce.org>
 *
 * Written by Benedikt Meurer <benny@xfce.org>.
 * Modified by Stephan Arts <stephan@xfce.org>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <glib.h>
#include <gtk/gtk.h>

#include <libxfce4util/libxfce4util.h>

#include <libexif/exif-data.h>

#include "util.h"
#include "file.h"
#include "thumbnailer.h"
#include "settings.h"
#include "marshal.h"
#include "icon_bar.h"

#define MINIMUM_ICON_ITEM_WIDTH 32
#define ICON_TEXT_PADDING 1

#define RSTTO_ICON_BAR_GET_PRIVATE(obj) ( \
            G_TYPE_INSTANCE_GET_PRIVATE ( \
                    (obj), \
                    RSTTO_TYPE_ICON_BAR, \
                    RsttoIconBarPrivate))

#define RSTTO_ICON_BAR_VALID_MODEL_AND_COLUMNS(obj) \
            ((obj)->priv->model != NULL && \
            (obj)->priv->file_column != -1)



typedef struct _RsttoIconBarItem RsttoIconBarItem;

enum
{
    PROP_0,
    PROP_ORIENTATION,
    PROP_FILE_COLUMN,
    PROP_MODEL,
    PROP_ACTIVE,
    PROP_SHOW_TEXT,
};

enum
{
    SELECTION_CHANGED,
    LAST_SIGNAL,
};

static GdkPixbuf *thumbnail_missing = NULL;

static void
rstto_icon_bar_destroy (GtkObject *object);

static void
rstto_icon_bar_finalize (GObject *object);

static void
rstto_icon_bar_get_property (
        GObject    *object,
        guint       prop_id,
        GValue     *value,
        GParamSpec *pspec);

static void
rstto_icon_bar_set_property (
        GObject      *object,
        guint         prop_id,
        const GValue *value,
        GParamSpec   *pspec);

static void
rstto_icon_bar_style_set (
        GtkWidget *widget,
        GtkStyle  *previous_style);

static void
rstto_icon_bar_realize (GtkWidget *widget);
static void
rstto_icon_bar_unrealize (GtkWidget *widget);

static void
rstto_icon_bar_size_request (
        GtkWidget      *widget,
        GtkRequisition *requisition);

static void
rstto_icon_bar_size_allocate (
        GtkWidget     *widget,
        GtkAllocation *allocation);

static gboolean
rstto_icon_bar_expose (
        GtkWidget      *widget,
        GdkEventExpose *expose);

static gboolean
rstto_icon_bar_leave (
        GtkWidget        *widget,
        GdkEventCrossing *event);

static gboolean
rstto_icon_bar_motion (
        GtkWidget      *widget,
        GdkEventMotion *event);

static gboolean
rstto_icon_bar_scroll (
        GtkWidget      *widget,
        GdkEventScroll *event);

static gboolean
rstto_icon_bar_button_press (
        GtkWidget      *widget,
        GdkEventButton *event);

static gboolean
rstto_icon_bar_button_release (
        GtkWidget      *widget,
        GdkEventButton *event);

static void
rstto_icon_bar_set_adjustments (
        RsttoIconBar  *icon_bar,
        GtkAdjustment *hadj,
        GtkAdjustment *vadj);

static void
cb_rstto_icon_bar_adjustment_value_changed (
        GtkAdjustment *adjustment,
        RsttoIconBar  *icon_bar);

static void
rstto_icon_bar_invalidate (RsttoIconBar *icon_bar);

static RsttoIconBarItem *
rstto_icon_bar_get_item_at_pos (
        RsttoIconBar *icon_bar,
        gint          x,
        gint          y);

static void
rstto_icon_bar_queue_draw_item (
        RsttoIconBar     *icon_bar,
        RsttoIconBarItem *item);

static void
rstto_icon_bar_paint_item (
        RsttoIconBar     *icon_bar,
        RsttoIconBarItem *item,
        GdkRectangle     *area);

static void
rstto_icon_bar_calculate_item_size (
        RsttoIconBar     *icon_bar,
        RsttoIconBarItem *item);

static void
rstto_icon_bar_adjustment_changed (
        RsttoIconBar  *icon_bar,
        GtkAdjustment *adjustment);

static void
rstto_icon_bar_adjustment_value_changed (
        RsttoIconBar  *icon_bar,
        GtkAdjustment *adjustment);

static void
cb_rstto_thumbnail_size_changed (
        GObject *settings,
        GParamSpec *pspec,
        gpointer user_data);

static void
rstto_icon_bar_update_missing_icon (RsttoIconBar *icon_bar);


static RsttoIconBarItem *
rstto_icon_bar_item_new (void);

static void
rstto_icon_bar_item_free (RsttoIconBarItem *item);

static void
rstto_icon_bar_item_invalidate (RsttoIconBarItem *item);

static void
rstto_icon_bar_build_items (RsttoIconBar *icon_bar);

static void
rstto_icon_bar_row_changed (
        GtkTreeModel *model,
        GtkTreePath  *path,
        GtkTreeIter  *iter,
        RsttoIconBar *icon_bar);

static void
rstto_icon_bar_row_inserted (
        GtkTreeModel *model,
        GtkTreePath  *path,
        GtkTreeIter  *iter,
        RsttoIconBar *icon_bar);

static void
rstto_icon_bar_row_deleted (
        GtkTreeModel *model,
        GtkTreePath  *path,
        RsttoIconBar *icon_bar);

static void
rstto_icon_bar_rows_reordered (
        GtkTreeModel *model,
        GtkTreePath  *path,
        GtkTreeIter  *iter,
        gint         *new_order,
        RsttoIconBar *icon_bar);

struct _RsttoIconBarItem
{
    GtkTreeIter iter;
    gint        index;

    gint        width;
    gint        height;

    gint        pixbuf_width;
    gint        pixbuf_height;

    gint        layout_width;
    gint        layout_height;
};

struct _RsttoIconBarPrivate
{
    GdkWindow      *bin_window;

    gint            width;
    gint            height;

    gint            pixbuf_column;
    gint            file_column;

    RsttoIconBarItem *active_item;
    RsttoIconBarItem *single_click_item;
    RsttoIconBarItem *cursor_item;

    GList          *items;
    gint            item_width;
    gint            item_height;

    GtkAdjustment  *hadjustment;
    GtkAdjustment  *vadjustment;

    RsttoSettings  *settings;
    RsttoThumbnailer *thumbnailer;

    RsttoThumbnailSize thumbnail_size;

    gboolean        auto_center; /* automatically center the active item */

    GtkOrientation  orientation;

    GtkTreeModel   *model;

    PangoLayout    *layout;

    gboolean        show_text;
};



static guint icon_bar_signals[LAST_SIGNAL];

G_DEFINE_TYPE (RsttoIconBar, rstto_icon_bar, GTK_TYPE_CONTAINER)


static void
rstto_icon_bar_class_init (RsttoIconBarClass *klass)
{
    GtkObjectClass *gtkobject_class;
    GtkWidgetClass *gtkwidget_class;
    GObjectClass   *gobject_class;

    g_type_class_add_private (klass, sizeof (RsttoIconBarPrivate));

    gobject_class = G_OBJECT_CLASS (klass);
    gobject_class->finalize = rstto_icon_bar_finalize;
    gobject_class->get_property = rstto_icon_bar_get_property;
    gobject_class->set_property = rstto_icon_bar_set_property;

    gtkobject_class = GTK_OBJECT_CLASS (klass);
    gtkobject_class->destroy = rstto_icon_bar_destroy;

    gtkwidget_class = GTK_WIDGET_CLASS (klass);
    gtkwidget_class->style_set = rstto_icon_bar_style_set;
    gtkwidget_class->realize = rstto_icon_bar_realize;
    gtkwidget_class->unrealize = rstto_icon_bar_unrealize;
    gtkwidget_class->size_request = rstto_icon_bar_size_request;
    gtkwidget_class->size_allocate = rstto_icon_bar_size_allocate;
    gtkwidget_class->expose_event = rstto_icon_bar_expose;
    gtkwidget_class->leave_notify_event = rstto_icon_bar_leave;
    gtkwidget_class->motion_notify_event = rstto_icon_bar_motion;
    gtkwidget_class->scroll_event = rstto_icon_bar_scroll;
    gtkwidget_class->button_press_event = rstto_icon_bar_button_press;
    gtkwidget_class->button_release_event = rstto_icon_bar_button_release;

    klass->set_scroll_adjustments = rstto_icon_bar_set_adjustments;

    /**
     * RsttoIconBar:orientation:
     *
     * The orientation of the icon bar.
     *
     * Default value: %GTK_ORIENTATION_VERTICAL
     **/
    g_object_class_install_property (gobject_class,
            PROP_ORIENTATION,
            g_param_spec_enum ("orientation",
                _("Orientation"),
                _("The orientation of the iconbar"),
                GTK_TYPE_ORIENTATION,
                GTK_ORIENTATION_VERTICAL,
                G_PARAM_READWRITE));

    /**
     * RsttoIconBar:file-column:
     *
     * The ::file-column property contains the number of the model column
     * containing the files which are displayed. The text column must be
     * of type #RSTTO_TYPE_FILE. If this property is set to -1, no
     * details are displayed.
     **/
    g_object_class_install_property (gobject_class,
            PROP_FILE_COLUMN,
            g_param_spec_int ("file-column",
                _("File column"),
                _("Model column used to retrieve the file from"),
                -1, G_MAXINT, -1,
                G_PARAM_READWRITE));

    /**
     * RsttoIconBar:model:
     *
     * The model for the icon bar.
     **/
    g_object_class_install_property (gobject_class,
            PROP_MODEL,
            g_param_spec_object ("model",
                _("Icon Bar Model"),
                _("Model for the icon bar"),
                GTK_TYPE_TREE_MODEL,
                G_PARAM_READWRITE));

    /**
     * RsttoIconBar:active:
     *
     * The item which is currently active.
     *
     * Allowed values: >= -1
     *
     * Default value: -1
     **/
    g_object_class_install_property (gobject_class,
            PROP_ACTIVE,
            g_param_spec_int ("active",
                _("Active"),
                _("Active item index"),
                -1, G_MAXINT, -1,
                G_PARAM_READWRITE));

    /**
     * RsttoIconBar:show-text:
     *
     * Show text under icon.
     *
     * Allowed values: TRUE, FALSE
     *
     * Default value: TRUE
     **/
    g_object_class_install_property (gobject_class,
            PROP_SHOW_TEXT,
            g_param_spec_boolean ("show-text",
                _("Show Text"),
                _("Show Text"),
                TRUE,
                G_PARAM_READWRITE));

    gtk_widget_class_install_style_property (gtkwidget_class,
            g_param_spec_boxed ("active-item-fill-color",
                _("Active item fill color"),
                _("Active item fill color"),
                GDK_TYPE_COLOR,
                G_PARAM_READABLE));

    gtk_widget_class_install_style_property (gtkwidget_class,
            g_param_spec_boxed ("active-item-border-color",
                _("Active item border color"),
                _("Active item border color"),
                GDK_TYPE_COLOR,
                G_PARAM_READABLE));

    gtk_widget_class_install_style_property (gtkwidget_class,
            g_param_spec_boxed ("active-item-text-color",
                _("Active item text color"),
                _("Active item text color"),
                GDK_TYPE_COLOR,
                G_PARAM_READABLE));

    gtk_widget_class_install_style_property (gtkwidget_class,
            g_param_spec_boxed ("cursor-item-fill-color",
                _("Cursor item fill color"),
                _("Cursor item fill color"),
                GDK_TYPE_COLOR,
                G_PARAM_READABLE));

    gtk_widget_class_install_style_property (gtkwidget_class,
            g_param_spec_boxed ("cursor-item-border-color",
                _("Cursor item border color"),
                _("Cursor item border color"),
                GDK_TYPE_COLOR,
                G_PARAM_READABLE));

    gtk_widget_class_install_style_property (gtkwidget_class,
            g_param_spec_boxed ("cursor-item-text-color",
                _("Cursor item text color"),
                _("Cursor item text color"),
                GDK_TYPE_COLOR,
                G_PARAM_READABLE));

    /**
     * RsttoIconBar::set-scroll-adjustments:
     * @icon_bar    : The #RsttoIconBar.
     * @hadjustment : The horizontal adjustment.
     * @vadjustment : The vertical adjustment.
     *
     * Used internally to make the #RsttoIconBar scrollable.
     **/
    gtkwidget_class->set_scroll_adjustments_signal =
        g_signal_new ("set-scroll-adjustments",
                G_TYPE_FROM_CLASS (gobject_class),
                G_SIGNAL_RUN_LAST,
                G_STRUCT_OFFSET (RsttoIconBarClass, set_scroll_adjustments),
                NULL, NULL,
                _rstto_marshal_VOID__OBJECT_OBJECT,
                G_TYPE_NONE, 2,
                GTK_TYPE_ADJUSTMENT,
                GTK_TYPE_ADJUSTMENT);

    /**
     * RsttoIconBar::selection-changed:
     * @icon_bar  : The #RsttoIconBar.
     *
     * This signal is emitted whenever the currently selected icon
     * changes.
     **/
    icon_bar_signals[SELECTION_CHANGED] =
        g_signal_new ("selection-changed",
                G_TYPE_FROM_CLASS (gobject_class),
                G_SIGNAL_RUN_FIRST,
                G_STRUCT_OFFSET (RsttoIconBarClass, selection_changed),
                NULL, NULL,
                g_cclosure_marshal_VOID__VOID,
                G_TYPE_NONE, 0);
}



static void
rstto_icon_bar_init (RsttoIconBar *icon_bar)
{
    icon_bar->priv = RSTTO_ICON_BAR_GET_PRIVATE (icon_bar);

    icon_bar->priv->orientation = GTK_ORIENTATION_VERTICAL;
    icon_bar->priv->pixbuf_column = -1;
    icon_bar->priv->file_column = -1;
    icon_bar->priv->show_text = TRUE;
    icon_bar->priv->auto_center = TRUE;
    icon_bar->priv->settings = rstto_settings_new ();
    icon_bar->priv->thumbnailer = rstto_thumbnailer_new();

    icon_bar->priv->thumbnail_size = rstto_settings_get_uint_property (
            icon_bar->priv->settings,
            "thumbnail-size");

    rstto_icon_bar_update_missing_icon (icon_bar);

    icon_bar->priv->layout = gtk_widget_create_pango_layout (GTK_WIDGET (icon_bar), NULL);
    pango_layout_set_width (icon_bar->priv->layout, -1);
    pango_layout_set_wrap (icon_bar->priv->layout,
            PANGO_WRAP_WORD_CHAR);
    pango_layout_set_ellipsize (icon_bar->priv->layout,
            PANGO_ELLIPSIZE_END);

    GTK_WIDGET_UNSET_FLAGS (icon_bar, GTK_CAN_FOCUS);

    rstto_icon_bar_set_adjustments (icon_bar, NULL, NULL);

    g_signal_connect (
            G_OBJECT(icon_bar->priv->settings),
            "notify::thumbnail-size",
            G_CALLBACK (cb_rstto_thumbnail_size_changed),
            icon_bar);
}



static void
rstto_icon_bar_destroy (GtkObject *object)
{
    RsttoIconBar *icon_bar = RSTTO_ICON_BAR (object);

    rstto_icon_bar_set_model (icon_bar, NULL);

    (*GTK_OBJECT_CLASS (rstto_icon_bar_parent_class)->destroy) (object);
}



static void
rstto_icon_bar_finalize (GObject *object)
{
    RsttoIconBar *icon_bar = RSTTO_ICON_BAR (object);

    g_object_unref (G_OBJECT (icon_bar->priv->layout));
    g_object_unref (G_OBJECT (icon_bar->priv->settings));
    g_object_unref (G_OBJECT (icon_bar->priv->thumbnailer));

    (*G_OBJECT_CLASS (rstto_icon_bar_parent_class)->finalize) (object);
}



static void
rstto_icon_bar_get_property (
        GObject    *object,
        guint       prop_id,
        GValue     *value,
        GParamSpec *pspec)
{
    RsttoIconBar *icon_bar = RSTTO_ICON_BAR (object);

    switch (prop_id)
    {
        case PROP_ORIENTATION:
            g_value_set_enum (value, icon_bar->priv->orientation);
            break;

        case PROP_FILE_COLUMN:
            g_value_set_int (value, icon_bar->priv->file_column);
            break;

        case PROP_MODEL:
            g_value_set_object (value, icon_bar->priv->model);
            break;

        case PROP_ACTIVE:
            g_value_set_int (value, rstto_icon_bar_get_active (icon_bar));
            break;

        case PROP_SHOW_TEXT:
            g_value_set_boolean (value, rstto_icon_bar_get_show_text (icon_bar));
            break;

        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
            break;
    }
}



static void
rstto_icon_bar_set_property (
        GObject      *object,
        guint         prop_id,
        const GValue *value,
        GParamSpec   *pspec)
{
    RsttoIconBar *icon_bar = RSTTO_ICON_BAR (object);

    switch (prop_id)
    {
        case PROP_ORIENTATION:
            rstto_icon_bar_set_orientation (icon_bar, g_value_get_enum (value));
            break;

        case PROP_FILE_COLUMN:
            rstto_icon_bar_set_file_column (icon_bar, g_value_get_int (value));
            break;

        case PROP_MODEL:
            rstto_icon_bar_set_model (icon_bar, g_value_get_object (value));
            break;

        case PROP_ACTIVE:
            rstto_icon_bar_set_active (icon_bar, g_value_get_int (value));
            break;

        case PROP_SHOW_TEXT:
            rstto_icon_bar_set_show_text (icon_bar, g_value_get_boolean (value));
            break;

        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
            break;
    }
}



static void
rstto_icon_bar_style_set (
        GtkWidget *widget,
        GtkStyle  *previous_style)
{
    RsttoIconBar *icon_bar = RSTTO_ICON_BAR (widget);

    (*GTK_WIDGET_CLASS (rstto_icon_bar_parent_class)->style_set) (widget, previous_style);

    if (GTK_WIDGET_REALIZED (widget))
    {
        gdk_window_set_background (icon_bar->priv->bin_window,
                &(gtk_widget_get_style (widget)->base[widget->state]));
    }
}



static void
rstto_icon_bar_realize (GtkWidget *widget)
{
    GdkWindowAttr attributes;
    RsttoIconBar   *icon_bar = RSTTO_ICON_BAR (widget);
    gint          attributes_mask;
    GtkAllocation allocation;
    GdkWindow    *window;
    GtkStyle     *style;

    GTK_WIDGET_SET_FLAGS (widget, GTK_REALIZED);

    gtk_widget_get_allocation (widget, &allocation);

    attributes.window_type = GDK_WINDOW_CHILD;
    attributes.x = allocation.x;
    attributes.y = allocation.y;
    attributes.width = allocation.width;
    attributes.height = allocation.height;
    attributes.wclass = GDK_INPUT_OUTPUT;
    attributes.visual = gtk_widget_get_visual (widget);
    attributes.colormap = gtk_widget_get_colormap (widget);
    attributes.event_mask = GDK_VISIBILITY_NOTIFY_MASK;
    attributes_mask = GDK_WA_X | GDK_WA_Y | GDK_WA_VISUAL | GDK_WA_COLORMAP;

    window = gdk_window_new (gtk_widget_get_parent_window (widget),
            &attributes, attributes_mask);
    gtk_widget_set_window (widget, window);
    gdk_window_set_user_data (window, widget);

    attributes.x = 0;
    attributes.y = 0;
    attributes.width = MAX (icon_bar->priv->width, allocation.width);
    attributes.height = MAX (icon_bar->priv->height, allocation.height);
    attributes.event_mask = (GDK_SCROLL_MASK
            | GDK_EXPOSURE_MASK
            | GDK_LEAVE_NOTIFY_MASK
            | GDK_POINTER_MOTION_MASK
            | GDK_BUTTON_PRESS_MASK
            | GDK_BUTTON_RELEASE_MASK
            | GDK_KEY_PRESS_MASK
            | GDK_KEY_RELEASE_MASK)
            | gtk_widget_get_events (widget);
    attributes_mask = GDK_WA_X | GDK_WA_Y | GDK_WA_VISUAL | GDK_WA_COLORMAP;

    icon_bar->priv->bin_window = gdk_window_new (window, &attributes, attributes_mask);
    gdk_window_set_user_data (icon_bar->priv->bin_window, widget);

    style = gtk_style_attach (gtk_widget_get_style (widget), window);
    gtk_widget_set_style (widget, style);
    gdk_window_set_background (window, &(gtk_widget_get_style (widget)->base[widget->state]));
    gdk_window_set_background (icon_bar->priv->bin_window, &(gtk_widget_get_style (widget)->base[widget->state]));
    gdk_window_show (icon_bar->priv->bin_window);
}



static void
rstto_icon_bar_unrealize (GtkWidget *widget)
{
    RsttoIconBar *icon_bar = RSTTO_ICON_BAR (widget);

    gdk_window_set_user_data (icon_bar->priv->bin_window, NULL);
    gdk_window_destroy (icon_bar->priv->bin_window);
    icon_bar->priv->bin_window = NULL;

    /* GtkWidget::unrealize destroys children and widget->window */
    (*GTK_WIDGET_CLASS (rstto_icon_bar_parent_class)->unrealize) (widget);
}



static void
rstto_icon_bar_size_request (
        GtkWidget      *widget,
        GtkRequisition *requisition)
{
    RsttoIconBarItem *item;
    RsttoIconBar     *icon_bar = RSTTO_ICON_BAR (widget);
    GList          *lp;
    gint            n = 0;
    gint            max_width = 0;
    gint            max_height = 0;

    if (!RSTTO_ICON_BAR_VALID_MODEL_AND_COLUMNS (icon_bar)
            || icon_bar->priv->items == NULL)
    {
        icon_bar->priv->width = requisition->width = 0;
        icon_bar->priv->height = requisition->height = 0;
        return;
    }

    /* calculate max item size */
    for (lp = icon_bar->priv->items; lp != NULL; ++n, lp = lp->next)
    {
        item = lp->data;
        rstto_icon_bar_calculate_item_size (icon_bar, item);
        if (item->width > max_width)
            max_width = item->width;
        if (item->height > max_height)
            max_height = item->height;
    }

    icon_bar->priv->item_width = max_width;
    icon_bar->priv->item_height = max_height;

    if (icon_bar->priv->orientation == GTK_ORIENTATION_VERTICAL)
    {
        icon_bar->priv->width = requisition->width = icon_bar->priv->item_width;
        icon_bar->priv->height = requisition->height = icon_bar->priv->item_height * n;
    }
    else
    {
        icon_bar->priv->width = requisition->width = icon_bar->priv->item_width * n;
        icon_bar->priv->height = requisition->height = icon_bar->priv->item_height;
    }
}



static void
rstto_icon_bar_size_allocate (
        GtkWidget     *widget,
        GtkAllocation *allocation)
{
    RsttoIconBar *icon_bar = RSTTO_ICON_BAR (widget);
    gdouble value = 0.0;
    gdouble page_size = 0.0;

    gtk_widget_set_allocation (widget, allocation);

    if (!icon_bar->priv->active_item)
        g_warning ("thumbnail bar shown when no images are available");

    if (GTK_WIDGET_REALIZED (widget))
    {
        gdk_window_move_resize (gtk_widget_get_window (widget),
                allocation->x,
                allocation->y,
                allocation->width,
                allocation->height);
        gdk_window_resize (icon_bar->priv->bin_window,
                MAX (icon_bar->priv->width, allocation->width),
                MAX (icon_bar->priv->height, allocation->height));
    }

    if (icon_bar->priv->orientation == GTK_ORIENTATION_VERTICAL)
    {
        value = gtk_adjustment_get_value (icon_bar->priv->vadjustment);
        value = value / icon_bar->priv->vadjustment->upper * MAX (allocation->height, icon_bar->priv->height);
    }
    else
    {
        value = gtk_adjustment_get_value (icon_bar->priv->hadjustment);
        value = value / icon_bar->priv->hadjustment->upper * MAX (allocation->width, icon_bar->priv->width);
    }

    icon_bar->priv->hadjustment->page_size = allocation->width;
    icon_bar->priv->hadjustment->page_increment = allocation->width * 0.9;
    icon_bar->priv->hadjustment->step_increment = allocation->width * 0.1;
    icon_bar->priv->hadjustment->lower = 0;
    icon_bar->priv->hadjustment->upper = MAX (allocation->width, icon_bar->priv->width);

    icon_bar->priv->vadjustment->page_size = allocation->height;
    icon_bar->priv->vadjustment->page_increment = allocation->height * 0.9;
    icon_bar->priv->vadjustment->step_increment = allocation->height * 0.1;
    icon_bar->priv->vadjustment->lower = 0;
    icon_bar->priv->vadjustment->upper = MAX (allocation->height, icon_bar->priv->height);


    if (icon_bar->priv->orientation == GTK_ORIENTATION_VERTICAL)
    {
        icon_bar->priv->width = allocation->width;
        icon_bar->priv->item_width = icon_bar->priv->width;
        icon_bar->priv->hadjustment->value = 0;

        page_size = gtk_adjustment_get_page_size (icon_bar->priv->vadjustment);

        /* If auto-center is true, center the selected item */
        if (icon_bar->priv->auto_center == TRUE)
        {
            if (icon_bar->priv->active_item)
            {
                value = icon_bar->priv->active_item->index * icon_bar->priv->item_height;// - ((page_size-icon_bar->priv->item_height)/2);
            }
		

            if (value > (gtk_adjustment_get_upper (icon_bar->priv->vadjustment)-page_size))
                value = (gtk_adjustment_get_upper (icon_bar->priv->vadjustment)-page_size);

            gtk_adjustment_set_value (icon_bar->priv->vadjustment, value);
            rstto_icon_bar_adjustment_changed (icon_bar, icon_bar->priv->vadjustment);
            rstto_icon_bar_adjustment_changed (icon_bar, icon_bar->priv->hadjustment);
        }
        else
        {
            if (value > (gtk_adjustment_get_upper (icon_bar->priv->vadjustment)-page_size))
            {
                value = gtk_adjustment_get_upper (icon_bar->priv->vadjustment)-page_size;
            }
            gtk_adjustment_set_value (icon_bar->priv->vadjustment, value);
            rstto_icon_bar_adjustment_changed (icon_bar, icon_bar->priv->vadjustment);
            rstto_icon_bar_adjustment_changed (icon_bar, icon_bar->priv->hadjustment);
        }
    }
    else
    {
        icon_bar->priv->height = allocation->height;
        icon_bar->priv->item_height = icon_bar->priv->height;
        icon_bar->priv->vadjustment->value = 0;

        page_size = gtk_adjustment_get_page_size (icon_bar->priv->hadjustment);

        /* If auto-center is true, center the selected item */
        if (icon_bar->priv->auto_center == TRUE)
        {
            value = icon_bar->priv->active_item->index * icon_bar->priv->item_width - ((page_size-icon_bar->priv->item_width)/2);

            if (value > (gtk_adjustment_get_upper (icon_bar->priv->hadjustment)-page_size))
                value = (gtk_adjustment_get_upper (icon_bar->priv->hadjustment)-page_size);

            gtk_adjustment_set_value (icon_bar->priv->hadjustment, value);
            rstto_icon_bar_adjustment_changed (icon_bar, icon_bar->priv->vadjustment);
            rstto_icon_bar_adjustment_changed (icon_bar, icon_bar->priv->hadjustment);
        }
        else
        {
            if (value > (gtk_adjustment_get_upper (icon_bar->priv->hadjustment)-page_size))
            {
                value = gtk_adjustment_get_upper (icon_bar->priv->hadjustment)-page_size;
            }
            gtk_adjustment_set_value (icon_bar->priv->hadjustment, value);
            rstto_icon_bar_adjustment_changed (icon_bar, icon_bar->priv->vadjustment);
            rstto_icon_bar_adjustment_changed (icon_bar, icon_bar->priv->hadjustment);
        }
    }
}



static gboolean
rstto_icon_bar_expose (
        GtkWidget      *widget,
        GdkEventExpose *expose)
{
    RsttoIconBarItem *item;
    GdkRectangle    area;
    RsttoIconBar     *icon_bar = RSTTO_ICON_BAR (widget);
    GList          *lp;
    RsttoFile      *file;
    GtkTreeIter     iter;

    if (expose->window != icon_bar->priv->bin_window)
        return FALSE;

    for (lp = icon_bar->priv->items; lp != NULL; lp = lp->next)
    {
        item = lp->data;

        if (icon_bar->priv->orientation == GTK_ORIENTATION_VERTICAL)
        {
            area.x = 0;
            area.y = item->index * icon_bar->priv->item_height;
        }
        else
        {
            area.x = item->index * icon_bar->priv->item_width;
            area.y = 0;
        }

        area.width = icon_bar->priv->item_width;
        area.height = icon_bar->priv->item_height;


        if (gdk_region_rect_in (expose->region, &area) != GDK_OVERLAP_RECTANGLE_OUT)
        {
            rstto_icon_bar_paint_item (icon_bar, item, &expose->area);
        }
        else
        {
            iter = item->iter;
            gtk_tree_model_get (icon_bar->priv->model, &iter,
                    icon_bar->priv->file_column, &file,
                    -1);
            rstto_thumbnailer_dequeue_file (icon_bar->priv->thumbnailer, file);
            g_object_unref (file);
        }
    }

    return TRUE;
}



static gboolean
rstto_icon_bar_leave (
        GtkWidget        *widget,
        GdkEventCrossing *event)
{
    RsttoIconBar *icon_bar = RSTTO_ICON_BAR (widget);

    if (icon_bar->priv->cursor_item != NULL)
    {
        rstto_icon_bar_queue_draw_item (icon_bar, icon_bar->priv->cursor_item);
        icon_bar->priv->cursor_item = NULL;
    }

    return FALSE;
}



static gboolean
rstto_icon_bar_motion (
        GtkWidget      *widget,
        GdkEventMotion *event)
{
    RsttoIconBarItem *item;
    RsttoIconBar     *icon_bar = RSTTO_ICON_BAR (widget);
    GtkTreeIter       iter;
    RsttoFile        *file;

    item = rstto_icon_bar_get_item_at_pos (icon_bar, event->x, event->y);
    if (item != NULL && icon_bar->priv->cursor_item != item)
    {
        if (icon_bar->priv->cursor_item != NULL)
            rstto_icon_bar_queue_draw_item (icon_bar, icon_bar->priv->cursor_item);
        icon_bar->priv->cursor_item = item;
        rstto_icon_bar_queue_draw_item (icon_bar, item);

        iter = item->iter;
        gtk_tree_model_get (icon_bar->priv->model, &iter,
                icon_bar->priv->file_column, &file,
                -1);
        g_object_unref (file);

        gtk_widget_trigger_tooltip_query (widget);
    }
    else if (icon_bar->priv->cursor_item != NULL
            && icon_bar->priv->cursor_item != item)
    {
        rstto_icon_bar_queue_draw_item (icon_bar, icon_bar->priv->cursor_item);
        icon_bar->priv->cursor_item = NULL;
    }

    return TRUE;
}

static gboolean
rstto_icon_bar_scroll (
        GtkWidget      *widget,
        GdkEventScroll *event)
{
    RsttoIconBar    *icon_bar   = RSTTO_ICON_BAR (widget);
    GtkAdjustment *adjustment = NULL;
    gdouble        val        = 0;
    gdouble        step_size  = 0;
    gdouble        max_value  = 0;

    if (icon_bar->priv->orientation == GTK_ORIENTATION_VERTICAL)
    {
        adjustment = icon_bar->priv->vadjustment;
        step_size = icon_bar->priv->item_height / 2;
    }
    else
    {
        adjustment = icon_bar->priv->hadjustment;
        step_size = icon_bar->priv->item_width / 2;
    }

    val = gtk_adjustment_get_value (adjustment);
    max_value = gtk_adjustment_get_upper (adjustment) - gtk_adjustment_get_page_size (adjustment);

    switch (event->direction)
    {
        case GDK_SCROLL_UP:
        case GDK_SCROLL_LEFT:
            val-=step_size;
            if (val<0) val = 0.0;
            break;
        case GDK_SCROLL_DOWN:
        case GDK_SCROLL_RIGHT:
            val+=step_size;
            if (val > max_value) val = max_value;
            break;
    }
    gtk_adjustment_set_value (adjustment, val);
    return TRUE;
}

static gboolean
rstto_icon_bar_button_press (
        GtkWidget      *widget,
        GdkEventButton *event)
{
    RsttoIconBar *icon_bar;
    RsttoIconBarItem *item;

    icon_bar = RSTTO_ICON_BAR (widget);

    if (G_UNLIKELY (!GTK_WIDGET_HAS_FOCUS (widget)))
        gtk_widget_grab_focus (widget);

    if (event->button == 1 && event->type == GDK_BUTTON_PRESS)
    {
        item = rstto_icon_bar_get_item_at_pos (icon_bar, event->x, event->y);
        icon_bar->priv->single_click_item = item;
    }
    return TRUE;
}

static gboolean
rstto_icon_bar_button_release (
        GtkWidget      *widget,
        GdkEventButton *event)
{
    RsttoIconBar *icon_bar;
    RsttoIconBarItem *item;

    icon_bar = RSTTO_ICON_BAR (widget);

    if (event->button == 1 && event->type == GDK_BUTTON_RELEASE)
    {
        item = rstto_icon_bar_get_item_at_pos (icon_bar, event->x, event->y);
        if (G_LIKELY (item != NULL && item != icon_bar->priv->active_item && item == icon_bar->priv->single_click_item))
            rstto_icon_bar_set_active (icon_bar, item->index);
    }
    return TRUE;
}


static void
rstto_icon_bar_set_adjustments (
        RsttoIconBar  *icon_bar,
        GtkAdjustment *hadj,
        GtkAdjustment *vadj)
{
    gboolean need_adjust = FALSE;

    if (hadj != NULL)
        g_return_if_fail (GTK_IS_ADJUSTMENT (hadj));
    else
        hadj = GTK_ADJUSTMENT (gtk_adjustment_new (0.0, 0.0, 0.0, 0.0, 0.0, 0.0));

    if (vadj != NULL)
        g_return_if_fail (GTK_IS_ADJUSTMENT (vadj));
    else
        vadj = GTK_ADJUSTMENT (gtk_adjustment_new (0.0, 0.0, 0.0, 0.0, 0.0, 0.0));

    if (icon_bar->priv->hadjustment && (icon_bar->priv->hadjustment != hadj))
    {
        g_signal_handlers_disconnect_matched (icon_bar->priv->hadjustment, G_SIGNAL_MATCH_DATA,
                0, 0, NULL, NULL, icon_bar);
        g_object_unref (icon_bar->priv->hadjustment);
    }

    if (icon_bar->priv->vadjustment && (icon_bar->priv->vadjustment != vadj))
    {
        g_signal_handlers_disconnect_matched (icon_bar->priv->vadjustment, G_SIGNAL_MATCH_DATA,
                0, 0, NULL, NULL, icon_bar);
        g_object_unref (icon_bar->priv->vadjustment);
    }

    if (icon_bar->priv->hadjustment != hadj)
    {
        icon_bar->priv->hadjustment = hadj;
        g_object_ref (icon_bar->priv->hadjustment);
        gtk_object_sink (GTK_OBJECT (icon_bar->priv->hadjustment));

        g_signal_connect (icon_bar->priv->hadjustment, "value_changed",
                G_CALLBACK (cb_rstto_icon_bar_adjustment_value_changed), icon_bar);
        need_adjust = TRUE;
    }

    if (icon_bar->priv->vadjustment != vadj)
    {
        icon_bar->priv->vadjustment = vadj;
        g_object_ref (icon_bar->priv->vadjustment);
        gtk_object_sink (GTK_OBJECT (icon_bar->priv->vadjustment));

        g_signal_connect (icon_bar->priv->vadjustment, "value_changed",
                G_CALLBACK (cb_rstto_icon_bar_adjustment_value_changed), icon_bar);
        need_adjust = TRUE;
    }

    if (need_adjust)
        cb_rstto_icon_bar_adjustment_value_changed (NULL, icon_bar);
}



static void
cb_rstto_icon_bar_adjustment_value_changed (
        GtkAdjustment *adjustment,
        RsttoIconBar  *icon_bar)
{
    if (GTK_WIDGET_REALIZED (icon_bar))
    {
        /* Set auto_center to false, this should be the default behaviour
         * in case a user changes the value of the adjustments.
         *
         * If the value is set internally, and auto-center was enabled
         * the function calling gtk_adjustment_value_changed should set
         * auto_center to TRUE afterwards.
         */
        icon_bar->priv->auto_center = FALSE;

        gdk_window_move (icon_bar->priv->bin_window,
                - icon_bar->priv->hadjustment->value,
                - icon_bar->priv->vadjustment->value);

        gdk_window_process_updates (icon_bar->priv->bin_window, TRUE);
    }
}

static void
rstto_icon_bar_invalidate (RsttoIconBar *icon_bar)
{
    g_list_foreach (icon_bar->priv->items, (GFunc) rstto_icon_bar_item_invalidate, NULL);

    gtk_widget_queue_resize (GTK_WIDGET (icon_bar));
}

static RsttoIconBarItem*
rstto_icon_bar_get_item_at_pos (
        RsttoIconBar *icon_bar,
        gint          x,
        gint          y)
{
    GList *lp;

    if (G_UNLIKELY (icon_bar->priv->item_height == 0))
        return NULL;

    if (icon_bar->priv->orientation == GTK_ORIENTATION_VERTICAL)
        lp = g_list_nth (icon_bar->priv->items, y / icon_bar->priv->item_height);
    else
        lp = g_list_nth (icon_bar->priv->items, x / icon_bar->priv->item_width);

    return (lp != NULL) ? lp->data : NULL;
}



static void
rstto_icon_bar_queue_draw_item (
        RsttoIconBar     *icon_bar,
        RsttoIconBarItem *item)
{
    GdkRectangle area;

    if (GTK_WIDGET_REALIZED (icon_bar))
    {
        if (icon_bar->priv->orientation == GTK_ORIENTATION_VERTICAL)
        {
            area.x = 0;
            area.y = icon_bar->priv->item_height * item->index;
        }
        else
        {
            area.x = icon_bar->priv->item_width * item->index;
            area.y = 0;
        }

        area.width = icon_bar->priv->item_width;
        area.height = icon_bar->priv->item_height;

        gdk_window_invalidate_rect (icon_bar->priv->bin_window, &area, TRUE);
    }
}



static void
rstto_icon_bar_paint_item (
        RsttoIconBar     *icon_bar,
        RsttoIconBarItem *item,
        GdkRectangle     *area)
{
    const GdkPixbuf *pixbuf = NULL;
    GdkColor        *border_color;
    GdkColor        *fill_color;
    GdkGC           *gc;
    gint             focus_width;
    gint             focus_pad;
    gint             x, y;
    gint             px, py;
    gint             pixbuf_height, pixbuf_width;
    RsttoFile       *file;
    GtkTreeIter      iter;

    if (!RSTTO_ICON_BAR_VALID_MODEL_AND_COLUMNS (icon_bar))
        return;

    gtk_widget_style_get (GTK_WIDGET (icon_bar),
            "focus-line-width", &focus_width,
            "focus-padding", &focus_pad,
            NULL);

    iter = item->iter;
    gtk_tree_model_get (icon_bar->priv->model, &iter,
            icon_bar->priv->file_column, &file,
            -1);
    
    pixbuf = rstto_file_get_thumbnail (file, icon_bar->priv->thumbnail_size);

    g_object_unref (file);

    if (NULL == pixbuf)
    {
        pixbuf = thumbnail_missing;
    }

    if (pixbuf)
    {
        pixbuf_width = gdk_pixbuf_get_width (pixbuf);
        pixbuf_height = gdk_pixbuf_get_height (pixbuf);
    }

    /* calculate pixbuf/layout location */
    if (icon_bar->priv->orientation == GTK_ORIENTATION_VERTICAL)
    {
        x = 0;
        y = icon_bar->priv->item_height * item->index;

        px = (icon_bar->priv->item_width - pixbuf_width) / 2;
        py = (icon_bar->priv->item_height - pixbuf_height) / 2 + icon_bar->priv->item_height * item->index;
    }
    else
    {
        x = icon_bar->priv->item_width * item->index;
        y = 0;

        px = (icon_bar->priv->item_width - pixbuf_width) / 2 + icon_bar->priv->item_width * item->index;
        py = (icon_bar->priv->item_height - pixbuf_height) / 2;
    }

    if (icon_bar->priv->active_item == item)
    {
        gtk_widget_style_get (GTK_WIDGET (icon_bar),
                "active-item-fill-color", &fill_color,
                "active-item-border-color", &border_color,
                NULL);

        if (fill_color == NULL)
        {
            fill_color = gdk_color_copy (&(gtk_widget_get_style (GTK_WIDGET (icon_bar))->base[GTK_STATE_SELECTED]));
            gdk_color_parse ("#c1d2ee", fill_color);
        }

        if (border_color == NULL)
        {
            border_color = gdk_color_copy (&(gtk_widget_get_style (GTK_WIDGET (icon_bar))->base[GTK_STATE_SELECTED]));
            gdk_color_parse ("#316ac5", border_color);
        }

        gc = gdk_gc_new (icon_bar->priv->bin_window);
        gdk_gc_set_clip_rectangle (gc, area);
        gdk_gc_set_rgb_fg_color (gc, fill_color);
        gdk_draw_rectangle (icon_bar->priv->bin_window, gc, TRUE,
                x + focus_pad + focus_width,
                y + focus_pad + focus_width,
                icon_bar->priv->item_width - 2 * (focus_width + focus_pad),
                icon_bar->priv->item_height - 2 * (focus_width + focus_pad));
        gdk_gc_set_rgb_fg_color (gc, border_color);
        gdk_gc_set_line_attributes (gc, focus_width, GDK_LINE_SOLID, GDK_CAP_BUTT, GDK_JOIN_MITER);
        gdk_draw_rectangle (icon_bar->priv->bin_window, gc, FALSE,
                x + focus_pad + focus_width / 2,
                y + focus_pad + focus_width / 2,
                icon_bar->priv->item_width - (2 * focus_pad + focus_width),
                icon_bar->priv->item_height - (2 * focus_pad + focus_width));
        gdk_color_free (border_color);
        gdk_color_free (fill_color);
        g_object_unref (gc);
    }
    else if (icon_bar->priv->cursor_item == item)
    {
        gtk_widget_style_get (GTK_WIDGET (icon_bar),
                "cursor-item-fill-color", &fill_color,
                "cursor-item-border-color", &border_color,
                NULL);

        if (fill_color == NULL)
        {
            fill_color = gdk_color_copy (&(gtk_widget_get_style (GTK_WIDGET (icon_bar))->base[GTK_STATE_SELECTED]));
            gdk_color_parse ("#e0e8f6", fill_color);
        }

        if (border_color == NULL)
        {
            border_color = gdk_color_copy (&(gtk_widget_get_style (GTK_WIDGET (icon_bar))->base[GTK_STATE_SELECTED]));
            gdk_color_parse ("#98b4e2", border_color);
        }

        gc = gdk_gc_new (icon_bar->priv->bin_window);
        gdk_gc_set_clip_rectangle (gc, area);
        gdk_gc_set_rgb_fg_color (gc, fill_color);
        gdk_draw_rectangle (icon_bar->priv->bin_window, gc, TRUE,
                x + focus_pad + focus_width,
                y + focus_pad + focus_width,
                icon_bar->priv->item_width - 2 * (focus_width + focus_pad),
                icon_bar->priv->item_height - 2 * (focus_width + focus_pad));
        gdk_gc_set_rgb_fg_color (gc, border_color);
        gdk_gc_set_line_attributes (gc, focus_width, GDK_LINE_SOLID, GDK_CAP_BUTT, GDK_JOIN_MITER);
        gdk_draw_rectangle (icon_bar->priv->bin_window, gc, FALSE,
                x + focus_pad + focus_width / 2,
                y + focus_pad + focus_width / 2,
                icon_bar->priv->item_width - (2 * focus_pad + focus_width),
                icon_bar->priv->item_height - (2 * focus_pad + focus_width));
        gdk_color_free (border_color);
        gdk_color_free (fill_color);
        g_object_unref (gc);
    }


    if (NULL != pixbuf)
    {
        gdk_draw_pixbuf (icon_bar->priv->bin_window, NULL, pixbuf, 0, 0,
                px, py, 
                pixbuf_width, pixbuf_height,
                GDK_RGB_DITHER_NORMAL,
                pixbuf_width, pixbuf_height);
    }
}



static void
rstto_icon_bar_calculate_item_size (
        RsttoIconBar     *icon_bar,
        RsttoIconBarItem *item)
{
    gint       focus_width;
    gint       focus_pad;
    gint       int_pad;

    if (G_LIKELY (item->width != -1))
        return;

    gtk_widget_style_get (GTK_WIDGET (icon_bar),
            "focus-line-width", &focus_width,
            "focus-padding", &focus_pad,
            NULL);
    int_pad = focus_pad;

    switch (icon_bar->priv->thumbnail_size)
    {
        case THUMBNAIL_SIZE_VERY_SMALL:
            item->width = (2 * (int_pad + focus_width + focus_pad)) + THUMBNAIL_SIZE_VERY_SMALL_SIZE;
            break;
        case THUMBNAIL_SIZE_SMALLER:
            item->width = (2 * (int_pad + focus_width + focus_pad)) + THUMBNAIL_SIZE_SMALLER_SIZE;
            break;
        case THUMBNAIL_SIZE_SMALL:
            item->width = (2 * (int_pad + focus_width + focus_pad)) + THUMBNAIL_SIZE_SMALL_SIZE;
            break;
        case THUMBNAIL_SIZE_NORMAL:
            item->width = (2 * (int_pad + focus_width + focus_pad)) + THUMBNAIL_SIZE_NORMAL_SIZE;
            break;
        case THUMBNAIL_SIZE_LARGE:
            item->width = (2 * (int_pad + focus_width + focus_pad)) + THUMBNAIL_SIZE_LARGE_SIZE;
            break;
        case THUMBNAIL_SIZE_LARGER:
            item->width = (2 * (int_pad + focus_width + focus_pad)) + THUMBNAIL_SIZE_LARGER_SIZE;
            break;
        case THUMBNAIL_SIZE_VERY_LARGE:
            item->width = (2 * (int_pad + focus_width + focus_pad)) + THUMBNAIL_SIZE_VERY_LARGE_SIZE;
            break;
        default:
            break;
    }

    item->height = item->width;
}

static RsttoIconBarItem *
rstto_icon_bar_item_new (void)
{
    RsttoIconBarItem *item;

    item = g_slice_new0 (RsttoIconBarItem);
    item->width = -1;
    item->height = -1;

    return item;
}



static void
rstto_icon_bar_item_free (RsttoIconBarItem *item)
{
    g_slice_free (RsttoIconBarItem, item);
}



static void
rstto_icon_bar_item_invalidate (RsttoIconBarItem *item)
{
    item->width = -1;
    item->height = -1;
}



static void
rstto_icon_bar_build_items (RsttoIconBar *icon_bar)
{
    RsttoIconBarItem *item;
    GtkTreeIter     iter;
    GList          *items = NULL;
    gint            i = 0;

    if (!gtk_tree_model_get_iter_first (icon_bar->priv->model, &iter))
        return;

    do
    {
        item = rstto_icon_bar_item_new ();
        item->iter = iter;
        item->index = i++;

        items = g_list_prepend (items, item);
    }
    while (gtk_tree_model_iter_next (icon_bar->priv->model, &iter));

    icon_bar->priv->items = g_list_reverse (items);
}



static void
rstto_icon_bar_row_changed (
        GtkTreeModel *model,
        GtkTreePath  *path,
        GtkTreeIter  *iter,
        RsttoIconBar *icon_bar)
{
    RsttoIconBarItem  *item;
    gint             idx;

    idx = gtk_tree_path_get_indices (path)[0];
    item = g_list_nth (icon_bar->priv->items, idx)->data;
    rstto_icon_bar_item_invalidate (item);
    gtk_widget_queue_resize (GTK_WIDGET (icon_bar));
}



static void
rstto_icon_bar_row_inserted (
        GtkTreeModel *model,
        GtkTreePath  *path,
        GtkTreeIter  *iter,
        RsttoIconBar *icon_bar)
{
    RsttoIconBarItem  *item;
    GList           *lp;
    gint             idx;

    idx = gtk_tree_path_get_indices (path)[0];
    item = rstto_icon_bar_item_new ();

    if ((gtk_tree_model_get_flags (icon_bar->priv->model) & GTK_TREE_MODEL_ITERS_PERSIST) != 0)
        item->iter = *iter;
    item->index = idx;

    icon_bar->priv->items = g_list_insert (icon_bar->priv->items, item, idx);

    for (lp = g_list_nth (icon_bar->priv->items, idx + 1); lp != NULL; lp = lp->next)
    {
        item = lp->data;
        item->index++;
    }

    gtk_widget_queue_resize (GTK_WIDGET (icon_bar));
}



static void
rstto_icon_bar_row_deleted (
        GtkTreeModel *model,
        GtkTreePath  *path,
        RsttoIconBar *icon_bar)
{
    RsttoIconBarItem *item;
    gboolean        active = FALSE;
    GList          *lnext;
    GList          *lp;
    gint            idx;

    g_return_if_fail (RSTTO_IS_ICON_BAR (icon_bar));

    idx = gtk_tree_path_get_indices (path)[0];
    lp = g_list_nth (icon_bar->priv->items, idx);
    item = lp->data;

    if (item == icon_bar->priv->active_item)
    {
        icon_bar->priv->active_item = NULL;
        active = TRUE;
    }

    if (item == icon_bar->priv->cursor_item)
        icon_bar->priv->cursor_item = NULL;

    rstto_icon_bar_item_free (item);

    for (lnext = lp->next; lnext != NULL; lnext = lnext->next)
    {
        item = lnext->data;
        item->index--;
    }

    icon_bar->priv->items = g_list_delete_link (icon_bar->priv->items, lp);

    if (active && icon_bar->priv->items != NULL)
        icon_bar->priv->active_item = icon_bar->priv->items->data;

    gtk_widget_queue_resize (GTK_WIDGET (icon_bar));

    if (active)
        rstto_icon_bar_set_active (icon_bar, -1);
}


static void
rstto_icon_bar_rows_reordered (
        GtkTreeModel *model,
        GtkTreePath  *path,
        GtkTreeIter  *iter,
        gint         *new_order,
        RsttoIconBar *icon_bar)
{
    RsttoIconBarItem **item_array;
    GList           *items = NULL;
    GList           *lp;
    gint            *inverted_order;
    gint             length;
    gint             i;

    length = gtk_tree_model_iter_n_children (model, NULL);
    inverted_order = g_newa (gint, length);

    /* invert the array */
    for (i = 0; i < length; ++i)
        inverted_order[new_order[i]] = i;

    item_array = g_newa (RsttoIconBarItem *, length);
    for (i = 0, lp = icon_bar->priv->items; lp != NULL; ++i, lp = lp->next)
        item_array[inverted_order[i]] = lp->data;

    for (i = 0; i < length; ++i)
    {
        item_array[i]->index = i;
        items = g_list_append (items, item_array[i]);
    }

    g_list_free (icon_bar->priv->items);
    icon_bar->priv->items = g_list_reverse (items);
    if (icon_bar->priv->auto_center)
    {
        rstto_icon_bar_show_active (icon_bar);
        icon_bar->priv->auto_center = TRUE;
    }

    gtk_widget_queue_draw (GTK_WIDGET (icon_bar));
}



/**
 * rstto_icon_bar_new:
 *
 * Creates a new #RsttoIconBar without model.
 *
 * Returns: a newly allocated #RsttoIconBar.
 **/
GtkWidget *
rstto_icon_bar_new (void)
{
    return g_object_new (RSTTO_TYPE_ICON_BAR, NULL);
}



/**
 * rstto_icon_bar_new_with_model:
 * @model : A #GtkTreeModel.
 *
 * Creates a new #RsttoIconBar and associates it with
 * @model.
 *
 * Returns: a newly allocated #RsttoIconBar, which is associated with @model.
 **/
GtkWidget *
rstto_icon_bar_new_with_model (GtkTreeModel *model)
{
    g_return_val_if_fail (GTK_IS_TREE_MODEL (model), NULL);

    return g_object_new (RSTTO_TYPE_ICON_BAR,
            "model", model,
            NULL);
}



/**
 * rstto_icon_bar_get_model:
 * @icon_bar  : A #RsttoIconBar.
 *
 * Returns the model the #RsttoIconBar is based on. Returns %NULL if
 * the model is unset.
 *
 * Returns: A #GtkTreeModel, or %NULL if none is currently being used.
 **/
GtkTreeModel*
rstto_icon_bar_get_model (RsttoIconBar *icon_bar)
{
    g_return_val_if_fail (RSTTO_IS_ICON_BAR (icon_bar), NULL);
    return icon_bar->priv->model;
}



/**
 * rstto_icon_bar_set_model:
 * @icon_bar  : A #RsttoIconBar.
 * @model     : A #GtkTreeModel or %NULL.
 *
 * Sets the model for a #RsttoIconBar. If the @icon_bar already has a model
 * set, it will remove it before settings the new model. If @model is %NULL,
 * then it will unset the old model.
 **/
void
rstto_icon_bar_set_model (
        RsttoIconBar *icon_bar,
        GtkTreeModel *model)
{
    GType file_column_type;
    gint  active = -1;

    g_return_if_fail (RSTTO_IS_ICON_BAR (icon_bar));
    g_return_if_fail (GTK_IS_TREE_MODEL (model) || model == NULL);

    if (G_UNLIKELY (model == icon_bar->priv->model))
        return;

    if (model != NULL)
    {
        g_return_if_fail (gtk_tree_model_get_flags (model) & GTK_TREE_MODEL_LIST_ONLY);

        if (icon_bar->priv->file_column != -1)
        {
            file_column_type = gtk_tree_model_get_column_type (model, icon_bar->priv->file_column);
            g_return_if_fail (file_column_type == RSTTO_TYPE_FILE);
        }
    }

    if (icon_bar->priv->model)
    {
        g_signal_handlers_disconnect_by_func (icon_bar->priv->model,
                rstto_icon_bar_row_changed,
                icon_bar);
        g_signal_handlers_disconnect_by_func (icon_bar->priv->model,
                rstto_icon_bar_row_inserted,
                icon_bar);
        g_signal_handlers_disconnect_by_func (icon_bar->priv->model,
                rstto_icon_bar_row_deleted,
                icon_bar);
        g_signal_handlers_disconnect_by_func (icon_bar->priv->model,
                rstto_icon_bar_rows_reordered,
                icon_bar);

        g_object_unref (G_OBJECT (icon_bar->priv->model));

        g_list_foreach (icon_bar->priv->items, (GFunc) rstto_icon_bar_item_free, NULL);
        g_list_free (icon_bar->priv->items);
        icon_bar->priv->active_item = NULL;
        icon_bar->priv->cursor_item = NULL;
        icon_bar->priv->items = NULL;
    }

    icon_bar->priv->model = model;

    if (model != NULL)
    {
        g_object_ref (G_OBJECT (model));

        g_signal_connect (G_OBJECT (model), "row-changed",
                G_CALLBACK (rstto_icon_bar_row_changed), icon_bar);
        g_signal_connect (G_OBJECT (model), "row-inserted",
                G_CALLBACK (rstto_icon_bar_row_inserted), icon_bar);
        g_signal_connect (G_OBJECT (model), "row-deleted",
                G_CALLBACK (rstto_icon_bar_row_deleted), icon_bar);
        g_signal_connect (G_OBJECT (model), "rows-reordered",
                G_CALLBACK (rstto_icon_bar_rows_reordered), icon_bar);

        rstto_icon_bar_build_items (icon_bar);

        if (icon_bar->priv->items != NULL)
            active = ((RsttoIconBarItem *) icon_bar->priv->items->data)->index;
    }

    rstto_icon_bar_invalidate (icon_bar);

    g_object_notify (G_OBJECT (icon_bar), "model");

    rstto_icon_bar_set_active (icon_bar, active);
}


/**
 * rstto_icon_bar_get_file_column:
 * @icon_bar  : An #RsttoIconBar.
 *
 * Returns the column with file for @icon_bar.
 *
 * Returns: the file column, or -1 if it's unset.
 **/
gint
rstto_icon_bar_get_file_column (RsttoIconBar *icon_bar)
{
    g_return_val_if_fail (RSTTO_IS_ICON_BAR (icon_bar), -1);
    return icon_bar->priv->file_column;
}



/**
 * rstto_icon_bar_set_file_column:
 * @icon_bar  : An #RsttoIconBar.
 * @column    : A column in the currently used model or -1 to
 *              use no text in @icon_bar.
 *
 * Sets the column with text for @icon_bar to be @column. The
 * text column must be of type #G_TYPE_STRING.
 **/
void
rstto_icon_bar_set_file_column (
        RsttoIconBar *icon_bar,
        gint          column)
{
    GType file_column_type;

    g_return_if_fail (RSTTO_IS_ICON_BAR (icon_bar));

    if (column == icon_bar->priv->file_column)
        return;

    if (column == -1)
    {
        icon_bar->priv->file_column = -1;
    }
    else
    {
        if (icon_bar->priv->model != NULL)
        {
            file_column_type = gtk_tree_model_get_column_type (icon_bar->priv->model, column);
            g_return_if_fail (file_column_type == RSTTO_TYPE_FILE);
        }

        icon_bar->priv->file_column = column;
    }

    rstto_icon_bar_invalidate (icon_bar);

    g_object_notify (G_OBJECT (icon_bar), "file-column");
}



/**
 * rstto_icon_bar_get_orientation:
 * @icon_bar  : An #RsttoIconBar.
 *
 * Retrieves the current orientation of the toolbar. See
 * rstto_icon_bar_set_orientation().
 *
 * Returns: The orientation of @icon_bar.
 **/
GtkOrientation
rstto_icon_bar_get_orientation (RsttoIconBar *icon_bar)
{
    g_return_val_if_fail (RSTTO_IS_ICON_BAR (icon_bar), GTK_ORIENTATION_VERTICAL);
    return icon_bar->priv->orientation;
}



/**
 * rstto_icon_bar_set_orientation:
 * @icon_bar    : An #RsttoIconBar.
 * @orientation : A new #GtkOrientation.
 *
 * Sets whether the @icon_bar should appear horizontally
 * or vertically.
 **/
void
rstto_icon_bar_set_orientation (
        RsttoIconBar    *icon_bar,
        GtkOrientation   orientation)
{
    g_return_if_fail (RSTTO_IS_ICON_BAR (icon_bar));

    if (icon_bar->priv->orientation != orientation)
    {
        /* Unset the cursor-item */
        icon_bar->priv->cursor_item = NULL;

        icon_bar->priv->orientation = orientation;
        gtk_widget_queue_resize (GTK_WIDGET (icon_bar));

        /* If the orientation changes, focus the active item */
        rstto_icon_bar_show_active (icon_bar);

        g_object_notify (G_OBJECT (icon_bar), "orientation");
    }
}



/**
 * rstto_icon_bar_get_active:
 * @icon_bar  : An #RsttoIconBar.
 *
 * Returns the index of the currently active item, or -1 if there's no
 * active item.
 *
 * Returns: An integer which is the index of the currently active item,
 *          or -1 if there's no active item.
 **/
gint
rstto_icon_bar_get_active (RsttoIconBar *icon_bar)
{
    g_return_val_if_fail (RSTTO_IS_ICON_BAR (icon_bar), -1);

    return (icon_bar->priv->active_item != NULL)
        ? icon_bar->priv->active_item->index
        : -1;
}



/**
 * rstto_icon_bar_set_active:
 * @icon_bar  : An #RsttoIconBar.
 * @idx       : An index in the model passed during construction,
 *              or -1 to have no active item.
 *
 * Sets the active item of @icon_bar to be the item at @idx.
 **/
void
rstto_icon_bar_set_active (
        RsttoIconBar *icon_bar,
        gint          idx)
{
    g_return_if_fail (RSTTO_IS_ICON_BAR (icon_bar));
    g_return_if_fail (idx == -1 || g_list_nth (icon_bar->priv->items, idx) != NULL);

    if ((icon_bar->priv->active_item == NULL && idx == -1)
            || (icon_bar->priv->active_item != NULL && idx == icon_bar->priv->active_item->index))
        return;

    if (G_UNLIKELY (idx >= 0))
        icon_bar->priv->active_item = g_list_nth (icon_bar->priv->items, idx)->data;
    else
        icon_bar->priv->active_item = NULL;

    g_signal_emit (G_OBJECT (icon_bar), icon_bar_signals[SELECTION_CHANGED], 0);
    g_object_notify (G_OBJECT (icon_bar), "active");
    gtk_widget_queue_draw (GTK_WIDGET (icon_bar));
}



/**
 * rstto_icon_bar_get_active_iter:
 * @icon_bar  : An #RsttoIconBar.
 * @iter      : An uninitialized #GtkTreeIter.
 *
 * Sets @iter to point to the current active item, if it exists.
 *
 * Returns: %TRUE if @iter was set.
 **/
gboolean
rstto_icon_bar_get_active_iter (
        RsttoIconBar  *icon_bar,
        GtkTreeIter   *iter)
{
    RsttoIconBarItem *item;
    GtkTreePath    *path;

    g_return_val_if_fail (RSTTO_IS_ICON_BAR (icon_bar), FALSE);
    g_return_val_if_fail (iter != NULL, FALSE);

    item = icon_bar->priv->active_item;
    if (item == NULL)
        return FALSE;

    if ((gtk_tree_model_get_flags (icon_bar->priv->model) & GTK_TREE_MODEL_ITERS_PERSIST) == 0)
    {
        path = gtk_tree_path_new_from_indices (item->index, -1);
        gtk_tree_model_get_iter (icon_bar->priv->model, iter, path);
        gtk_tree_path_free (path);
    }
    else
    {
        *iter = item->iter;
    }

    return TRUE;
}



/**
 * rstto_icon_bar_set_active_iter:
 * @icon_bar  : An #RsttoIconBar.
 * @iter      : The #GtkTreeIter.
 *
 * Sets the current active item to be the one referenced by @iter. @iter
 * must correspond to a path of depth one.
 *
 * This can only be called if @icon_bar is associated with #GtkTreeModel.
 **/
void
rstto_icon_bar_set_active_iter (
        RsttoIconBar *icon_bar,
        GtkTreeIter  *iter)
{
    GtkTreePath *path;

    g_return_if_fail (RSTTO_IS_ICON_BAR (icon_bar));
    g_return_if_fail (icon_bar->priv->model != NULL);
    g_return_if_fail (iter != NULL);

    path = gtk_tree_model_get_path (icon_bar->priv->model, iter);
    if (G_LIKELY (path != NULL))
    {
        rstto_icon_bar_set_active (icon_bar, gtk_tree_path_get_indices (path)[0]);
        gtk_tree_path_free (path);
    }
}

gint
rstto_icon_bar_get_item_width (RsttoIconBar *icon_bar)
{
    return -1;
}

void
rstto_icon_bar_set_item_width (
        RsttoIconBar *icon_bar,
        gint item_width)
{
    pango_layout_set_width (icon_bar->priv->layout,
            item_width*PANGO_SCALE);
    return;
}

/**
 * rstto_icon_bar_set_show_text:
 * @icon_bar  : An #RsttoIconBar.
 * @show_text : TRUE if text should be visible
 *              or FALSE if text is hidden
 *
 * Toggles the visibility of the text-label
 **/
void
rstto_icon_bar_set_show_text (
        RsttoIconBar *icon_bar,
        gboolean show_text)
{
    g_return_if_fail (RSTTO_IS_ICON_BAR (icon_bar));
    icon_bar->priv->show_text = show_text;
}

/**
 * rstto_icon_bar_get_show_text:
 * @icon_bar  : An #RsttoIconBar.
 *
 * Returns: TRUE if text is visible.
 **/
gboolean
rstto_icon_bar_get_show_text (RsttoIconBar  *icon_bar)
{
    g_return_val_if_fail (RSTTO_IS_ICON_BAR (icon_bar), FALSE);
    return icon_bar->priv->show_text;
}

/**
 * rstto_icon_bar_show_active:
 * @icon_bar  : An #RsttoIconBar.
 *
 * Returns: TRUE on success
 **/
gboolean
rstto_icon_bar_show_active (RsttoIconBar *icon_bar)
{
    gint page_size = 0;
    gint value = 0;

    g_return_val_if_fail (RSTTO_IS_ICON_BAR (icon_bar), FALSE);
    if (NULL == icon_bar->priv->active_item)
        return FALSE;

    icon_bar->priv->auto_center = TRUE;

    if (icon_bar->priv->orientation == GTK_ORIENTATION_VERTICAL)
    {
        page_size = gtk_adjustment_get_page_size (icon_bar->priv->vadjustment);
        value = icon_bar->priv->active_item->index * icon_bar->priv->item_height - ((page_size-icon_bar->priv->item_height)/2);

        if (value > (gtk_adjustment_get_upper (icon_bar->priv->vadjustment)-page_size))
            value = (gtk_adjustment_get_upper (icon_bar->priv->vadjustment)-page_size);

        gtk_adjustment_set_value (icon_bar->priv->vadjustment, value);
        rstto_icon_bar_adjustment_value_changed (
                icon_bar,
                icon_bar->priv->vadjustment);
        return TRUE;
    }
    else
    {
        page_size = gtk_adjustment_get_page_size (icon_bar->priv->hadjustment);
        value = icon_bar->priv->active_item->index * icon_bar->priv->item_width - ((page_size-icon_bar->priv->item_width)/2);

        if (value > (gtk_adjustment_get_upper (icon_bar->priv->hadjustment)-page_size))
            value = (gtk_adjustment_get_upper (icon_bar->priv->hadjustment)-page_size);

        gtk_adjustment_set_value (icon_bar->priv->hadjustment, value);
        rstto_icon_bar_adjustment_value_changed (
                icon_bar,
                icon_bar->priv->hadjustment);
        return TRUE;
    }
    return FALSE;
}

static void
rstto_icon_bar_adjustment_changed (
        RsttoIconBar  *icon_bar,
        GtkAdjustment *adjustment)
{
    if (TRUE == icon_bar->priv->auto_center)
    {
        gtk_adjustment_changed (adjustment);
        icon_bar->priv->auto_center = TRUE;
    }
    else
    {
        gtk_adjustment_changed (adjustment);
    }
}

static void
rstto_icon_bar_adjustment_value_changed (
        RsttoIconBar  *icon_bar,
        GtkAdjustment *adjustment)
{
    if (TRUE == icon_bar->priv->auto_center)
    {
        gtk_adjustment_value_changed (adjustment);
        icon_bar->priv->auto_center = TRUE;
    }
    else
    {
        gtk_adjustment_value_changed (adjustment);
    }
}

static void
cb_rstto_thumbnail_size_changed (
        GObject *settings,
        GParamSpec *pspec,
        gpointer user_data)
{
    GValue val_thumbnail_size = { 0, };
    RsttoIconBar *icon_bar = RSTTO_ICON_BAR (user_data);
    gboolean auto_center = icon_bar->priv->auto_center;

    g_value_init (&val_thumbnail_size, G_TYPE_UINT);

    g_object_get_property (
            settings,
            "thumbnail-size",
            &val_thumbnail_size);


    icon_bar->priv->thumbnail_size = g_value_get_uint (&val_thumbnail_size);

    rstto_icon_bar_invalidate (icon_bar);

    rstto_icon_bar_update_missing_icon (icon_bar);
    icon_bar->priv->auto_center = auto_center;
}


static void
rstto_icon_bar_update_missing_icon (RsttoIconBar *icon_bar)
{

    if (NULL != thumbnail_missing)
    {
        g_object_unref (thumbnail_missing);
    }

    switch (icon_bar->priv->thumbnail_size)
    {
        case THUMBNAIL_SIZE_VERY_SMALL:
            thumbnail_missing = gtk_icon_theme_load_icon (
                    gtk_icon_theme_get_default(),
                    "image-missing",
                    THUMBNAIL_SIZE_VERY_SMALL_SIZE,
                    0,
                    NULL);
            break;
        case THUMBNAIL_SIZE_SMALLER:
            thumbnail_missing = gtk_icon_theme_load_icon (
                    gtk_icon_theme_get_default(),
                    "image-missing",
                    THUMBNAIL_SIZE_SMALLER_SIZE,
                    0,
                    NULL);
            break;
        case THUMBNAIL_SIZE_SMALL:
            thumbnail_missing = gtk_icon_theme_load_icon (
                    gtk_icon_theme_get_default(),
                    "image-missing",
                    THUMBNAIL_SIZE_SMALL_SIZE,
                    0,
                    NULL);
            break;
        case THUMBNAIL_SIZE_NORMAL:
            thumbnail_missing = gtk_icon_theme_load_icon (
                    gtk_icon_theme_get_default(),
                    "image-missing",
                    THUMBNAIL_SIZE_NORMAL_SIZE,
                    0,
                    NULL);
            break;
        case THUMBNAIL_SIZE_LARGE:
            thumbnail_missing = gtk_icon_theme_load_icon (
                    gtk_icon_theme_get_default(),
                    "image-missing",
                    THUMBNAIL_SIZE_LARGE_SIZE,
                    0,
                    NULL);
            break;
        case THUMBNAIL_SIZE_LARGER:
            thumbnail_missing = gtk_icon_theme_load_icon (
                    gtk_icon_theme_get_default(),
                    "image-missing",
                    THUMBNAIL_SIZE_LARGER_SIZE,
                    0,
                    NULL);
            break;
        case THUMBNAIL_SIZE_VERY_LARGE:
            thumbnail_missing = gtk_icon_theme_load_icon (
                    gtk_icon_theme_get_default(),
                    "image-missing",
                    THUMBNAIL_SIZE_VERY_LARGE_SIZE,
                    0,
                    NULL);
            break;
        default:
            break;
    }

}
