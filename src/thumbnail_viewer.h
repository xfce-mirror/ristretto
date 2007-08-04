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

#ifndef __RISTRETTO_THUMBNAIL_VIEWER_H__
#define __RISTRETTO_THUMBNAIL_VIEWER_H__

G_BEGIN_DECLS

#define RSTTO_TYPE_THUMBNAIL_VIEWER rstto_thumbnail_viewer_get_type()

#define RSTTO_THUMBNAIL_VIEWER(obj)( \
        G_TYPE_CHECK_INSTANCE_CAST ((obj), \
				RSTTO_TYPE_THUMBNAIL_VIEWER, \
				RsttoThumbnailViewer))

#define RSTTO_IS_THUMBNAIL_VIEWER(obj)( \
        G_TYPE_CHECK_INSTANCE_TYPE ((obj), \
				RSTTO_TYPE_THUMBNAIL_VIEWER))

#define RSTTO_THUMBNAIL_VIEWER_CLASS(klass)( \
        G_TYPE_CHECK_CLASS_CAST ((klass), \
				RSTTO_TYPE_THUMBNAIL_VIEWER, \
				RsttoThumbnailViewerClass))

#define RSTTO_IS_THUMBNAIL_VIEWER_CLASS(klass)( \
        G_TYPE_CHECK_CLASS_TYPE ((klass), \
				RSTTO_TYPE_THUMBNAIL_VIEWER()))

typedef struct _RsttoThumbnailViewerPriv RsttoThumbnailViewerPriv;

typedef struct _RsttoThumbnailViewer RsttoThumbnailViewer;

struct _RsttoThumbnailViewer
{
	GtkWidget         parent;
    RsttoThumbnailViewerPriv *priv;
};

typedef struct _RsttoThumbnailViewerClass RsttoThumbnailViewerClass;

struct _RsttoThumbnailViewerClass
{
	GtkWidgetClass  parent_class;
};

GType      rstto_thumbnail_viewer_get_type();

GtkWidget *rstto_thumbnail_viewer_new();

G_END_DECLS

#endif /* __RISTRETTO_THUMBNAIL_VIEWER_H__ */
