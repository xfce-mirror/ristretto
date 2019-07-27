/*-
 * Copyright (c) 2004 os-cillation e.K.
 * Copyright (c) 2012 Stephan Arts <stephan@xfce.org>
 *
 * Written by Benedikt Meurer <benny@xfce.org>.
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

#ifndef __RSTTO_ICON_BAR_H__
#define __RSTTO_ICON_BAR_H__

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define RSTTO_TYPE_ICON_BAR             (rstto_icon_bar_get_type ())
#define RSTTO_ICON_BAR(obj)             (G_TYPE_CHECK_INSTANCE_CAST ((obj), RSTTO_TYPE_ICON_BAR, RsttoIconBar))
#define RSTTO_ICON_BAR_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST ((klass), RSTTO_TYPE_ICON_BAR, RsttoIconBarClass))
#define RSTTO_IS_ICON_BAR(obj)          (G_TYPE_CHECK_INSTANCE_TYPE ((obj), RSTTO_TYPE_ICON_BAR))
#define RSTTO_IS_ICON_BAR_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE ((obj), RSTTO_TYPE_ICON_BAR))
#define RSTTO_ICON_BAR_GET_CLASS(obj)   (G_TYPE_INSTANCE_GET_CLASS ((obj), RSTTO_TYPE_ICON_BAR, RsttoIconBarClass))

typedef struct _RsttoIconBarPrivate RsttoIconBarPrivate;
typedef struct _RsttoIconBarClass   RsttoIconBarClass;
typedef struct _RsttoIconBar        RsttoIconBar;

/**
 * RsttoIconBarClass:
 * @set_scroll_adjustments : Used internally to make the RsttoIconBar scrollable.
 * @selection_changed      : This signal is emitted whenever the currently selected icon changes.
 **/
struct _RsttoIconBarClass
{
  GtkContainerClass __parent__;

  /* signals */
#if 0
  void  (*set_scroll_adjustments)  (RsttoIconBar    *icon_bar,
                                    GtkAdjustment *hadjustment,
                                    GtkAdjustment *vadjustment);
#endif
  void  (*selection_changed)       (RsttoIconBar *icon_bar);

  /*< private >*/
  void  (*reserved1) (void);
  void  (*reserved2) (void);
  void  (*reserved3) (void);
  void  (*reserved4) (void);
};

/**
 * RsttoIconBar:
 *
 * The #RsttoIconBar struct contains only private fields and should not
 * be directly accessed.
 **/
struct _RsttoIconBar
{
  GtkContainer       __parent__;

  /*< private >*/
  RsttoIconBarPrivate *priv;
};

GType           rstto_icon_bar_get_type           (void) G_GNUC_CONST;

GtkWidget      *rstto_icon_bar_new                (GtkWidget      *s_window);
GtkWidget      *rstto_icon_bar_new_with_model     (GtkTreeModel   *model);

GtkTreeModel   *rstto_icon_bar_get_model          (RsttoIconBar     *icon_bar);
void            rstto_icon_bar_set_model          (RsttoIconBar     *icon_bar,
                                                 GtkTreeModel   *model);

gint            rstto_icon_bar_get_file_column    (RsttoIconBar     *icon_bar);
void            rstto_icon_bar_set_file_column    (RsttoIconBar     *icon_bar,
                                                 gint            column);

GtkOrientation  rstto_icon_bar_get_orientation    (RsttoIconBar     *icon_bar);
void            rstto_icon_bar_set_orientation    (RsttoIconBar     *icon_bar,
                                                 GtkOrientation  orientation);

gint            rstto_icon_bar_get_active         (RsttoIconBar     *icon_bar);
void            rstto_icon_bar_set_active         (RsttoIconBar     *icon_bar,
                                                 gint            idx);

gboolean        rstto_icon_bar_get_active_iter    (RsttoIconBar     *icon_bar,
                                                 GtkTreeIter    *iter);
void            rstto_icon_bar_set_active_iter    (RsttoIconBar     *icon_bar,
                                                 GtkTreeIter    *iter);

gint            rstto_icon_bar_get_item_width     (RsttoIconBar     *icon_bar);
void            rstto_icon_bar_set_item_width     (RsttoIconBar     *icon_bar,
                                                 gint            item_width);

gboolean        rstto_icon_bar_get_show_text      (RsttoIconBar     *icon_bar);
void            rstto_icon_bar_set_show_text      (RsttoIconBar     *icon_bar,
                                                 gboolean        show_text);

gboolean        rstto_icon_bar_show_active        (RsttoIconBar     *icon_bar);

G_END_DECLS

#endif /* !__RSTTO_ICON_BAR_H__ */

