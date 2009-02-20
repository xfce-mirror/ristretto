/*
 *  Copyright (C) Stephan Arts 2009 <stephan@xfce.org>
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

#ifndef __RISTRETTO_IMAGE_CACHE_H__
#define __RISTRETTO_IMAGE_CACHE_H__

G_BEGIN_DECLS

#define RSTTO_TYPE_IMAGE_CACHE rstto_image_cache_get_type()

#define RSTTO_IMAGE_CACHE(obj)( \
        G_TYPE_CHECK_INSTANCE_CAST ((obj), \
                RSTTO_TYPE_IMAGE_CACHE, \
                RsttoImageCache))

#define RSTTO_IS_IMAGE_CACHE(obj)( \
        G_TYPE_CHECK_INSTANCE_TYPE ((obj), \
                RSTTO_TYPE_IMAGE_CACHE))

#define RSTTO_IMAGE_CACHE_CLASS(klass)( \
        G_TYPE_CHECK_CLASS_CAST ((klass), \
                RSTTO_TYPE_IMAGE_CACHE, \
                RsttoImageCacheClass))

#define RSTTO_IS_IMAGE_CACHE_CLASS(klass)( \
        G_TYPE_CHECK_CLASS_TYPE ((klass), \
                RSTTO_TYPE_IMAGE_CACHE()))

typedef struct _RsttoImageCache RsttoImageCache;

typedef struct _RsttoImageCacheClass RsttoImageCacheClass;

RsttoImageCache *rstto_image_cache_new ();

void rstto_image_cache_push_image (RsttoImageCache *cache, RsttoImage *image);

G_END_DECLS

#endif /* __RISTRETTO_IMAGE_CACHE_H__ */
