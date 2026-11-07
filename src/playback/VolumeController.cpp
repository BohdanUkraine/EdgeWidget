#include "playback/VolumeController.h"

#include "app/AppSettings.h"
#include "platform/AppVolume.h"
#include "spotify/SpotifyClient.h"

#include <algorithm>
#include <utility>

namespace {
constexpr qint64 kFreshMs = 3000; // our own last value: Spotify reports a new volume ~1.5 s late
}

VolumeController::VolumeController(SpotifyClient *spotify, QObject *parent)
    : QObject(parent)
    , m_spotify(spotify)
{
}

bool VolumeController::spotifyTarget() const
{
    return AppSettings::instance().volumeTarget() == AppSettings::VolumeTarget::Spotify;
}

void VolumeController::set(int percent)
{
    percent = std::clamp(percent, 0, 100);
    if (!spotifyTarget()) {
        AppVolume::setSystem(percent);
        return;
    }
    m_mutedFrom = -1;
    m_viaMixer = !m_spotify->isConnected();
    setSpotifyLevel(percent);
}

void VolumeController::step(int delta)
{
    if (!spotifyTarget()) {
        AppVolume::stepSystem(delta);
        return;
    }
    m_pendingDelta += delta;
    process();
}

void VolumeController::toggleMute()
{
    if (!spotifyTarget()) {
        AppVolume::toggleSystemMute();
        return;
    }
    m_pendingMute = !m_pendingMute;
    process();
}

void VolumeController::process()
{
    if (m_fetching)
        return;
    if (m_level >= 0 && m_levelAt.isValid() && m_levelAt.elapsed() < kFreshMs) {
        apply(m_level);
        return;
    }
    m_viaMixer = !m_spotify->isConnected();
    if (m_viaMixer) {
        apply(AppVolume::spotifyLevel());
        return;
    }
    m_fetching = true;
    m_spotify->fetchVolume([this](int percent) {
        m_fetching = false;
        apply(percent);
    });
}

void VolumeController::apply(int current)
{
    const int delta = std::exchange(m_pendingDelta, 0);
    const bool mute = std::exchange(m_pendingMute, false);
    if (current < 0)
        return;

    int level = current;
    if (mute && m_mutedFrom >= 0) {
        level = std::exchange(m_mutedFrom, -1);
    } else if (mute) {
        m_mutedFrom = level;
        level = 0;
    }
    if (delta) {
        if (m_mutedFrom >= 0)
            level = std::exchange(m_mutedFrom, -1);
        level = std::clamp(level + delta, 0, 100);
    }

    if (level != current) {
        setSpotifyLevel(level);
        return;
    }
    remember(level);
    if (delta)
        emit spotifyVolumeChanged(level); // at 0 or 100: still show the scale
}

void VolumeController::setSpotifyLevel(int percent)
{
    remember(percent);
    if (m_viaMixer)
        AppVolume::setSpotify(percent);
    else
        m_spotify->setVolume(percent);
    emit spotifyVolumeChanged(percent);
}

void VolumeController::remember(int percent)
{
    m_level = percent;
    m_levelAt.restart();
}
