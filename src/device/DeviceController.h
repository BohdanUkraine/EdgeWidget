#pragma once

#include "app/MediaState.h"
#include "device/MediaSpinProtocol.h"

#include <QColor>
#include <QElapsedTimer>
#include <QObject>

#include <optional>

class DeviceLink;
class MediaController;
class SpotifyClient;

// Glue between the MediaSpin record player and the app:
//  - device -> app: a record put on the reader sets the vinyl colour and plays its album /
//    playlist (if one is written on it); buttons and the fader control playback
//  - app -> device: track length, pause / resume and seeks keep the motorised fader in sync;
//    shuffle / repeat light the lamps
class DeviceController : public QObject
{
    Q_OBJECT

public:
    DeviceController(MediaController *media, SpotifyClient *spotify, QObject *parent = nullptr);

    bool isConnected() const;
    bool hasRecord() const { return m_hasRecord; }        // a record lies on the reader
    QColor recordColor() const { return m_recordColor; } // invalid: no record (or colour 0)
    bool writeRecord(const QString &uri); // BIND to the record on the reader; false if there is none

signals:
    void connectedChanged(bool connected);
    void recordColorChanged(const QColor &color);

private:
    void onMessage(const MediaSpin::Message &message);
    void onState(const MediaState &state);
    void setRecord(bool present, const QColor &color);
    void syncDevice(); // full state after (re)connecting
    void send(MediaSpin::Command command, const QString &data = {});
    void sendPlaying(bool playing);
    void sendModes(const MediaState &state); // shuffle / repeat lamps

    MediaController *m_media;
    SpotifyClient *m_spotify;
    DeviceLink *m_link;
    bool m_hasRecord = false;
    QElapsedTimer m_sinceConnected; // a SWITCH_TO right after connecting is just the announcement
    QString m_sentTrack;            // key of the track last sent as TRACK_LEN
    std::optional<bool> m_sentPlaying; // what the device was last told: don't repeat PAUSE / RESUME
    std::optional<bool> m_sentShuffle; // lamps on the device
    std::optional<int> m_sentRepeat;
    QColor m_recordColor;

    MediaState m_last;              // previous state, to detect changes and seeks
    QElapsedTimer m_sinceFromDevice; // don't send the device's own command back to it
};
