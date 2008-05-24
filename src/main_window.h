/*
 *  Copyright (C) Stephan Arts 2006-2008 <stephan@xfce.org>
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

#ifndef __RISTRETTO_MAIN_WINDOW_H__
#define __RISTRETTO_MAIN_WINDOW_H__

G_BEGIN_DECLS

#define RSTTO_TYPE_MAIN_WINDOW rstto_main_window_get_type()

#define RSTTO_MAIN_WINDOW(obj)( \
        G_TYPE_CHECK_INSTANCE_CAST ((obj), \
                RSTTO_TYPE_MAIN_WINDOW, \
                RsttoMainWindow))

#define RSTTO_IS_MAIN_WINDOW(obj)( \
        G_TYPE_CHECK_INSTANCE_TYPE ((obj), \
                RSTTO_TYPE_MAIN_WINDOW))

#define RSTTO_MAIN_WINDOW_CLASS(klass)( \
        G_TYPE_CHECK_CLASS_CAST ((klass), \
                RSTTO_TYPE_MAIN_WINDOW, \
                RsttoMainWindowClass))

#define RSTTO_IS_MAIN_WINDOW_CLASS(klass)( \
        G_TYPE_CHECK_CLASS_TYPE ((klass), \
                RSTTO_TYPE_MAIN_WINDOW()))

typedef struct _RsttoMainWindowPriv RsttoMainWindowPriv;

typedef struct _RsttoMainWindow RsttoMainWindow;

struct _RsttoMainWindow
{
    GtkWindow         parent;
    RsttoMainWindowPriv *priv;
};

typedef struct _RsttoMainWindowClass RsttoMainWindowClass;

struct _RsttoMainWindowClass
{
    GtkWindowClass  parent_class;
};

typedef enum {
    RSTTO_DESKTOP_NONE,
    RSTTO_DESKTOP_XFCE
} RsttoDesktop;


GType      rstto_main_window_get_type();

GtkWidget *rstto_main_window_new();

void
rstto_main_window_set_thumbnail_viewer_orientation(RsttoMainWindow *window, GtkOrientation orientation);
void
rstto_main_window_set_show_thumbnail_viewer (RsttoMainWindow *window, gboolean visibility);
void
rstto_main_window_set_show_toolbar (RsttoMainWindow *window, gboolean visibility);
gboolean
rstto_main_window_get_show_toolbar (RsttoMainWindow *window);
gboolean
rstto_main_window_get_show_thumbnail_viewer (RsttoMainWindow *window);
GtkOrientation
rstto_main_window_get_thumbnail_viewer_orientation (RsttoMainWindow *window);
RsttoNavigator *
rstto_main_window_get_navigator (RsttoMainWindow *window);
GtkRecentManager *
rstto_main_window_get_recent_manager (RsttoMainWindow *window);


gdouble
rstto_main_window_get_slideshow_timeout (RsttoMainWindow *window);
gboolean
rstto_main_window_get_hide_thumbnails (RsttoMainWindow *window);
gint
rstto_main_window_get_max_cache_size (RsttoMainWindow *window);
void
rstto_main_window_set_slideshow_timeout (RsttoMainWindow *window, gdouble timeout);
void
rstto_main_window_set_hide_thumbnails (RsttoMainWindow *window, gboolean hide);
void
rstto_main_window_set_max_cache_size (RsttoMainWindow *window, gint max_cache_size);
void
rstto_main_window_set_pv_bg_color (RsttoMainWindow *window, const GdkColor *color);
const GdkColor *
rstto_main_window_get_pv_bg_color (RsttoMainWindow *window);

void
rstto_main_window_set_start_fullscreen (RsttoMainWindow *window, gboolean fullscreen);
void
rstto_main_window_set_start_slideshow (RsttoMainWindow *window, gboolean slideshow);
void
rstto_main_window_force_fullscreen (RsttoMainWindow *window);
void
rstto_main_window_force_slideshow (RsttoMainWindow *window);

void
rstto_main_window_set_scale_to_100 (RsttoMainWindow *window, gboolean scale_to_100);
gboolean
rstto_main_window_get_scale_to_100 (RsttoMainWindow *window);

GtkStatusbar *
rstto_main_window_get_statusbar(RsttoMainWindow *window);
gint
rstto_main_window_get_desktop(RsttoMainWindow *window);
gint
rstto_main_window_set_desktop(RsttoMainWindow *window, RsttoDesktop desktop);
gboolean
rstto_main_window_get_hide_thumbnail (RsttoMainWindow *window);
void
rstto_main_window_set_hide_thumbnail (RsttoMainWindow *window, gboolean hide);
void
rstto_main_window_set_start_fullscreen (RsttoMainWindow *window, gboolean fullscreen);

G_END_DECLS

#endif /* __RISTRETTO_MAIN_WINDOW_H__ */

