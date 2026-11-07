#include "platform/AppVolume.h"

#include <QProcess>
#include <QStandardPaths>
#include <QString>

#include <cstdlib>

namespace {
QString wpctl()
{
    return QStandardPaths::findExecutable(QStringLiteral("wpctl"));
}

QString pactl()
{
    return QStandardPaths::findExecutable(QStringLiteral("pactl"));
}
}

namespace AppVolume {
bool setSystem(int percent)
{
    const QString value = QString::number(percent) + QLatin1Char('%');
    const QString unmute = percent > 0 ? QStringLiteral("0") : QString();
    if (const QString w = wpctl(); !w.isEmpty()) {
        if (!unmute.isEmpty())
            QProcess::startDetached(w, { QStringLiteral("set-mute"), QStringLiteral("@DEFAULT_AUDIO_SINK@"), unmute });
        return QProcess::startDetached(w, { QStringLiteral("set-volume"), QStringLiteral("@DEFAULT_AUDIO_SINK@"), value });
    }
    if (const QString p = pactl(); !p.isEmpty()) {
        if (!unmute.isEmpty())
            QProcess::startDetached(p, { QStringLiteral("set-sink-mute"), QStringLiteral("@DEFAULT_SINK@"), unmute });
        return QProcess::startDetached(p, { QStringLiteral("set-sink-volume"), QStringLiteral("@DEFAULT_SINK@"), value });
    }
    return false;
}

bool stepSystem(int delta)
{
    if (delta == 0)
        return true;
    const QString step = QString::number(std::abs(delta)) + QLatin1Char('%');
    const QString sign = delta > 0 ? QStringLiteral("+") : QStringLiteral("-");
    if (const QString w = wpctl(); !w.isEmpty()) {
        QProcess::startDetached(w, { QStringLiteral("set-mute"), QStringLiteral("@DEFAULT_AUDIO_SINK@"), QStringLiteral("0") });
        return QProcess::startDetached(w, { QStringLiteral("set-volume"), QStringLiteral("-l"), QStringLiteral("1.0"),
                                            QStringLiteral("@DEFAULT_AUDIO_SINK@"),
                                            step + sign });
    }
    if (const QString p = pactl(); !p.isEmpty()) {
        QProcess::startDetached(p, { QStringLiteral("set-sink-mute"), QStringLiteral("@DEFAULT_SINK@"), QStringLiteral("0") });
        return QProcess::startDetached(p, { QStringLiteral("set-sink-volume"), QStringLiteral("@DEFAULT_SINK@"),
                                            sign + step });
    }
    return false;
}

bool toggleSystemMute()
{
    if (const QString w = wpctl(); !w.isEmpty())
        return QProcess::startDetached(w, { QStringLiteral("set-mute"), QStringLiteral("@DEFAULT_AUDIO_SINK@"),
                                            QStringLiteral("toggle") });
    if (const QString p = pactl(); !p.isEmpty())
        return QProcess::startDetached(p, { QStringLiteral("set-sink-mute"), QStringLiteral("@DEFAULT_SINK@"),
                                            QStringLiteral("toggle") });
    return false;
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
