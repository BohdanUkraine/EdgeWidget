#include "platform/AppInit.h"

#include <QByteArray>
#include <QtGlobal>

namespace AppInit {

void beforeQApplication()
{
    // Under Wayland a window cannot place itself (and GNOME has no layer-shell): run through
    // XWayland. Falls back to Wayland if there is no XWayland; QT_QPA_PLATFORM set by hand wins.
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM") && !qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY"))
        qputenv("QT_QPA_PLATFORM", "xcb;wayland");
}

} // namespace AppInit
