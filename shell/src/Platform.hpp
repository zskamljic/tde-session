#pragma once

#include <QtGlobal>

namespace shell {

// The shell draws its own dialogs. Going through the portal, as the session has programs do,
// would have it wait on the portal while the portal waits on the shell, which is one of its
// back ends.
inline void keepOffPortal()
{
    if (qgetenv("QT_QPA_PLATFORMTHEME") == "xdgdesktopportal")
        qunsetenv("QT_QPA_PLATFORMTHEME");
}

} // namespace shell
