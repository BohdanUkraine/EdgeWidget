#include "device/DeviceController.h"

#include "app/AppSettings.h"
#include "device/DeviceLink.h"
#include "platform/AppVolume.h"
#include "playback/MediaController.h"

#include <QDebug>

#include <algorithm>
#include <cmath>
#include <utility>

using MediaSpin::Command;

namespace {
constexpr qint64 kEchoMs = 2000;       // the player state lags this long behind a device command
constexpr qint64 kSeekJumpMs = 2500;   // position jump that counts as a seek
constexpr qint64 kAnnounceMs = 1000;   // the device says what lies on the reader right after connecting
constexpr int kCoverWaitMs = 3000;     // longest a record's colour waits for its cover

// Playlist fader mode needs an addressable, orderable track list: Liked Songs and a local
// file playing with no Spotify context at all don't have one.
//
// Most endpoints give "spotify:playlist:ID" (kind right after "spotify:"), but some — the play
// history behind the startup fallback below included — still answer with the older
// "spotify:user:USERID:playlist:ID": look for the kind anywhere in the URI, not just position 1.
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
} // namespace

DeviceController::DeviceController(MediaController *media, SpotifyClient *spotify, QObject *parent)
    : QObject(parent)
    , m_media(media)
    , m_spotify(spotify)
    , m_link(new DeviceLink(this))
{
    connect(m_link, &DeviceLink::messageReceived, this, &DeviceController::onMessage);
    m_pendingTimeout.setSingleShot(true);
    m_pendingTimeout.setInterval(kCoverWaitMs);
    connect(&m_pendingTimeout, &QTimer::timeout, this, &DeviceController::applyPendingColor);
    connect(m_link, &DeviceLink::connectedChanged, this, [this](bool connected) {
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
                emit playlistScrubEnded(); // let any preview UI close
            }
            m_pendingPlaylistIndex = -1;
            m_lastJumpIndex = -1;
        }
        emit connectedChanged(connected);
    });
    connect(m_media, &MediaController::stateChanged, this, &DeviceController::onState);
    m_last = m_media->state();

    // Spotify was already playing something before this app even started: the track-changed
    // path in onState() can run before SpotifyClient has resolved activeUri() for it (its own
    // "what's playing" request is still in flight), so sendPlaylistFaderState() finds no context
    // yet and gives up — and nothing re-asks it, because the track itself never changes again.
    // Retry whenever activeUri() lands, however late.
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
        m_pendingPlaylistIndex = -1; // a pick from the mode we just left no longer applies
        syncDevice(); // re-announce TRACK_LEN / MOVE_TO for the new mode without touching playback
    });

    // Fallback for firmware without FADER_TOUCH: treat MOVE_TO going quiet as the release.
    // Committing already happens live in playlistScrub() (while playing) or waits for Play
    // (while paused) — this timer only needs to close the preview popup.
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
        return false; // a tag can only be written while it lies on the reader
    MediaSpin::Message message;
    message.command = Command::Bind;
    message.record = MediaSpin::Record{ SpotifyClient::contextUri(uri), 0 }; // colour is factory-set
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

// ---------------------------------------------------------------- device -> app

