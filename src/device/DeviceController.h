#pragma once

#include "app/AppSettings.h"
#include "app/MediaState.h"
#include "device/MediaSpinProtocol.h"
#include "spotify/SpotifyClient.h" // Track, LibraryItem: the playlist fader mode jumps by index

#include <QColor>
#include <QElapsedTimer>
#include <QList>
#include <QObject>
#include <QTimer>

#include <optional>

class DeviceLink;
class MediaController;

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
    void deviceError(const QString &message); // the player says a request failed (text for the user)
    // Playlist fader mode, while the device fader is moving: a live preview, not a commitment yet.
    void playlistScrubMoved(const QList<Track> &tracks, int index);
    void playlistScrubEnded(); // settled: the index above is now the real, committed track
    void spotifyVolumeChanged(int percent); // the knob, while it controls Spotify (nothing else shows it)

private:
    void onMessage(const MediaSpin::Message &message);
    void onState(const MediaState &state);
    void setRecord(bool present, const QColor &color);
    void applyPendingColor();
    void syncDevice(); // full state after (re)connecting
    void send(MediaSpin::Command command, const QString &data = {});
    void sendPlaying(bool playing);
    void sendModes(const MediaState &state); // shuffle / repeat lamps

    // Playlist fader mode: TRACK_LEN carries the playlist's track count instead of a duration,
    // and MOVE_TO carries a track index instead of a seek position (no new protocol needed).
    // The device is always told "paused" (the fader never animates with elapsed time) while
    // the song itself keeps playing undisturbed.
    void sendPlaylistFaderState(); // (re)fetches the track list first if needed
    void playlistTouchStart(); // FADER_TOUCH "1": show the preview
    void playlistTouchEnd();   // FADER_TOUCH "0": hide the preview
    void playlistScrub(int index); // MOVE_TO while in Playlist mode: preview now; see below for when it commits
    void playlistJumpTo(int index); // actually switch Spotify to this track (and start it playing)
    int playlistIndexOf(const QString &title) const;

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
    // A record that starts music: its colour waits for the new cover, so both change at once.
    std::optional<QColor> m_pendingColor;
    QString m_pendingFromTrack; // the track playing when the record was put on
    qint64 m_pendingFromMs = 0; // and its position
    QTimer m_pendingTimeout;

    MediaState m_last;              // previous state, to detect changes and seeks
    QElapsedTimer m_sinceFromDevice; // don't send the device's own command back to it

    QString m_playlistContextUri;  // context the fetched tracks below belong to
    QList<Track> m_playlistTracks; // its tracks in order, for the playlist fader mode
    int m_playlistRequestId = 0;   // guards a stale fetchTracks() reply
    AppSettings::FaderMode m_lastFaderMode; // to notice when the setting itself flips

    QTimer m_scrubTimer;  // single-shot: fires once MOVE_TO stops arriving (the fader settled)
    int m_scrubIndex = -1; // last index previewed, so playlistTouchStart() can resume showing it
    int m_lastJumpIndex = -1; // last index actually sent to Spotify: skip repeats while parked on it
    bool m_faderTouched = false; // FADER_TOUCH: the hand is on the fader right now

    // Scrubbed to this track while the song was paused: Spotify has no "cue without playing",
    // so the switch waits for Play instead of yanking the paused song off pause. Committed in
    // onState() the moment playback actually resumes — whichever button or control did it —
    // as long as it is still resuming the very track that was paused when the pick was made.
    int m_pendingPlaylistIndex = -1;
    QString m_pendingPlaylistFromTrack;
};
