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

#ifndef __RISTRETTO_UTIL_H__
#define __RISTRETTO_UTIL_H__

G_BEGIN_DECLS

typedef enum
{
  RSTTO_IMAGE_ORIENT_NONE = 1,
  RSTTO_IMAGE_ORIENT_FLIP_HORIZONTAL,
  RSTTO_IMAGE_ORIENT_180,
  RSTTO_IMAGE_ORIENT_FLIP_VERTICAL,
  RSTTO_IMAGE_ORIENT_FLIP_TRANSPOSE,
  RSTTO_IMAGE_ORIENT_90,
  RSTTO_IMAGE_ORIENT_FLIP_TRANSVERSE,
  RSTTO_IMAGE_ORIENT_270,
  RSTTO_IMAGE_ORIENT_NOT_DETERMINED,
} RsttoImageOrientation;

typedef enum {
    DESKTOP_TYPE_NONE = 0,
    DESKTOP_TYPE_XFCE,
    DESKTOP_TYPE_GNOME,
    DESKTOP_TYPE_COUNT
} RsttoDesktopType;

typedef enum {
    SORT_TYPE_NAME = 0,
    SORT_TYPE_DATE,
    SORT_TYPE_COUNT,
} RsttoSortType;


#define THUMBNAIL_SIZE_VERY_SMALL_SIZE   24
#define THUMBNAIL_SIZE_SMALLER_SIZE      32
#define THUMBNAIL_SIZE_SMALL_SIZE        48
#define THUMBNAIL_SIZE_NORMAL_SIZE       64
#define THUMBNAIL_SIZE_LARGE_SIZE        96
#define THUMBNAIL_SIZE_LARGER_SIZE      128
#define THUMBNAIL_SIZE_VERY_LARGE_SIZE  256

typedef enum {
    THUMBNAIL_SIZE_VERY_SMALL = 0,
    THUMBNAIL_SIZE_SMALLER,
    THUMBNAIL_SIZE_SMALL,
    THUMBNAIL_SIZE_NORMAL,
    THUMBNAIL_SIZE_LARGE,
    THUMBNAIL_SIZE_LARGER,
    THUMBNAIL_SIZE_VERY_LARGE,
    THUMBNAIL_SIZE_COUNT,
} RsttoThumbnailSize;

#define THUMBNAIL_SIZE_VERY_SMALL_SIZE   24
#define THUMBNAIL_SIZE_SMALLER_SIZE      32
#define THUMBNAIL_SIZE_SMALL_SIZE        48
#define THUMBNAIL_SIZE_NORMAL_SIZE       64
#define THUMBNAIL_SIZE_LARGE_SIZE        96
#define THUMBNAIL_SIZE_LARGER_SIZE      128
#define THUMBNAIL_SIZE_VERY_LARGE_SIZE  256

G_END_DECLS

#endif /* __RSTTO_UTIL_H__ */
