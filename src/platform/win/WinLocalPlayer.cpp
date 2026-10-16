// C++/WinRT first: Qt's keyword macros must not leak into the WinRT headers.
#define NOMINMAX
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Media.Control.h>
#include <winrt/Windows.Media.h>
#include <winrt/Windows.Storage.Streams.h>

#include "platform/LocalPlayer.h"

#include <algorithm>
#include <atomic>
#include <chrono>

using namespace winrt::Windows::Media::Control;
using winrt::Windows::Media::MediaPlaybackAutoRepeatMode;
using winrt::Windows::Storage::Streams::DataReader;
using winrt::Windows::Storage::Streams::IRandomAccessStreamReference;

namespace {
QString toQString(const winrt::hstring &s)
{
    return QString::fromWCharArray(s.c_str(), int(s.size()));
}

qint64 toMs(winrt::Windows::Foundation::TimeSpan t)
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(t).count();
}

QImage readThumbnail(const IRandomAccessStreamReference &ref)
{
    if (!ref)
        return {};
    auto stream = ref.OpenReadAsync().get();
    const auto size = uint32_t(stream.Size());
    if (size == 0)
        return {};
    DataReader reader(stream);
    reader.LoadAsync(size).get();
    QByteArray bytes(int(size), Qt::Uninitialized);
    reader.ReadBytes(winrt::array_view<uint8_t>(reinterpret_cast<uint8_t *>(bytes.data()), size));
    return QImage::fromData(bytes);
}

// Spotify through Windows "Global System Media Transport Controls": no login or API keys,
// works with the Spotify desktop app. Event driven.
class WinLocalPlayer final : public LocalPlayer
{
public:
    explicit WinLocalPlayer(std::function<void()> notify)
        : m_notify(std::move(notify))
    {
        try {
            winrt::init_apartment(winrt::apartment_type::multi_threaded);
        } catch (...) {
        }
    }

    ~WinLocalPlayer() override
    {
        reset(); // WinRT objects must go before the apartment
        winrt::uninit_apartment();
    }

    void reset() override
    {
        playbackRevoker = {};
        timelineRevoker = {};
        propsRevoker = {};
        sessionsRevoker = {};
        current = nullptr;
        currentId.clear();
        manager = nullptr;
    }

    MediaState poll() override
    {
        MediaState st;
        auto s = session();
        if (!s)
            return st;
        st.available = true;

        const auto info = s.GetPlaybackInfo();
        st.playing = info.PlaybackStatus() == GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing;
        if (const auto shuffle = info.IsShuffleActive())
            st.shuffle = shuffle.Value();
        if (const auto repeat = info.AutoRepeatMode())
            st.repeat = int(repeat.Value());

        // Players update the timeline rarely, so extrapolate from the last update.
        const auto tl = s.GetTimelineProperties();
        auto pos = tl.Position() - tl.StartTime();
        if (st.playing) {
            const auto since = winrt::clock::now() - tl.LastUpdatedTime();
            if (since.count() > 0)
                pos += since;
        }
        st.durationMs = std::max<qint64>(0, toMs(tl.EndTime() - tl.StartTime()));
        st.positionMs = std::clamp<qint64>(toMs(pos), 0, st.durationMs);
        st.sampledAtMs = MediaState::nowMs();

        // Media properties (and the thumbnail) are the slow part: only re-read on change.
        if (propsDirty.exchange(false) || (cover.isNull() && coverAttempts < 6)) {
            const auto props = s.TryGetMediaPropertiesAsync().get();
            const QString t = toQString(props.Title());
            const QString a = toQString(props.Artist());
            const QString al = toQString(props.AlbumTitle());
            if (t != title || a != artist || al != album) {
                title = t;
                artist = a;
                album = al;
                cover = {};
                coverAttempts = 0;
            }
            // The thumbnail often shows up a moment after the title, so retry a few times.
            if (cover.isNull() && coverAttempts < 6) {
                ++coverAttempts;
                try {
                    cover = readThumbnail(props.Thumbnail());
                } catch (...) {
                }
            }
        }
        st.title = title;
        st.artist = artist;
        st.album = album;
        st.cover = cover;
        return st;
    }

