#include "platform/AppVolume.h"

#include <QProcess>
#include <QStandardPaths>
#include <QString>

// Per-app volume is not done on Linux (PipeWire / PulseAudio differ); the Web API handles it.
namespace AppVolume {

bool setSystem(int percent)
{
    const QString value = QString::number(percent) + QLatin1Char('%');
    const QString wpctl = QStandardPaths::findExecutable(QStringLiteral("wpctl")); // PipeWire
    if (!wpctl.isEmpty())
        return QProcess::startDetached(wpctl, { QStringLiteral("set-volume"), QStringLiteral("@DEFAULT_AUDIO_SINK@"), value });
    const QString pactl = QStandardPaths::findExecutable(QStringLiteral("pactl")); // PulseAudio
    if (!pactl.isEmpty())
        return QProcess::startDetached(pactl, { QStringLiteral("set-sink-volume"), QStringLiteral("@DEFAULT_SINK@"), value });
    return false;
}

bool setSpotify(int)
{
    return false;
}

} // namespace AppVolume
