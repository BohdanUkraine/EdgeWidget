#include "device/DeviceController.h"

#include "app/AppSettings.h"
#include "device/BleLink.h"
#include "playback/MediaController.h"
#include "playback/VolumeController.h"

#include <QDebug>

#include <algorithm>
#include <cmath>
#include <utility>

using MediaSpin::Command;

namespace {
constexpr qint64 kEchoMs = 2000;
constexpr qint64 kSeekJumpMs = 2500;
constexpr qint64 kAnnounceMs = 1000;
constexpr int kCoverWaitMs = 3000;

std::optional<LibraryItem::Kind> playlistKind(const QString &contextUri)
{
    const QStringList parts = contextUri.split(QLatin1Char(':'));
    if (parts.contains(QLatin1String("album")))
        return LibraryItem::Album;
    if (parts.contains(QLatin1String("playlist")))
        return LibraryItem::Playlist;
    if (parts.contains(QLatin1String("artist")))
        return LibraryItem::Artist;
    return std::nullopt;
}
}

DeviceController::DeviceController(MediaController *media, SpotifyClient *spotify, QObject *parent)
    : QObject(parent)
    , m_media(media)
    , m_spotify(spotify)
    , m_link(new BleLink(this))
    , m_volume(new VolumeController(spotify, this))
{
    connect(m_volume, &VolumeController::spotifyVolumeChanged, this, &DeviceController::spotifyVolumeChanged);
    connect(m_link, &BleLink::messageReceived, this, &DeviceController::onMessage);
    m_pendingTimeout.setSingleShot(true);
    m_pendingTimeout.setInterval(kCoverWaitMs);
    connect(&m_pendingTimeout, &QTimer::timeout, this, &DeviceController::applyPendingColor);
    connect(m_link, &BleLink::connectedChanged, this, [this](bool connected) {
        m_sinceConnected.start();
        m_sentTrack.clear();
        m_sentPlaying.reset();
        m_sentShuffle.reset();
        m_sentRepeat.reset();
        if (connected)
            syncDevice();
        else {
            m_pendingColor.reset();
            m_pendingTimeout.stop();
            setRecord(false, {});
            if (m_faderTouched || m_scrubTimer.isActive() || m_scrubIndex >= 0) {
                m_faderTouched = false;
                m_scrubTimer.stop();
                m_scrubIndex = -1;
                emit playlistScrubEnded();
            }
            m_pendingPlaylistIndex = -1;
            m_lastJumpIndex = -1;
        }
        emit connectedChanged(connected);
    });
    connect(m_media, &MediaController::stateChanged, this, &DeviceController::onState);
    m_last = m_media->state();

    connect(m_spotify, &SpotifyClient::activeChanged, this, [this] {
        if (m_link->isConnected() && AppSettings::instance().faderMode() == AppSettings::FaderMode::Playlist)
            sendPlaylistFaderState();
    });

    m_lastFaderMode = AppSettings::instance().faderMode();
    connect(&AppSettings::instance(), &AppSettings::changed, this, [this] {
        const AppSettings::FaderMode mode = AppSettings::instance().faderMode();
        if (mode == m_lastFaderMode)
            return;
        m_lastFaderMode = mode;
        m_pendingPlaylistIndex = -1;
        syncDevice();
    });

    m_scrubTimer.setSingleShot(true);
    m_scrubTimer.setInterval(500);
    connect(&m_scrubTimer, &QTimer::timeout, this, &DeviceController::playlistScrubEnded);
}

bool DeviceController::isConnected() const
{
    return m_link->isConnected();
}

bool DeviceController::writeRecord(const QString &uri)
{
    if (!m_hasRecord)
        return false;
    MediaSpin::Message message;
    message.command = Command::Bind;
    message.record = MediaSpin::Record{ SpotifyClient::contextUri(uri), 0 };
    m_link->send(message);
    return true;
}

void DeviceController::setRecord(bool present, const QColor &color)
{
    m_hasRecord = present;
    if (color != m_recordColor) {
        m_recordColor = color;
        emit recordColorChanged(color);
    }
}

void DeviceController::applyPendingColor()
{
    m_pendingTimeout.stop();
    if (const auto color = std::exchange(m_pendingColor, std::nullopt))
        setRecord(true, *color);
}

void DeviceController::send(Command command, const QString &data)
{
    m_link->send({ command, data, std::nullopt });
}

void DeviceController::sendModes(const MediaState &state)
{
    if (m_sentShuffle != state.shuffle) {
        m_sentShuffle = state.shuffle;
        send(Command::Remix, state.shuffle ? QStringLiteral("1") : QStringLiteral("0"));
    }
    if (m_sentRepeat != state.repeat) {
        m_sentRepeat = state.repeat;
        send(Command::LoopAlbum, state.repeat == 2 ? QStringLiteral("1") : QStringLiteral("0"));
        send(Command::LoopSong, state.repeat == 1 ? QStringLiteral("1") : QStringLiteral("0"));
    }
}

