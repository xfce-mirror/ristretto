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

#ifndef __RISTRETTO_THUMBNAIL_BAR_H__
#define __RISTRETTO_THUMBNAIL_BAR_H__

G_BEGIN_DECLS

#define RSTTO_TYPE_THUMBNAIL_BAR rstto_thumbnail_bar_get_type()

#define RSTTO_THUMBNAIL_BAR(obj)( \
        G_TYPE_CHECK_INSTANCE_CAST ((obj), \
                RSTTO_TYPE_THUMBNAIL_BAR, \
                RsttoThumbnailBar))

#define RSTTO_IS_THUMBNAIL_BAR(obj)( \
        G_TYPE_CHECK_INSTANCE_TYPE ((obj), \
                RSTTO_TYPE_THUMBNAIL_BAR))

#define RSTTO_THUMBNAIL_BAR_CLASS(klass)( \
        G_TYPE_CHECK_CLASS_CAST ((klass), \
                RSTTO_TYPE_THUMBNAIL_BAR, \
                RsttoThumbnailBarClass))

#define RSTTO_IS_THUMBNAIL_BAR_CLASS(klass)( \
        G_TYPE_CHECK_CLASS_TYPE ((klass), \
                RSTTO_TYPE_THUMBNAIL_BAR()))

typedef struct _RsttoThumbnailBarPriv RsttoThumbnailBarPriv;

typedef struct _RsttoThumbnailBar RsttoThumbnailBar;

struct _RsttoThumbnailBar
{
    GtkContainer           parent;
    RsttoThumbnailBarPriv *priv;
    gint film_border_width;
};

typedef struct _RsttoThumbnailBarClass RsttoThumbnailBarClass;

struct _RsttoThumbnailBarClass
{
    GtkContainerClass  parent_class;
};

GType      rstto_thumbnail_bar_get_type();

GtkWidget *rstto_thumbnail_bar_new();

void       rstto_thumbnail_bar_set_orientation (RsttoThumbnailBar *, GtkOrientation);
GtkOrientation  rstto_thumbnail_bar_get_orientation (RsttoThumbnailBar *);

void rstto_thumbnail_bar_set_image_list (RsttoThumbnailBar *bar, RsttoImageList *nav);
void rstto_thumbnail_bar_set_iter (RsttoThumbnailBar *bar, RsttoImageListIter *iter);

G_END_DECLS

#endif /* __RISTRETTO_THUMBNAIL_BAR_H__ */
