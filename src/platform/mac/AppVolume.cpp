#include "platform/AppVolume.h"

#include <QProcess>
#include <QString>

namespace AppVolume {
bool setSystem(int percent)
{
    return QProcess::startDetached(QStringLiteral("/usr/bin/osascript"),
                                   { QStringLiteral("-e"), QStringLiteral("set volume output volume %1").arg(percent) });
}

bool setSpotify(int)
{
    return false;
}

}
