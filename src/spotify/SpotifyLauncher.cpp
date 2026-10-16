#include "spotify/SpotifyLauncher.h"

#include "platform/SpotifyApp.h"

namespace {
constexpr int kLaunchTimeoutMs = 20000;
} // namespace

SpotifyLauncher &SpotifyLauncher::instance()
{
    static SpotifyLauncher launcher;
    return launcher;
}

SpotifyLauncher::SpotifyLauncher()
{
    m_timeout.setSingleShot(true);
    connect(&m_timeout, &QTimer::timeout, this, [this] {
        if (!m_launching)
            return;
        setLaunching(false);
        emit failed(tr("Не вдалося запустити Spotify"));
    });
}

bool SpotifyLauncher::isRunning()
{
    return SpotifyApp::isRunning();
}

bool SpotifyLauncher::launch()
{
    if (m_launching)
        return true;

    if (!isRunning() && !SpotifyApp::launchQuietly()) {
        emit failed(tr("Не вдалося запустити Spotify"));
        return false;
    }
    // Already running (maybe still starting up) or just started: wait for it to show up.
    setLaunching(true);
    m_timeout.start(kLaunchTimeoutMs);
    return true;
}

void SpotifyLauncher::finished()
{
    m_timeout.stop();
    setLaunching(false);
}

void SpotifyLauncher::setLaunching(bool launching)
{
    if (launching == m_launching)
        return;
    m_launching = launching;
    emit launchingChanged(launching);
}
