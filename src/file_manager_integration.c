/*
 *  Copyright (c) 2025 The Xfce Development Team
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

#include "file_manager_integration.h"

G_DEFINE_INTERFACE (RsttoFileManagerIntegration, rstto_file_manager_integration, G_TYPE_OBJECT)

static void
rstto_file_manager_integration_default_init (RsttoFileManagerIntegrationInterface *iface)
{
}

gboolean
rstto_file_manager_integration_get_sort_settings (RsttoFileManagerIntegration *self,
                                                  GFile *dir,
                                                  RsttoSortType *sort_type,
                                                  RsttoSortOrder *sort_order)
{
    RsttoFileManagerIntegrationInterface *iface = RSTTO_FILE_MANAGER_INTEGRATION_GET_IFACE (self);

    return iface->get_sort_settings (self, dir, sort_type, sort_order);
}
