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

#ifndef __RISTRETTO_FILE_MANAGER_INTEGRATION_THUNAR_H__
#define __RISTRETTO_FILE_MANAGER_INTEGRATION_THUNAR_H__

#include "file_manager_integration.h"

G_BEGIN_DECLS

#define RSTTO_TYPE_FILE_MANAGER_INTEGRATION_THUNAR (rstto_file_manager_integration_thunar_get_type ())
G_DECLARE_DERIVABLE_TYPE (RsttoFileManagerIntegrationThunar, rstto_file_manager_integration_thunar, RSTTO, FILE_MANAGER_INTEGRATION_THUNAR, GObject)

typedef struct _RsttoFileManagerIntegrationThunarClass RsttoFileManagerIntegrationThunarClass;
typedef struct _RsttoFileManagerIntegrationThunar RsttoFileManagerIntegrationThunar;

struct _RsttoFileManagerIntegrationThunarClass
{
    GObjectClass parent;
};

RsttoFileManagerIntegration *
rstto_file_manager_integration_thunar_new (void);

G_END_DECLS

#endif /* __RISTRETTO_FILE_MANAGER_INTEGRATION_THUNAR_H__ */
