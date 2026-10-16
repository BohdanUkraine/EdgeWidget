#include "device/DeviceController.h"

#include "device/DeviceLink.h"
#include "playback/MediaController.h"
#include "spotify/SpotifyClient.h"

#include <algorithm>
#include <cmath>
#include <utility>

using MediaSpin::Command;

namespace {
constexpr qint64 kEchoMs = 2000;       // the player state lags this long behind a device command
constexpr qint64 kSeekJumpMs = 2500;   // position jump that counts as a seek
constexpr qint64 kAnnounceMs = 1000;   // the device says what lies on the reader right after connecting
} // namespace

DeviceController::DeviceController(MediaController *media, SpotifyClient *spotify, QObject *parent)
    : QObject(parent)
    , m_media(media)
    , m_spotify(spotify)
    , m_link(new DeviceLink(this))
{
    connect(m_link, &DeviceLink::messageReceived, this, &DeviceController::onMessage);
    connect(m_link, &DeviceLink::connectedChanged, this, [this](bool connected) {
        m_sinceConnected.start();
        m_sentTrack.clear();
        m_sentPlaying.reset();
        m_sentShuffle.reset();
        m_sentRepeat.reset();
        if (connected)
            syncDevice();
        else
            setRecord(false, {});
        emit connectedChanged(connected);
    });
    connect(m_media, &MediaController::stateChanged, this, &DeviceController::onState);
    m_last = m_media->state();
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
        setRecord(true, color); // an empty record still changes the colour
        const QString context = SpotifyClient::contextUri(uri);
        // Right after connecting it only says what already lies there: just the colour, keep the music.
        const bool announce = m_sinceConnected.elapsed() < kAnnounceMs;
        if (!context.isEmpty() && !announce)
            m_spotify->play(context);
        break;
    }
    case Command::Pause: m_sentPlaying = false; m_media->setPlaying(false); break; // the device knows already
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
    case Command::MoveTo: m_media->seek(qint64(message.data.toDouble() * 1000)); break;
    case Command::SetVolume: m_media->setVolume(std::clamp(message.data.toInt(), 0, 100)); break;
    default:
        break;
    }
}

// ---------------------------------------------------------------- app -> device

void DeviceController::onState(const MediaState &state)
{
    const MediaState last = std::exchange(m_last, state);
    if (!m_link->isConnected() || !state.available)
        return;

    // Right after a device command the player state still lags behind: don't argue with the device.
    const bool settling = m_sinceFromDevice.isValid() && m_sinceFromDevice.elapsed() < kEchoMs;

    if (state.durationMs > 0 && state.trackKey() != m_sentTrack) { // new track
        m_sentTrack = state.trackKey();
        m_sentPlaying = true; // TRACK_LEN starts the fader
        send(Command::TrackLen, QString::number(std::lround(state.durationMs / 1000.0)));
        if (state.positionNow() > kSeekJumpMs) // started mid-track
            send(Command::MoveTo, QString::number(state.positionNow() / 1000));
        if (!state.playing)
            sendPlaying(false);
        return;
    }

    if (!settling) { // each is a no-op unless it differs from what the device was told
        sendPlaying(state.playing);
        sendModes(state);
    }

    // A jump in position that playback alone does not explain is a seek.
    if (!settling && state.durationMs > 0 && std::abs(state.positionNow() - last.positionNow()) > kSeekJumpMs) {
        send(Command::MoveTo, QString::number(state.positionNow() / 1000));
    }
}

void DeviceController::syncDevice()
{
    const MediaState &state = m_media->state();
    if (!state.available)
        return;
    sendModes(state);
    if (state.durationMs <= 0)
        return;
    m_sentTrack = state.trackKey();
    m_sentPlaying = true;
    send(Command::TrackLen, QString::number(std::lround(state.durationMs / 1000.0)));
    send(Command::MoveTo, QString::number(state.positionNow() / 1000));
    if (!state.playing)
        sendPlaying(false);
}