void DeviceController::onMessage(const MediaSpin::Message &message)
{
    m_sinceFromDevice.start();
    const MediaState &state = m_media->state();
    switch (message.command) {
    case Command::SwitchTo: {
        const QString uri = message.record ? message.record->uri : message.data;
        const QColor color = message.record && message.record->color ? QColor(QRgb(message.record->color)) : QColor();
        const QString context = SpotifyClient::contextUri(uri);
        // Right after connecting it only says what already lies there: just the colour, keep the music.
        const bool announce = m_sinceConnected.elapsed() < kAnnounceMs;
        if (!context.isEmpty() && !announce) {
            m_hasRecord = true;
            m_pendingColor = color; // shown with the record's first cover, see onState()
            m_pendingFromTrack = state.trackKey();
            m_pendingFromMs = state.positionNow();
            m_pendingTimeout.start();
            m_spotify->play(context);
        } else {
            m_pendingColor.reset(); // a record put on before this one no longer counts
            m_pendingTimeout.stop();
            setRecord(true, color); // an empty record still changes the colour
        }
        break;
    }
    case Command::Pause: m_sentPlaying = false; m_media->setPlaying(false); break; // the device knows already
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
        // data "1" / "0" sets the mode; no data toggles it (older firmware)
        const int mode = message.command == Command::LoopAlbum ? 2 : 1;
        const bool on = message.data.isEmpty() ? state.repeat != mode : message.data == QLatin1String("1");
        const int repeat = on ? mode : (state.repeat == mode ? 0 : state.repeat);
        m_sentRepeat = repeat; // the device lit its lamps itself
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
            playlistScrub(message.data.toInt()); // preview the track the fader points at now
        } else {
            qDebug().noquote() << "[fader] MOVE_TO" << message.data << "-> Timeline mode, seeking";
            m_media->seek(qint64(message.data.toDouble() * 1000));
        }
        break;
    case Command::SetVolume: {
        const int percent = std::clamp(message.data.toInt(), 0, 100);
        if (AppSettings::instance().volumeTarget() == AppSettings::VolumeTarget::System) {
            AppVolume::setSystem(percent);
        } else {
            m_media->setVolume(percent);
            emit spotifyVolumeChanged(percent);
        }
        break;
    }
    case Command::Error: emit deviceError(message.data); break; // the device words it for the user
    case Command::RequestSync:
        qDebug().noquote() << "[fader] REQUEST_SYNC: device asked for the full state again";
        syncDevice();
        break;
    default:
        break;
    }
}

// ---------------------------------------------------------------- app -> device

void DeviceController::onState(const MediaState &state)
{
    const MediaState last = std::exchange(m_last, state);
    // The record's music is up: a new track with its cover, or the same track started over
    // (the record's playlist was already playing). Repainted in the same frame as the cover.
    const bool newTrack = state.trackKey() != m_pendingFromTrack && !state.cover.isNull();
    const bool restarted = state.trackKey() == m_pendingFromTrack && m_pendingFromMs > kSeekJumpMs
                           && state.positionNow() < kSeekJumpMs;
    if (m_pendingColor && (newTrack || restarted))
        applyPendingColor();
    if (!m_link->isConnected() || !state.available)
        return;

    // A playlist pick made while paused: commit it the moment playback actually resumes, no
    // matter what started it (the widget's own Play button, the device, anything) — but only
    // if it is this exact track resuming, not some other track that started playing first,
    // which would make the stale pick wrong to apply now.
    if (m_pendingPlaylistIndex >= 0 && state.playing) {
        if (!last.playing && last.trackKey() == m_pendingPlaylistFromTrack) {
            m_lastJumpIndex = m_pendingPlaylistIndex;
            playlistJumpTo(std::exchange(m_pendingPlaylistIndex, -1));
        } else {
            m_pendingPlaylistIndex = -1;
        }
    }

    // Right after a device command the player state still lags behind: don't argue with the device.
    const bool settling = m_sinceFromDevice.isValid() && m_sinceFromDevice.elapsed() < kEchoMs;

    const bool playlistMode = AppSettings::instance().faderMode() == AppSettings::FaderMode::Playlist;

    if (state.durationMs > 0 && state.trackKey() != m_sentTrack) { // new track
        m_sentTrack = state.trackKey();
        if (playlistMode) {
            sendPlaylistFaderState();
        } else {
            m_sentPlaying = true; // TRACK_LEN starts the fader
            send(Command::TrackLen, QString::number(std::lround(state.durationMs / 1000.0)));
            if (state.positionNow() > kSeekJumpMs) // started mid-track
                send(Command::MoveTo, QString::number(state.positionNow() / 1000));
            if (!state.playing)
                sendPlaying(false);
        }
        return;
    }

    if (!settling) { // each is a no-op unless it differs from what the device was told
        // Playlist mode: the fader picks a track, it never animates with elapsed time, so the
        // device is always told "paused" regardless of whether the song itself is playing.
        sendPlaying(playlistMode ? false : state.playing);
        sendModes(state);
    }

    // A jump in position that playback alone does not explain is a seek. Meaningless in the
    // playlist fader mode, where the fader tracks which song plays, not where in it.
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

// ---------------------------------------------------------------- playlist fader mode

void DeviceController::sendPlaylistFaderState()
{
    const QString uri = m_spotify->activeUri();
    if (uri != m_playlistContextUri) {
        m_playlistContextUri = uri;
        m_playlistTracks.clear();
        m_lastJumpIndex = -1; // a previous playlist's index means nothing in this one
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
                return; // the context moved on while this was in flight
            m_playlistTracks = ok ? tracks : QList<Track>();
            sendPlaylistFaderState(); // now armed with the track list
        });
        return;
    }
    if (m_playlistTracks.isEmpty())
        return; // fetch failed, or still pending from the branch above

    const int index = playlistIndexOf(m_media->state().title);
    if (index < 0) {
        qDebug().noquote() << "[fader] Playlist mode: current track" << m_media->state().title
                           << "not found among" << m_playlistTracks.size() << "fetched tracks";
        return;
    }

    // Resent on every switch, not just once per playlist: now that the device knows the mode
    // (SET_MODE), its TRACK_LEN handler only updates the fader's range in playlist mode and
    // never resets the position, so this is safe to repeat — and necessary, since the playlist
    // could have been edited (tracks added/removed) since the last switch.
    send(Command::TrackLen, QString::number(int(m_playlistTracks.size())));
    send(Command::MoveTo, QString::number(index));
    m_sentPlaying = true; // force the PAUSE below out: the device always shows "paused" in this mode
    sendPlaying(false);
}

