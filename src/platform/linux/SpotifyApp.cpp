#include "platform/SpotifyApp.h"

#include <QDir>
#include <QFile>
#include <QProcess>
#include <QStandardPaths>

namespace SpotifyApp {

bool isRunning()
{
    // Any process named "spotify" (the .deb, snap and Flatpak builds all are).
    const QStringList pids = QDir(QStringLiteral("/proc")).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &pid : pids) {
        QFile comm(QStringLiteral("/proc/%1/comm").arg(pid));
        if (comm.open(QIODevice::ReadOnly) && comm.readAll().trimmed() == "spotify")
            return true;
    }
    return false;
}

bool launchQuietly()
{
    // --minimized: no window where Spotify supports it (ignored otherwise).
    const QStringList args = { QStringLiteral("--minimized") };
    const QString exe = QStandardPaths::findExecutable(QStringLiteral("spotify")); // .deb, AUR, snap
    if (!exe.isEmpty())
        return QProcess::startDetached(exe, args);
    for (const QString &flatpak : { QDir::homePath() + QStringLiteral("/.local/share/flatpak/exports/bin/com.spotify.Client"),
                                    QStringLiteral("/var/lib/flatpak/exports/bin/com.spotify.Client") }) {
        if (QFile::exists(flatpak))
            return QProcess::startDetached(flatpak, args);
    }
    return false; // not installed
}

} // namespace SpotifyApp
