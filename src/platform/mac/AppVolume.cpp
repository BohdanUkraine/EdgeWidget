#include "platform/AppVolume.h"

#include <QProcess>
#include <QString>

namespace {
const QString kOsascript = QStringLiteral("/usr/bin/osascript");

bool run(const QString &script)
{
    return QProcess::startDetached(kOsascript, { QStringLiteral("-e"), script });
}
}

namespace AppVolume {
bool setSystem(int percent)
{
    const QString unmute = percent > 0 ? QStringLiteral(" without output muted") : QString();
    return run(QStringLiteral("set volume output volume %1").arg(percent) + unmute);
}

bool stepSystem(int delta)
{
    const QString e = QStringLiteral("-e");
    return QProcess::startDetached(
        kOsascript, { e, QStringLiteral("set v to (output volume of (get volume settings)) + %1").arg(delta),
                      e, QStringLiteral("if v > 100 then set v to 100"), e, QStringLiteral("if v < 0 then set v to 0"),
                      e, QStringLiteral("set volume output volume v without output muted") });
}

bool toggleSystemMute()
{
    return run(QStringLiteral("set volume output muted not (output muted of (get volume settings))"));
}

bool setSpotify(int)
{
    return false;
}

int spotifyLevel()
{
    return -1;
}

}
