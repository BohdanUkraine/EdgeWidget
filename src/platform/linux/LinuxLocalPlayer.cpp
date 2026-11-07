#include "platform/LocalPlayer.h"

#include "platform/CoverFetcher.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusReply>
#include <QDBusServiceWatcher>
#include <QDBusVariant>

#include <algorithm>

namespace {
const QString kService = QStringLiteral("org.mpris.MediaPlayer2.spotify");
const QString kPath = QStringLiteral("/org/mpris/MediaPlayer2");
const QString kPlayer = QStringLiteral("org.mpris.MediaPlayer2.Player");
const QString kProperties = QStringLiteral("org.freedesktop.DBus.Properties");

QVariantMap toMap(const QVariant &v)
{
    if (v.canConvert<QDBusArgument>()) {
        QVariantMap map;
        v.value<QDBusArgument>() >> map;
        return map;
    }
    return v.toMap();
}

QString joined(const QVariant &v)
{
    if (v.canConvert<QDBusArgument>()) {
        QStringList list;
        v.value<QDBusArgument>() >> list;
        return list.join(QStringLiteral(", "));
    }
    return v.toStringList().join(QStringLiteral(", "));
}

class Watcher : public QObject
{
    Q_OBJECT

public:
    explicit Watcher(std::function<void()> notify)
        : m_notify(std::move(notify))
    {
        QDBusConnection bus = QDBusConnection::sessionBus();
        bus.connect(kService, kPath, kProperties, QStringLiteral("PropertiesChanged"), this, SLOT(changed()));
        bus.connect(kService, kPath, kPlayer, QStringLiteral("Seeked"), this, SLOT(changed()));
        auto *services = new QDBusServiceWatcher(kService, bus, QDBusServiceWatcher::WatchForOwnerChange, this);
        connect(services, &QDBusServiceWatcher::serviceOwnerChanged, this, [this] { m_notify(); });
    }

public slots:
    void changed() { m_notify(); }

private:
    std::function<void()> m_notify;
};

class LinuxLocalPlayer final : public LocalPlayer
{
public:
    explicit LinuxLocalPlayer(std::function<void()> notify)
        : m_watcher(std::move(notify))
    {
    }

    void reset() override {}

    MediaState poll() override
    {
        MediaState st;
        QDBusConnection bus = QDBusConnection::sessionBus();
        if (!bus.interface() || !bus.interface()->isServiceRegistered(kService))
            return st;

        QDBusMessage call = QDBusMessage::createMethodCall(kService, kPath, kProperties, QStringLiteral("GetAll"));
        call << kPlayer;
        const QDBusReply<QVariantMap> reply = bus.call(call);
        if (!reply.isValid())
            return st;
        const QVariantMap p = reply.value();
        const QVariantMap meta = toMap(p.value(QStringLiteral("Metadata")));
        const QString status = p.value(QStringLiteral("PlaybackStatus")).toString();
        if (status == QLatin1String("Stopped") || meta.isEmpty())
            return st;

        st.available = true;
        st.playing = status == QLatin1String("Playing");
        st.title = meta.value(QStringLiteral("xesam:title")).toString();
        st.artist = joined(meta.value(QStringLiteral("xesam:artist")));
        st.album = meta.value(QStringLiteral("xesam:album")).toString();
        st.durationMs = std::max<qint64>(0, meta.value(QStringLiteral("mpris:length")).toLongLong() / 1000);
        st.sampledAtMs = MediaState::nowMs();
        st.shuffle = p.value(QStringLiteral("Shuffle")).toBool();
        const QString loop = p.value(QStringLiteral("LoopStatus")).toString();
        st.repeat = loop == QLatin1String("Track") ? 1 : loop == QLatin1String("Playlist") ? 2 : 0;
        st.cover = m_covers.get(meta.value(QStringLiteral("mpris:artUrl")).toString());
        m_trackId = qvariant_cast<QDBusObjectPath>(meta.value(QStringLiteral("mpris:trackid")));

        const qint64 reported = p.value(QStringLiteral("Position")).toLongLong() / 1000;
        if (reported == 0 && st.trackKey() == m_last.trackKey() && m_last.positionNow() > 0)
            st.positionMs = m_last.positionNow();
        else
            st.positionMs = reported;
        st.positionMs = std::clamp<qint64>(st.positionMs, 0, st.durationMs);
        m_last = st;
        return st;
    }

    void togglePlayPause() override { method(QStringLiteral("PlayPause")); }
    void play() override { method(QStringLiteral("Play")); }
    void next() override { method(QStringLiteral("Next")); }
    void previous() override { method(QStringLiteral("Previous")); }

    void setShuffle(bool on) override { setProperty(QStringLiteral("Shuffle"), on); }

    void setRepeat(int mode) override
    {
        static const char *names[] = { "None", "Track", "Playlist" };
        setProperty(QStringLiteral("LoopStatus"), QString::fromLatin1(names[std::clamp(mode, 0, 2)]));
    }

    void seek(qint64 positionMs) override
    {
        QDBusInterface player(kService, kPath, kPlayer, QDBusConnection::sessionBus());
        player.call(QStringLiteral("SetPosition"), QVariant::fromValue(m_trackId), positionMs * 1000);
        m_last.positionMs = positionMs;
        m_last.sampledAtMs = MediaState::nowMs();
    }

private:
    void method(const QString &name)
    {
        QDBusInterface player(kService, kPath, kPlayer, QDBusConnection::sessionBus());
        player.call(name);
    }

    void setProperty(const QString &name, const QVariant &value)
    {
        QDBusMessage call = QDBusMessage::createMethodCall(kService, kPath, kProperties, QStringLiteral("Set"));
        call << kPlayer << name << QVariant::fromValue(QDBusVariant(value));
        QDBusConnection::sessionBus().call(call);
    }

    Watcher m_watcher;
    CoverFetcher m_covers;
    QDBusObjectPath m_trackId;
    MediaState m_last;
};
}

std::unique_ptr<LocalPlayer> LocalPlayer::create(std::function<void()> notify)
{
    return std::make_unique<LinuxLocalPlayer>(std::move(notify));
}

#include "LinuxLocalPlayer.moc"
