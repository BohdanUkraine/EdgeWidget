#include "playback/MediaController.h"

#include "platform/AppVolume.h"
#include "platform/LocalPlayer.h"
#include "spotify/SpotifyClient.h"
#include "spotify/SpotifyLauncher.h"

#include <algorithm>

namespace {
constexpr int kFallbackPollMs = 1000; // events do the real work where the OS has them
constexpr int kOptimisticMs = 2500;
} // namespace

MediaController::MediaController(QObject *parent)
    : QObject(parent)
{
    m_worker = new QObject;
    m_worker->moveToThread(&m_thread);
    connect(&m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    m_thread.start();

    QMetaObject::invokeMethod(m_worker, [this] {
        m_player = LocalPlayer::create([this] { scheduleRefresh(); });
        auto *timer = new QTimer(m_worker);
        connect(timer, &QTimer::timeout, m_worker, [this] { refresh(); });
        timer->start(kFallbackPollMs);
        refresh();
    });
}

MediaController::~MediaController()
{
    QMetaObject::invokeMethod(m_worker, [this] { m_player.reset(); }, Qt::BlockingQueuedConnection);
    m_thread.quit();
    m_thread.wait();
}

void MediaController::scheduleRefresh()
{
    // OS events arrive on other threads and in bursts: coalesce into one refresh.
    if (m_refreshQueued.exchange(true))
        return;
    QMetaObject::invokeMethod(m_worker, [this] {
        m_refreshQueued = false;
        refresh();
    });
}

void MediaController::refresh()
{
    if (!m_player)
        return;
    MediaState st;
    try {
        st = m_player->poll();
    } catch (...) {
        m_player->reset(); // player went away; reconnect next time
    }
    QMetaObject::invokeMethod(this, [this, st] { onLocalState(st); });
}

void MediaController::publish(MediaState st)
{
    // Keep optimistic values until the player reports them (or they time out),
    // otherwise a stale poll would make the buttons flicker back.
    Pending &p = m_pending;
    if (p.until.hasExpired()) {
        p = {};
    } else {
        if (p.playing) {
            if (st.playing == *p.playing)
                p.playing.reset();
            else
                st.playing = *p.playing;
        }
        if (p.shuffle) {
            if (st.shuffle == *p.shuffle)
                p.shuffle.reset();
            else
                st.shuffle = *p.shuffle;
        }
        if (p.repeat) {
            if (st.repeat == *p.repeat)
                p.repeat.reset();
            else
                st.repeat = *p.repeat;
        }
        if (p.positionMs) {
            const qint64 now = MediaState::nowMs();
            const qint64 expected = *p.positionMs + (st.playing ? now - p.positionSetAtMs : 0);
            if (st.trackKey() != p.trackKey || std::abs(st.positionNow() - expected) < 1500) {
                p.positionMs.reset();
            } else {
                st.positionMs = expected;
                st.sampledAtMs = now;
            }
        }
    }
    m_state = st;
    emit stateChanged(m_state);
}

void MediaController::publishOptimistic()
{
    m_pending.until = QDeadlineTimer(kOptimisticMs);
    emit stateChanged(m_state);
}

void MediaController::command(std::function<void(LocalPlayer &)> fn)
{
    QMetaObject::invokeMethod(m_worker, [this, fn = std::move(fn)] {
        if (!m_player)
            return;
        try {
            fn(*m_player);
        } catch (...) {
            m_player->reset();
        }
        scheduleRefresh();
    });
}

// ---------------------------------------------------------------- routing: local / remote / launch

void MediaController::setRemote(SpotifyClient *spotify)
{
    m_remote = spotify;
    m_remotePoll.setInterval(3000);
    connect(&m_remotePoll, &QTimer::timeout, this, &MediaController::pollRemote);
}

void MediaController::onLocalState(const MediaState &state)
{
    m_localAvailable = state.available;
    if (state.available) {
        m_remotePoll.stop();
        m_remoteState = {};
        if (!state.title.isEmpty()) // keep "Starting Spotify…" until it reports a track
            SpotifyLauncher::instance().finished();
        if (m_afterLaunch != AfterLaunch::None) {
            // Spotify just came up after we started it: do what the user clicked.
            const AfterLaunch action = std::exchange(m_afterLaunch, AfterLaunch::None);
            QTimer::singleShot(700, this, [this, action] { runAfterLaunch(action); });
        }
        publish(state);
        return;
    }
    // Nothing plays on this PC: maybe on a phone or speaker? Ask the Web API.
    if (m_remote && m_remote->isConnected() && !m_remotePoll.isActive()) {
        m_remotePoll.start();
        pollRemote();
    }
    publish(m_remoteState.available ? m_remoteState : state);
}

void MediaController::pollRemote()
{
    if (!m_remote || !m_remote->isConnected() || m_localAvailable)
        return;
    m_remote->fetchPlayback([this](MediaState st, const QString &coverUrl) {
        if (m_localAvailable)
            return; // a local player showed up meanwhile; it wins
        if (coverUrl != m_remoteImageUrl) {
            m_remoteImageUrl = coverUrl;
            m_remoteCover = {};
            if (!coverUrl.isEmpty()) {
                m_remote->fetchImage(coverUrl, this, [this, coverUrl](const QImage &img) {
                    if (coverUrl != m_remoteImageUrl || m_localAvailable)
                        return;
                    m_remoteCover = img;
                    m_remoteState.cover = img;
                    publish(m_remoteState);
                });
            }
        }
        st.cover = m_remoteCover;
        m_remoteState = st;
        publish(st);
    });
}
void MediaController::scheduleRemotePoll()
{
    QTimer::singleShot(600, this, &MediaController::pollRemote);
}

bool MediaController::remoteActive() const
{
    return !m_localAvailable && m_remote && m_remote->isConnected() && m_remoteState.available;
}

// Neither a local player nor a remote device: start Spotify quietly, act once it is up.
bool MediaController::needsLaunch(AfterLaunch then)
{
    if (m_localAvailable || remoteActive())
        return false;
    if (SpotifyLauncher::instance().launch())
        m_afterLaunch = then;
    return true;
}

void MediaController::runAfterLaunch(AfterLaunch action)
{
    command([action](LocalPlayer &p) {
        switch (action) {
        case AfterLaunch::Play: p.play(); break;
        case AfterLaunch::Next: p.next(); break;
        case AfterLaunch::Previous: p.previous(); break;
        case AfterLaunch::None: break;
        }
    });
}

// ---------------------------------------------------------------- commands

void MediaController::togglePlayPause()
{
    if (needsLaunch(AfterLaunch::Play))
        return;
    m_state.positionMs = m_state.positionNow();
    m_state.sampledAtMs = MediaState::nowMs();
    m_state.playing = !m_state.playing;
    m_pending.playing = m_state.playing;
    publishOptimistic();
    if (remoteActive()) {
        m_remote->playerCommand("PUT", m_state.playing ? "/me/player/play" : "/me/player/pause");
        scheduleRemotePoll();
        return;
    }
    command([](LocalPlayer &p) { p.togglePlayPause(); });
}

void MediaController::next()
{
    if (needsLaunch(AfterLaunch::Next))
        return;
    m_state.positionMs = 0;
    m_state.sampledAtMs = MediaState::nowMs();
    emit stateChanged(m_state);
    if (remoteActive()) {
        m_remote->playerCommand("POST", "/me/player/next");
        scheduleRemotePoll();
        return;
    }
    command([](LocalPlayer &p) { p.next(); });
}

void MediaController::previous()
{
    if (needsLaunch(AfterLaunch::Previous))
        return;
    m_state.positionMs = 0;
    m_state.sampledAtMs = MediaState::nowMs();
    emit stateChanged(m_state);
    if (remoteActive()) {
        m_remote->playerCommand("POST", "/me/player/previous");
        scheduleRemotePoll();
        return;
    }
    command([](LocalPlayer &p) { p.previous(); });
}

void MediaController::toggleShuffle()
{
    if (!m_localAvailable && !remoteActive())
        return;
    const bool shuffle = !m_state.shuffle;
    m_state.shuffle = shuffle;
    m_pending.shuffle = shuffle;
    publishOptimistic();
    if (remoteActive()) {
        m_remote->playerCommand("PUT", QStringLiteral("/me/player/shuffle?state=%1").arg(shuffle ? "true" : "false"));
        scheduleRemotePoll();
        return;
    }
    command([shuffle](LocalPlayer &p) { p.setShuffle(shuffle); });
}

void MediaController::cycleRepeat()
{
    // Spotify order: off -> list -> track -> off
    static constexpr int nextMode[] = { 2, 0, 1 }; // indexed by current mode
    setRepeat(nextMode[std::clamp(m_state.repeat, 0, 2)]);
}

void MediaController::setRepeat(int mode)
{
    if (!m_localAvailable && !remoteActive())
        return;
    m_state.repeat = mode;
    m_pending.repeat = mode;
    publishOptimistic();
    if (remoteActive()) {
        static const char *names[] = { "off", "track", "context" };
        m_remote->playerCommand("PUT", QStringLiteral("/me/player/repeat?state=%1").arg(names[mode]));
        scheduleRemotePoll();
        return;
    }
    command([mode](LocalPlayer &p) { p.setRepeat(mode); });
}

void MediaController::seek(qint64 positionMs)
{
    if (!m_localAvailable && !remoteActive())
        return;
    m_state.positionMs = positionMs;
    m_state.sampledAtMs = MediaState::nowMs();
    m_pending.positionMs = positionMs;
    m_pending.positionSetAtMs = m_state.sampledAtMs;
    m_pending.trackKey = m_state.trackKey();
    publishOptimistic();
    if (remoteActive()) {
        m_remote->playerCommand("PUT", QStringLiteral("/me/player/seek?position_ms=%1").arg(positionMs));
        scheduleRemotePoll();
        return;
    }
    command([positionMs](LocalPlayer &p) { p.seek(positionMs); });
}

void MediaController::setPlaying(bool playing)
{
    // With nothing to control, "play" still starts Spotify (togglePlayPause launches it).
    if (m_state.available ? playing != m_state.playing : playing)
        togglePlayPause();
}

void MediaController::setVolume(int percent)
{
    // The Web API knows which device plays (the desktop app shows a phone's playback too);
    // without it, at least the Spotify app on this PC.
    if (m_remote && m_remote->isConnected())
        m_remote->setVolume(percent);
    else
        AppVolume::setSpotify(percent);
}

void MediaController::setShuffle(bool on)
{
    if (on != m_state.shuffle)
        toggleShuffle();
}
