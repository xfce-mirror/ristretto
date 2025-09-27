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

#include "file_manager_integration_factory.h"
#include "file_manager_integration_thunar.h"

RsttoFileManagerIntegration *
rstto_file_manager_integration_factory_create (RsttoDesktopEnvironment desktop_env)
{
    switch (desktop_env)
    {
        default:
        case DESKTOP_ENVIRONMENT_NONE:
            return NULL;

        case DESKTOP_ENVIRONMENT_XFCE:
            return rstto_file_manager_integration_thunar_new ();
    }

    g_assert_not_reached ();
}