    void togglePlayPause() override { withSession([](auto &s) { s.TryTogglePlayPauseAsync().get(); }); }
    void play() override { withSession([](auto &s) { s.TryPlayAsync().get(); }); }
    void next() override { withSession([](auto &s) { s.TrySkipNextAsync().get(); }); }
    void previous() override { withSession([](auto &s) { s.TrySkipPreviousAsync().get(); }); }
    void setShuffle(bool on) override { withSession([on](auto &s) { s.TryChangeShuffleActiveAsync(on).get(); }); }

    void setRepeat(int mode) override
    {
        withSession([mode](auto &s) { s.TryChangeAutoRepeatModeAsync(MediaPlaybackAutoRepeatMode(mode)).get(); });
    }

    void seek(qint64 positionMs) override
    {
        withSession([positionMs](auto &s) { s.TryChangePlaybackPositionAsync(positionMs * 10'000).get(); }); // 100 ns
    }

private:
    template<typename Fn>
    void withSession(Fn fn)
    {
        if (auto s = session())
            fn(s);
    }

    GlobalSystemMediaTransportControlsSession session()
    {
        if (!manager) {
            manager = GlobalSystemMediaTransportControlsSessionManager::RequestAsync().get();
            sessionsRevoker = manager.SessionsChanged(winrt::auto_revoke, [this](auto &&, auto &&) {
                sessionsDirty = true;
                m_notify();
            });
        }
        // Spotify only: other players (Media Player, a browser video) are not this widget's business,
        // and without a local Spotify the Web API shows what plays on the phone.
        GlobalSystemMediaTransportControlsSession found{ nullptr };
        for (const auto &s : manager.GetSessions()) {
            if (toQString(s.SourceAppUserModelId()).contains("spotify", Qt::CaseInsensitive)) {
                found = s;
                break;
            }
        }
        subscribe(found);
        return found;
    }

    void subscribe(const GlobalSystemMediaTransportControlsSession &s)
    {
        const QString id = s ? toQString(s.SourceAppUserModelId()) : QString();
        if (!sessionsDirty.exchange(false) && s && current && id == currentId)
            return;
        current = s;
        currentId = id;
        propsDirty = true;
        playbackRevoker = {};
        timelineRevoker = {};
        propsRevoker = {};
        if (!s)
            return;
        playbackRevoker = s.PlaybackInfoChanged(winrt::auto_revoke, [this](auto &&, auto &&) { m_notify(); });
        timelineRevoker = s.TimelinePropertiesChanged(winrt::auto_revoke, [this](auto &&, auto &&) { m_notify(); });
        propsRevoker = s.MediaPropertiesChanged(winrt::auto_revoke, [this](auto &&, auto &&) {
            propsDirty = true;
            m_notify();
        });
    }

    std::function<void()> m_notify; // thread-safe "something changed"

    GlobalSystemMediaTransportControlsSessionManager manager{ nullptr };
    GlobalSystemMediaTransportControlsSessionManager::SessionsChanged_revoker sessionsRevoker;

    GlobalSystemMediaTransportControlsSession current{ nullptr };
    QString currentId;
    GlobalSystemMediaTransportControlsSession::PlaybackInfoChanged_revoker playbackRevoker;
    GlobalSystemMediaTransportControlsSession::TimelinePropertiesChanged_revoker timelineRevoker;
    GlobalSystemMediaTransportControlsSession::MediaPropertiesChanged_revoker propsRevoker;

    std::atomic<bool> propsDirty{ true };
    std::atomic<bool> sessionsDirty{ false }; // player restarted: session objects are new
    QString title, artist, album;
    QImage cover;
    int coverAttempts = 0;
};
} // namespace

std::unique_ptr<LocalPlayer> LocalPlayer::create(std::function<void()> notify)
{
    return std::make_unique<WinLocalPlayer>(std::move(notify));
}
