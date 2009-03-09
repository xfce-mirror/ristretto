/*
 *  Copyright (C) Stephan Arts 2006-2009 <stephan@xfce.org>
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

#ifndef __RISTRETTO_PICTURE_VIEWER_H__
#define __RISTRETTO_PICTURE_VIEWER_H__

G_BEGIN_DECLS

#define RSTTO_TYPE_PICTURE_VIEWER rstto_picture_viewer_get_type()

#define RSTTO_PICTURE_VIEWER(obj)( \
        G_TYPE_CHECK_INSTANCE_CAST ((obj), \
                RSTTO_TYPE_PICTURE_VIEWER, \
                RsttoPictureViewer))

#define RSTTO_IS_PICTURE_VIEWER(obj)( \
        G_TYPE_CHECK_INSTANCE_TYPE ((obj), \
                RSTTO_TYPE_PICTURE_VIEWER))

#define RSTTO_PICTURE_VIEWER_CLASS(klass)( \
        G_TYPE_CHECK_CLASS_CAST ((klass), \
                RSTTO_TYPE_PICTURE_VIEWER, \
                RsttoPictureViewerClass))

#define RSTTO_IS_PICTURE_VIEWER_CLASS(klass)( \
        G_TYPE_CHECK_CLASS_TYPE ((klass), \
                RSTTO_TYPE_PICTURE_VIEWER()))

typedef struct _RsttoPictureViewerPriv RsttoPictureViewerPriv;

typedef struct _RsttoPictureViewer RsttoPictureViewer;

struct _RsttoPictureViewer
{
    GtkWidget         parent;
    RsttoPictureViewerPriv *priv;

    GtkAdjustment    *vadjustment;
    GtkAdjustment    *hadjustment;
};

typedef struct _RsttoPictureViewerClass RsttoPictureViewerClass;

struct _RsttoPictureViewerClass
{
    GtkWidgetClass  parent_class;

    gboolean (* set_scroll_adjustments) (RsttoPictureViewer *viewer,
          GtkAdjustment     *hadjustment,
          GtkAdjustment     *vadjustment);
};

GType      rstto_picture_viewer_get_type();

GtkWidget *rstto_picture_viewer_new ();
void       rstto_picture_viewer_set_image (RsttoPictureViewer *, RsttoImage *);
gdouble    rstto_picture_viewer_get_scale(RsttoPictureViewer *viewer);

void       rstto_picture_viewer_zoom_fit (RsttoPictureViewer *viewer);
void       rstto_picture_viewer_zoom_100 (RsttoPictureViewer *viewer);
void       rstto_picture_viewer_zoom_in  (RsttoPictureViewer *viewer, gdouble factor);
void       rstto_picture_viewer_zoom_out (RsttoPictureViewer *viewer, gdouble factor);

/**
gdouble    rstto_picture_viewer_fit_scale(RsttoPictureViewer *viewer);

void       rstto_picture_viewer_set_menu (RsttoPictureViewer *viewer, GtkMenu *menu);
void       rstto_picture_viewer_set_bg_color (RsttoPictureViewer *viewer, const GdkColor *color);
const GdkColor *
rstto_picture_viewer_get_bg_color (RsttoPictureViewer *viewer);

void       rstto_picture_viewer_redraw (RsttoPictureViewer *viewer);
*/

G_END_DECLS

#endif /* __RISTRETTO_PICTURE_VIEWER_H__ */