void DeviceController::sendPlaying(bool playing)
{
    if (m_sentPlaying == playing)
        return;
    m_sentPlaying = playing;
    send(playing ? Command::Resume : Command::Pause);
}

void DeviceController::onMessage(const MediaSpin::Message &message)
{
    m_sinceFromDevice.start();
    const MediaState &state = m_media->state();
    switch (message.command) {
    case Command::SwitchTo: {
        const QString uri = message.record ? message.record->uri : message.data;
        const QColor color = message.record && message.record->color ? QColor(QRgb(message.record->color)) : QColor();
        const QString context = SpotifyClient::contextUri(uri);
        const bool announce = m_sinceConnected.elapsed() < kAnnounceMs;
        if (!context.isEmpty() && !announce) {
            m_hasRecord = true;
            m_pendingColor = color;
            m_pendingFromTrack = state.trackKey();
            m_pendingFromMs = state.positionNow();
            m_pendingTimeout.start();
            m_spotify->play(context);
        } else {
            m_pendingColor.reset();
            m_pendingTimeout.stop();
            setRecord(true, color);
        }
        break;
    }
    case Command::Pause: m_sentPlaying = false; m_media->setPlaying(false); break;
    case Command::FaderTouch: {
        const bool touched = message.data == QLatin1String("1");
        qDebug().noquote() << "[fader] touch" << (touched ? "on" : "off");
        if (AppSettings::instance().faderMode() == AppSettings::FaderMode::Playlist) {
            if (touched)
                playlistTouchStart();
            else
                playlistTouchEnd();
        }
        m_faderTouched = touched;
        break;
    }
    case Command::Resume: m_sentPlaying = true; m_media->setPlaying(true); break;
    case Command::GoBack: m_media->previous(); break;
    case Command::GoForth: m_media->next(); break;
    case Command::LoopAlbum:
    case Command::LoopSong: {
        const int mode = message.command == Command::LoopAlbum ? 2 : 1;
        const bool on = message.data.isEmpty() ? state.repeat != mode : message.data == QLatin1String("1");
        const int repeat = on ? mode : (state.repeat == mode ? 0 : state.repeat);
        m_sentRepeat = repeat;
        m_media->setRepeat(repeat);
        break;
    }
    case Command::Remix:
        m_sentShuffle = message.data == QLatin1String("1");
        m_media->setShuffle(*m_sentShuffle);
        break;
    case Command::MoveTo:
        if (AppSettings::instance().faderMode() == AppSettings::FaderMode::Playlist) {
            qDebug().noquote() << "[fader] MOVE_TO" << message.data << "-> Playlist mode, scrubbing";
            playlistScrub(message.data.toInt());
        } else {
            qDebug().noquote() << "[fader] MOVE_TO" << message.data << "-> Timeline mode, seeking";
            m_media->seek(qint64(message.data.toDouble() * 1000));
        }
        break;
    case Command::SetVolume: // "+N" / "-N" a step, "mute", or the level itself
        if (message.data == QLatin1String("mute"))
            m_volume->toggleMute();
        else if (message.data.startsWith(QLatin1Char('+')) || message.data.startsWith(QLatin1Char('-')))
            m_volume->step(message.data.toInt());
        else
            m_volume->set(message.data.toInt());
        break;
    case Command::Error: emit deviceError(message.data); break;
    case Command::RequestSync:
        qDebug().noquote() << "[fader] REQUEST_SYNC: device asked for the full state again";
        syncDevice();
        break;
    default:
        break;
    }
}

void DeviceController::onState(const MediaState &state)
{
    const MediaState last = std::exchange(m_last, state);
    const bool newTrack = state.trackKey() != m_pendingFromTrack && !state.cover.isNull();
    const bool restarted = state.trackKey() == m_pendingFromTrack && m_pendingFromMs > kSeekJumpMs
                           && state.positionNow() < kSeekJumpMs;
    if (m_pendingColor && (newTrack || restarted))
        applyPendingColor();
    if (!m_link->isConnected() || !state.available)
        return;

    if (m_pendingPlaylistIndex >= 0 && state.playing) {
        if (!last.playing && last.trackKey() == m_pendingPlaylistFromTrack) {
            m_lastJumpIndex = m_pendingPlaylistIndex;
            playlistJumpTo(std::exchange(m_pendingPlaylistIndex, -1));
        } else {
            m_pendingPlaylistIndex = -1;
        }
    }

    const bool settling = m_sinceFromDevice.isValid() && m_sinceFromDevice.elapsed() < kEchoMs;

    const bool playlistMode = AppSettings::instance().faderMode() == AppSettings::FaderMode::Playlist;

    if (state.durationMs > 0 && state.trackKey() != m_sentTrack) {
        m_sentTrack = state.trackKey();
        if (playlistMode) {
            sendPlaylistFaderState();
        } else {
            m_sentPlaying = true;
            send(Command::TrackLen, QString::number(std::lround(state.durationMs / 1000.0)));
            if (state.positionNow() > kSeekJumpMs)
                send(Command::MoveTo, QString::number(state.positionNow() / 1000));
            if (!state.playing)
                sendPlaying(false);
        }
        return;
    }

    if (!settling) {
        sendPlaying(playlistMode ? false : state.playing);
        sendModes(state);
    }

    if (!settling && !playlistMode
        && state.durationMs > 0 && std::abs(state.positionNow() - last.positionNow()) > kSeekJumpMs) {
        send(Command::MoveTo, QString::number(state.positionNow() / 1000));
    }
}

