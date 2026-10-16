#pragma once

#include "app/MediaState.h"

#include <QDeadlineTimer>
#include <QObject>
#include <QThread>
#include <QTimer>

#include <atomic>
#include <functional>
#include <memory>
#include <optional>

class LocalPlayer;
class SpotifyClient;

// What plays and how to control it. The Spotify desktop app on this computer goes through
// LocalPlayer (the OS media API, see platform/), on a private worker thread; commands update
// the state optimistically so the UI reacts instantly.
// Without a player on this PC it falls back to the Spotify Web API (music playing on a phone,
// speaker, ...), and if nothing plays anywhere it starts Spotify quietly and then plays.
class MediaController : public QObject
{
    Q_OBJECT

public:
    explicit MediaController(QObject *parent = nullptr);
    ~MediaController() override;

    const MediaState &state() const { return m_state; }
    void setRemote(SpotifyClient *spotify);

public slots:
    void togglePlayPause();
    void next();
    void previous();
    void toggleShuffle();
    void cycleRepeat();
    void seek(qint64 positionMs);
    // Explicit states (the record player's buttons say what they want, not "toggle").
    void setPlaying(bool playing);
    void setShuffle(bool on);
    void setRepeat(int mode); // 0 = off, 1 = track, 2 = list
    void setVolume(int percent); // via Web API; not signed in: Spotify's volume in the OS mixer

signals:
    void stateChanged(const MediaState &state);

private:
    enum class AfterLaunch { None, Play, Next, Previous };

    // Optimistic values shown until the player confirms them (or they time out).
    struct Pending
    {
        std::optional<bool> playing;
        std::optional<bool> shuffle;
        std::optional<int> repeat;
        std::optional<qint64> positionMs; // at positionSetAtMs
        qint64 positionSetAtMs = 0;
        QString trackKey; // position override only applies to this track
        QDeadlineTimer until;
    };

    void scheduleRefresh();                           // any thread
    void refresh();                                   // worker thread
    void command(std::function<void(LocalPlayer &)> fn); // runs fn on the worker thread
    void publish(MediaState state);                   // main thread
    void onLocalState(const MediaState &state);       // main thread
    void pollRemote();
    void scheduleRemotePoll();
    bool remoteActive() const;
    bool needsLaunch(AfterLaunch then);               // true: Spotify is being started first
    void runAfterLaunch(AfterLaunch action);
    void publishOptimistic();

    QThread m_thread;
    QObject *m_worker = nullptr; // lives in m_thread
    std::unique_ptr<LocalPlayer> m_player; // touched only from m_thread
    std::atomic<bool> m_refreshQueued{ false };
    MediaState m_state;
    Pending m_pending;

    SpotifyClient *m_remote = nullptr;
    bool m_localAvailable = false;
    MediaState m_remoteState;
    QString m_remoteImageUrl;
    QImage m_remoteCover;
    QTimer m_remotePoll;
    AfterLaunch m_afterLaunch = AfterLaunch::None;
};
