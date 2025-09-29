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

#include "file_manager_integration.h"
#include "file_manager_integration_thunar.h"
#include "settings.h"

G_DEFINE_INTERFACE (RsttoFileManagerIntegration, rstto_file_manager_integration, G_TYPE_OBJECT)

static void
rstto_file_manager_integration_default_init (RsttoFileManagerIntegrationInterface *iface)
{
    g_object_interface_install_property (iface,
                                         g_param_spec_object ("directory",
                                                              NULL,
                                                              NULL,
                                                              G_TYPE_FILE,
                                                              G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

    g_object_interface_install_property (iface,
                                         g_param_spec_int ("sort-type",
                                                           NULL,
                                                           NULL,
                                                           -1, SORT_TYPE_COUNT,
                                                           -1,
                                                           G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));


    g_object_interface_install_property (iface,
                                         g_param_spec_int ("sort-order",
                                                           NULL,
                                                           NULL,
                                                           -1, SORT_ORDER_COUNT,
                                                           -1,
                                                           G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));
}

void
rstto_file_manager_integration_set_directory (RsttoFileManagerIntegration *self,
                                              GFile *directory)
{
    g_object_set (self, "directory", directory, NULL);
}

RsttoFileManagerIntegration *
rstto_file_manager_integration_factory_create (RsttoDesktopEnvironment desktop_env)
{
    return NULL;
}
