#include "platform/AppVolume.h"

#include <QProcess>
#include <QString>

// macOS has no per-app volume API; the Web API handles Spotify's volume instead.
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

} // namespace AppVolume
