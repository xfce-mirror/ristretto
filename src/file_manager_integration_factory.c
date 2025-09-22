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
