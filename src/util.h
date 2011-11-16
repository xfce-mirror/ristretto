/*
 *  Copyright (c) Stephan Arts 2011 <stephan@xfce.org>
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
    DESKTOP_TYPE_GNOME
} RsttoDesktopType;

gboolean
rstto_launch_help (void);

#endif /* __RSTTO_UTIL_H__ */
