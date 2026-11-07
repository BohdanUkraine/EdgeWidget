#pragma once

#include "app/AppSettings.h"
#include "app/MediaState.h"
#include "device/MediaSpinProtocol.h"
#include "spotify/SpotifyClient.h"

#include <QColor>
#include <QElapsedTimer>
#include <QList>
#include <QObject>
#include <QTimer>

#include <optional>

class BleLink;
class MediaController;

class DeviceController : public QObject
{
    Q_OBJECT

public:
    DeviceController(MediaController *media, SpotifyClient *spotify, QObject *parent = nullptr);

    bool isConnected() const;
    bool hasRecord() const { return m_hasRecord; }
    QColor recordColor() const { return m_recordColor; }
    bool writeRecord(const QString &uri);

signals:
    void connectedChanged(bool connected);
    void recordColorChanged(const QColor &color);
    void deviceError(const QString &message);
    void playlistScrubMoved(const QList<Track> &tracks, int index);
    void playlistScrubEnded();
    void spotifyVolumeChanged(int percent);

private:
    void onMessage(const MediaSpin::Message &message);
    void onState(const MediaState &state);
    void setRecord(bool present, const QColor &color);
    void applyPendingColor();
    void syncDevice();
    void send(MediaSpin::Command command, const QString &data = {});
    void sendPlaying(bool playing);
    void sendModes(const MediaState &state);

    void sendPlaylistFaderState();
    void playlistTouchStart();
    void playlistTouchEnd();
    void playlistScrub(int index);
    void playlistJumpTo(int index);
    int playlistIndexOf(const QString &title) const;

    MediaController *m_media;
    SpotifyClient *m_spotify;
    BleLink *m_link;
    bool m_hasRecord = false;
    QElapsedTimer m_sinceConnected;
    QString m_sentTrack;
    std::optional<bool> m_sentPlaying;
    std::optional<bool> m_sentShuffle;
    std::optional<int> m_sentRepeat;
    QColor m_recordColor;
    std::optional<QColor> m_pendingColor;
    QString m_pendingFromTrack;
    qint64 m_pendingFromMs = 0;
    QTimer m_pendingTimeout;

    MediaState m_last;
    QElapsedTimer m_sinceFromDevice;

    QString m_playlistContextUri;
    QList<Track> m_playlistTracks;
    int m_playlistRequestId = 0;
    AppSettings::FaderMode m_lastFaderMode;

    QTimer m_scrubTimer;
    int m_scrubIndex = -1;
    int m_lastJumpIndex = -1;
    bool m_faderTouched = false;

    int m_pendingPlaylistIndex = -1;
    QString m_pendingPlaylistFromTrack;
};