void DeviceController::playlistTouchStart()
{
    // The hand just landed on the fader: show the preview right away, before the first MOVE_TO.
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
    // Nothing left to commit here: playlistScrub() already switched the track live while it was
    // playing, or deliberately left the pick pending (see m_pendingPlaylistIndex) while paused.
    emit playlistScrubEnded();
}

void DeviceController::playlistScrub(int index)
{
    if (m_playlistContextUri != m_spotify->activeUri() || index < 0 || index >= int(m_playlistTracks.size())) {
        qDebug().noquote() << "[fader] playlistScrub: ignored, index" << index << "tracks"
                           << m_playlistTracks.size() << "context" << m_playlistContextUri
                           << "active" << m_spotify->activeUri();
        return; // no matching track list to preview within yet
    }
    emit playlistScrubMoved(m_playlistTracks, index); // live preview, every MOVE_TO: zero delay
    m_scrubIndex = index;

    if (m_media->state().playing) {
        // Already playing: switch the instant the pick changes, not only once the hand lifts —
        // the point of scrubbing a live playlist is to hear it land, not to wait for the release.
        // Skip repeats while parked on the same track: every drag tick re-reports the index.
        m_pendingPlaylistIndex = -1;
        if (index != m_lastJumpIndex) {
            m_lastJumpIndex = index;
            playlistJumpTo(index);
        }
    } else {
        // Paused: Spotify has no "cue without playing", so don't un-pause it just by scrubbing —
        // remember the pick and only actually switch once Play is pressed (see onState()).
        m_pendingPlaylistIndex = index;
        m_pendingPlaylistFromTrack = m_media->state().trackKey();
    }

    // While the hand is on the fader the preview stays up until FADER_TOUCH "0". Firmware
    // without that signal: hide it once MOVE_TO stops arriving instead.
    if (!m_faderTouched)
        m_scrubTimer.start();
}

void DeviceController::playlistJumpTo(int index)
{
    if (m_playlistContextUri != m_spotify->activeUri() || index < 0 || index >= int(m_playlistTracks.size()))
        return; // no matching track list to jump within yet
    LibraryItem context;
    context.uri = m_playlistContextUri;
    context.kind = *playlistKind(m_playlistContextUri); // set together with m_playlistTracks above

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
