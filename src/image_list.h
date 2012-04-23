/*
 *  Copyright (c) Stephan Arts 2006-2012 <stephan@xfce.org>
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version 2
 *  of the License, or (at your option) any later version.
 * 
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 * 
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 *  02110-1301, USA.
 */

#ifndef __RISTRETTO_IMAGE_LIST_H__
#define __RISTRETTO_IMAGE_LIST_H__

G_BEGIN_DECLS

#define RSTTO_TYPE_IMAGE_LIST rstto_image_list_get_type()

#define RSTTO_IMAGE_LIST(obj)( \
        G_TYPE_CHECK_INSTANCE_CAST ((obj), \
                RSTTO_TYPE_IMAGE_LIST, \
                RsttoImageList))

#define RSTTO_IS_IMAGE_LIST(obj)( \
        G_TYPE_CHECK_INSTANCE_TYPE ((obj), \
                RSTTO_TYPE_IMAGE_LIST))

#define RSTTO_IMAGE_LIST_CLASS(klass)( \
        G_TYPE_CHECK_CLASS_CAST ((klass), \
                RSTTO_TYPE_IMAGE_LIST, \
                RsttoImageListClass))

#define RSTTO_IS_IMAGE_LIST_CLASS(klass)( \
        G_TYPE_CHECK_CLASS_TYPE ((klass), \
                RSTTO_TYPE_IMAGE_LIST()))

typedef struct _RsttoImageListPriv RsttoImageListPriv;
typedef struct _RsttoImageList RsttoImageList;

struct _RsttoImageList
{
    GObject                parent;
    RsttoImageListPriv    *priv;
};

typedef struct _RsttoImageListClass RsttoImageListClass;
struct _RsttoImageListClass
{
    GObjectClass      parent_class;
};


#define RSTTO_TYPE_IMAGE_LIST_ITER rstto_image_list_iter_get_type()

#define RSTTO_IMAGE_LIST_ITER(obj)( \
        G_TYPE_CHECK_INSTANCE_CAST ((obj), \
                RSTTO_TYPE_IMAGE_LIST_ITER, \
                RsttoImageListIter))

#define RSTTO_IS_IMAGE_LIST_ITER(obj)( \
        G_TYPE_CHECK_INSTANCE_TYPE ((obj), \
                RSTTO_TYPE_IMAGE_LIST_ITER))

#define RSTTO_IMAGE_LIST_ITER_CLASS(klass)( \
        G_TYPE_CHECK_CLASS_CAST ((klass), \
                RSTTO_TYPE_IMAGE_LIST_ITER, \
                RsttoImageListIterClass))

#define RSTTO_IS_IMAGE_LIST_ITER_CLASS(klass)( \
        G_TYPE_CHECK_CLASS_TYPE ((klass), \
                RSTTO_TYPE_IMAGE_LIST_ITER()))

typedef struct _RsttoImageListIter RsttoImageListIter;
typedef struct _RsttoImageListIterPriv RsttoImageListIterPriv;
struct _RsttoImageListIter
{
    GObject parent;
    RsttoImageListIterPriv *priv;
};

typedef struct _RsttoImageListIterClass RsttoImageListIterClass;
struct _RsttoImageListIterClass
{
    GObjectClass      parent_class;
};


GType
rstto_image_list_get_type (void);

RsttoImageList *
rstto_image_list_new (void);

gint
rstto_image_list_get_n_images (
        RsttoImageList *image_list);

gboolean
rstto_image_list_add_file (
        RsttoImageList *image_list,
        RsttoFile *file,
        GError **);

gboolean
rstto_image_list_set_directory (
        RsttoImageList *image_list,
        GFile *dir,
        GError **);

gboolean
rstto_image_list_is_busy (
        RsttoImageList *list );


GCompareFunc
rstto_image_list_get_compare_func (
        RsttoImageList *image_list);
void
rstto_image_list_set_compare_func (
        RsttoImageList *image_list,
        GCompareFunc func);

/** Built-in Sorting Functions */
void
rstto_image_list_set_sort_by_name (
        RsttoImageList *image_list);

void
rstto_image_list_set_sort_by_date (
        RsttoImageList *image_list);

RsttoImageListIter *
rstto_image_list_get_iter (RsttoImageList *image_list);

/** Iter functions */
GType
rstto_image_list_iter_get_type ();

RsttoFile *
rstto_image_list_iter_get_file (
        RsttoImageListIter *iter );

gboolean
rstto_image_list_iter_previous (
        RsttoImageListIter *iter);

gboolean
rstto_image_list_iter_next (
        RsttoImageListIter *iter);

gboolean
rstto_image_list_iter_has_previous (
        RsttoImageListIter *iter);

gboolean
rstto_image_list_iter_has_next (
        RsttoImageListIter *iter);

gint
rstto_image_list_iter_get_position (
        RsttoImageListIter *iter);

void
rstto_image_list_iter_set_position (
        RsttoImageListIter *iter,
        gint pos);

void
rstto_image_list_remove_file (
        RsttoImageList *image_list,
        RsttoFile *file);

gboolean
rstto_image_list_iter_find_file (
        RsttoImageListIter *iter,
        RsttoFile *file);

RsttoImageListIter *
rstto_image_list_iter_clone (
        RsttoImageListIter *iter);

gboolean
rstto_image_list_iter_get_sticky (
        RsttoImageListIter *iter);

G_END_DECLS

#endif /* __RISTRETTO_IMAGE_LIST_H__ */
