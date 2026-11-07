#include "platform/AppInit.h"

#include <QByteArray>
#include <QtGlobal>

namespace AppInit {
void beforeQApplication()
{
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM") && !qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY"))
        qputenv("QT_QPA_PLATFORM", "xcb;wayland");
}

}
