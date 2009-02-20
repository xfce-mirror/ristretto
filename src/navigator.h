/*
 *  Copyright (c) 2009 Stephan Arts <stephan@xfce.org>
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

#ifndef __RISTRETTO_NAVIGATOR_H__
#define __RISTRETTO_NAVIGATOR_H__

G_BEGIN_DECLS

#define RSTTO_TYPE_NAVIGATOR rstto_navigator_get_type()

#define RSTTO_NAVIGATOR(obj)( \
        G_TYPE_CHECK_INSTANCE_CAST ((obj), \
                RSTTO_TYPE_NAVIGATOR, \
                RsttoNavigator))

#define RSTTO_IS_NAVIGATOR(obj)( \
        G_TYPE_CHECK_INSTANCE_TYPE ((obj), \
                RSTTO_TYPE_NAVIGATOR))

#define RSTTO_NAVIGATOR_CLASS(klass)( \
        G_TYPE_CHECK_CLASS_CAST ((klass), \
                RSTTO_TYPE_NAVIGATOR, \
                RsttoNavigatorClass))

#define RSTTO_IS_NAVIGATOR_CLASS(klass)( \
        G_TYPE_CHECK_CLASS_TYPE ((klass), \
                RSTTO_TYPE_NAVIGATOR()))

typedef struct _RsttoNavigatorPriv RsttoNavigatorPriv;
typedef struct _RsttoNavigator RsttoNavigator;

struct _RsttoNavigator
{
    GObject                parent;
    RsttoNavigatorPriv    *priv;
};

typedef struct _RsttoNavigatorClass RsttoNavigatorClass;
struct _RsttoNavigatorClass
{
    GObjectClass      parent_class;
};

typedef struct _RsttoNavigatorIter RsttoNavigatorIter;


GType           rstto_navigator_get_type ();
RsttoNavigator *rstto_navigator_new ();

gint     rstto_navigator_get_n_images (RsttoNavigator *navigator);
gboolean rstto_navigator_add_file (RsttoNavigator *navigator, GFile *file, GError **);

RsttoNavigatorIter *rstto_navigator_get_iter (RsttoNavigator *navigator);


/** Iter functions */
RsttoImage *rstto_navigator_iter_get_image (RsttoNavigatorIter *iter);
gboolean    rstto_navigator_iter_previous (RsttoNavigatorIter *iter);
gboolean    rstto_navigator_iter_next (RsttoNavigatorIter *iter);
gint        rstto_navigator_iter_get_position (RsttoNavigatorIter *iter);
gboolean    rstto_navigator_iter_set_position (RsttoNavigatorIter *iter, gint pos);
void        rstto_navigator_iter_free (RsttoNavigatorIter *iter);


void        rstto_navigator_remove_all (RsttoNavigator *navigator);
void        rstto_navigator_remove_image (RsttoNavigator *navigator, RsttoImage *image);
gboolean    rstto_navigator_iter_find_image (RsttoNavigatorIter *iter, RsttoImage *image);

G_END_DECLS

#endif /* __RISTRETTO_NAVIGATOR_H__ */