void DeviceController::syncDevice()
{
    const bool playlistMode = AppSettings::instance().faderMode() == AppSettings::FaderMode::Playlist;
    send(Command::SetMode, playlistMode ? QStringLiteral("1") : QStringLiteral("0"));

    const MediaState &state = m_media->state();
    if (!state.available)
        return;
    sendModes(state);
    if (state.durationMs <= 0)
        return;
    m_sentTrack = state.trackKey();
    if (playlistMode) {
        sendPlaylistFaderState();
    } else {
        m_sentPlaying = true;
        send(Command::TrackLen, QString::number(std::lround(state.durationMs / 1000.0)));
        send(Command::MoveTo, QString::number(state.positionNow() / 1000));
        if (!state.playing)
            sendPlaying(false);
    }
}

void DeviceController::sendPlaylistFaderState()
{
    const QString uri = m_spotify->activeUri();
    if (uri != m_playlistContextUri) {
        m_playlistContextUri = uri;
        m_playlistTracks.clear();
        m_lastJumpIndex = -1;
        const std::optional<LibraryItem::Kind> kind = playlistKind(uri);
        if (!kind) {
            qDebug().noquote() << "[fader] Playlist mode: no orderable track list for context" << uri
                               << "(Liked Songs, a local file, or nothing playing)";
            return;
        }
        LibraryItem item;
        item.uri = uri;
        item.kind = *kind;
        const int id = ++m_playlistRequestId;
        m_spotify->fetchTracks(item, [this, id, uri](const QList<Track> &tracks, bool ok) {
            if (id != m_playlistRequestId || uri != m_playlistContextUri)
                return;
            m_playlistTracks = ok ? tracks : QList<Track>();
            sendPlaylistFaderState();
        });
        return;
    }
    if (m_playlistTracks.isEmpty())
        return;

    const int index = playlistIndexOf(m_media->state().title);
    if (index < 0) {
        qDebug().noquote() << "[fader] Playlist mode: current track" << m_media->state().title
                           << "not found among" << m_playlistTracks.size() << "fetched tracks";
        return;
    }

    send(Command::TrackLen, QString::number(int(m_playlistTracks.size())));
    send(Command::MoveTo, QString::number(index));
    m_sentPlaying = true;
    sendPlaying(false);
}

void DeviceController::playlistTouchStart()
{
    m_scrubTimer.stop();
    if (m_playlistContextUri != m_spotify->activeUri() || m_playlistTracks.isEmpty())
        return;
    const int index = m_scrubIndex >= 0 ? m_scrubIndex : playlistIndexOf(m_media->state().title);
    if (index >= 0 && index < int(m_playlistTracks.size()))
        emit playlistScrubMoved(m_playlistTracks, index);
}

void DeviceController::playlistTouchEnd()
{
    m_scrubTimer.stop();
    emit playlistScrubEnded();
}

void DeviceController::playlistScrub(int index)
{
    if (m_playlistContextUri != m_spotify->activeUri() || index < 0 || index >= int(m_playlistTracks.size())) {
        qDebug().noquote() << "[fader] playlistScrub: ignored, index" << index << "tracks"
                           << m_playlistTracks.size() << "context" << m_playlistContextUri
                           << "active" << m_spotify->activeUri();
        return;
    }
    emit playlistScrubMoved(m_playlistTracks, index);
    m_scrubIndex = index;

    if (m_media->state().playing) {
        m_pendingPlaylistIndex = -1;
        if (index != m_lastJumpIndex) {
            m_lastJumpIndex = index;
            playlistJumpTo(index);
        }
    } else {
        m_pendingPlaylistIndex = index;
        m_pendingPlaylistFromTrack = m_media->state().trackKey();
    }

    if (!m_faderTouched)
        m_scrubTimer.start();
}

void DeviceController::playlistJumpTo(int index)
{
    if (m_playlistContextUri != m_spotify->activeUri() || index < 0 || index >= int(m_playlistTracks.size()))
        return;
    LibraryItem context;
    context.uri = m_playlistContextUri;
    context.kind = *playlistKind(m_playlistContextUri);

    m_spotify->playTrack(context, m_playlistTracks, index);
}

int DeviceController::playlistIndexOf(const QString &title) const
{
    for (int i = 0, n = int(m_playlistTracks.size()); i < n; ++i) {
        if (m_playlistTracks.at(i).name == title)
            return i;
    }
    return -1;
}
