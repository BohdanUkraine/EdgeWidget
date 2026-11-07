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
    void setPlaying(bool playing);
    void setShuffle(bool on);
    void setRepeat(int mode);

signals:
    void stateChanged(const MediaState &state);

private:
    enum class AfterLaunch { None, Play, Next, Previous };

    struct Pending
    {
        std::optional<bool> playing;
        std::optional<bool> shuffle;
        std::optional<int> repeat;
        std::optional<qint64> positionMs;
        qint64 positionSetAtMs = 0;
        QString trackKey;
        QDeadlineTimer until;
    };

    void scheduleRefresh();
    void refresh();
    void command(std::function<void(LocalPlayer &)> fn);
    void publish(MediaState state);
    void onLocalState(const MediaState &state);
    void pollRemote();
    void scheduleRemotePoll();
    bool remoteActive() const;
    bool needsLaunch(AfterLaunch then);
    void runAfterLaunch(AfterLaunch action);
    void publishOptimistic();

    QThread m_thread;
    QObject *m_worker = nullptr;
    std::unique_ptr<LocalPlayer> m_player;
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
