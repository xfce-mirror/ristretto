/*
 *  Copyright (c) Dmitry Petrachkov 2025 <dmitry-petrachkov@outlook.com>
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

#ifndef __RISTRETTO_FILE_MANAGER_INTEGRATION_H__
#define __RISTRETTO_FILE_MANAGER_INTEGRATION_H__

#include "settings.h"

#include <gio/gio.h>
#include <glib-object.h>

G_BEGIN_DECLS

#define RSTTO_TYPE_FILE_MANAGER_INTEGRATION (rstto_file_manager_integration_get_type ())
G_DECLARE_INTERFACE (RsttoFileManagerIntegration, rstto_file_manager_integration, RSTTO, FILE_MANAGER_INTEGRATION, GObject)

typedef struct _RsttoFileManagerIntegrationInterface RsttoFileManagerIntegrationInterface;
typedef struct _RsttoFileManagerIntegration RsttoFileManagerIntegration;

struct _RsttoFileManagerIntegrationInterface
{
    GTypeInterface parent;

    gboolean (*get_sort_settings) (RsttoFileManagerIntegration *self,
                                   GFile *dir,
                                   RsttoSortType *sort_type,
                                   RsttoSortOrder *sort_order);
};

gboolean
rstto_file_manager_integration_get_sort_settings (RsttoFileManagerIntegration *self,
                                                  GFile *dir,
                                                  RsttoSortType *sort_type,
                                                  RsttoSortOrder *sort_order);

G_END_DECLS

#endif /* __RISTRETTO_FILE_MANAGER_INTEGRATION_H__ */
