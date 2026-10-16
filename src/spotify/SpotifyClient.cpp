#include "spotify/SpotifyClient.h"

#include "app/Localization.h"
#include "spotify/SpotifyAuth.h"
#include "spotify/SpotifyLauncher.h"

#include <QDir>
#include <QFile>
#include <QDebug>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkDiskCache>
#include <QNetworkReply>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTimer>

#include <algorithm>

namespace {
const QString kApi = QStringLiteral("https://api.spotify.com/v1");
constexpr int kMaxTracks = 500;
constexpr int kDeviceWaitAttempts = 18; // ~18 s for a freshly started Spotify to register

QString libraryCachePath()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/library.json";
}

// Images come largest first; take the smallest one that is still >= 160 px.
QString pickImage(const QJsonArray &images)
{
    QString url;
    for (const QJsonValue &img : images) {
        if (url.isEmpty() || img.toObject()["width"].toInt() >= 160)
            url = img.toObject()["url"].toString();
    }
    return url;
}

QString artistNames(const QJsonArray &artists)
{
    QStringList names;
    for (const QJsonValue &a : artists)
        names << a.toObject()["name"].toString();
    return names.join(", ");
}

QString apiError(int status, const QByteArray &body)
{
    const QString msg = QJsonDocument::fromJson(body).object()["error"].toObject()["message"].toString();
    return QStringLiteral("Spotify %1%2").arg(status).arg(msg.isEmpty() ? QString() : ": " + msg);
}

QByteArray json(const QJsonObject &o)
{
    return QJsonDocument(o).toJson(QJsonDocument::Compact);
}
} // namespace

SpotifyClient::SpotifyClient(QObject *parent)
    : QObject(parent)
    , m_auth(new SpotifyAuth(&m_nam, this))
{
    auto *cache = new QNetworkDiskCache(this); // album covers
    cache->setCacheDirectory(QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/images");
    cache->setMaximumCacheSize(100 * 1024 * 1024);
    m_nam.setCache(cache);

    connect(m_auth, &SpotifyAuth::error, this, &SpotifyClient::error);
    connect(m_auth, &SpotifyAuth::loggedOut, this, &SpotifyClient::onLoggedOut);
    connect(m_auth, &SpotifyAuth::loggedIn, this, [this] {
        emit connectedChanged(true);
        refreshLibrary();
        updateNowPlaying();
    });

    loadLibraryCache();
    if (isConnected()) {
        QTimer::singleShot(0, this, &SpotifyClient::refreshLibrary);
        QTimer::singleShot(0, this, &SpotifyClient::updateNowPlaying);
    }
}

// ---------------------------------------------------------------- account

QString SpotifyClient::clientId() const { return m_auth->clientId(); }
void SpotifyClient::setClientId(const QString &id) { m_auth->setClientId(id); }
bool SpotifyClient::isConnected() const { return m_auth->isConnected(); }
void SpotifyClient::login() { m_auth->login(); }
void SpotifyClient::logout() { m_auth->logout(); }

void SpotifyClient::onLoggedOut(bool wasConnected)
{
    m_library.clear();
    m_activeUri.clear();
    QFile::remove(libraryCachePath());
    emit libraryChanged();
    if (wasConnected)
        emit connectedChanged(false);
}

// ---------------------------------------------------------------- library cache

void SpotifyClient::loadLibraryCache()
{
    QFile f(libraryCachePath());
    if (!f.open(QIODevice::ReadOnly))
        return;
    for (const QJsonValue &v : QJsonDocument::fromJson(f.readAll()).array()) {
        const QJsonObject o = v.toObject();
        m_library.append({ LibraryItem::Kind(o["kind"].toInt()), o["uri"].toString(), o["name"].toString(),
                           o["owner"].toString(), o["image"].toString() });
    }
}

void SpotifyClient::saveLibraryCache() const
{
    QJsonArray arr;
    for (const LibraryItem &i : m_library) {
        arr.append(QJsonObject{ { "kind", int(i.kind) }, { "uri", i.uri }, { "name", i.name },
                                { "owner", i.owner }, { "image", i.imageUrl } });
    }
    QDir().mkpath(QFileInfo(libraryCachePath()).absolutePath());
    QFile f(libraryCachePath());
    if (f.open(QIODevice::WriteOnly))
        f.write(QJsonDocument(arr).toJson(QJsonDocument::Compact));
}

// ---------------------------------------------------------------- requests

// Authorized API call. Retries once after a 401 (token expired early) and waits out 429s.
void SpotifyClient::request(const QByteArray &verb, const QString &path, const QByteArray &body, Reply done, int attempt)
{
    m_auth->withToken([=](const QString &token) {
        QNetworkRequest req(QUrl(path.startsWith("https://") ? path : kApi + path));
        req.setRawHeader("Authorization", "Bearer " + token.toUtf8());
        req.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork);
        req.setAttribute(QNetworkRequest::CacheSaveControlAttribute, false);
        if (!body.isEmpty() || verb == "PUT" || verb == "POST")
            req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

        QNetworkReply *reply = m_nam.sendCustomRequest(req, verb, body);
        connect(reply, &QNetworkReply::finished, this, [=] {
            reply->deleteLater();
            const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            const QByteArray data = reply->readAll();
            if (status >= 400)
                qDebug().noquote() << "[spotify]" << verb << path.left(120) << "->" << status
                                   << reply->rawHeader("Retry-After") << data.simplified().left(200);
            if (status == 401 && attempt == 0) {
                m_auth->invalidateToken();
                request(verb, path, body, done, attempt + 1);
                return;
            }
            if (status == 429 && attempt < 3) {
                const int wait = std::clamp(reply->rawHeader("Retry-After").toInt(), 1, 60);
                QTimer::singleShot(wait * 1000, this, [=] { request(verb, path, body, done, attempt + 1); });
                return;
            }
            if (status == 0)
                emit error(tr("Немає з'єднання зі Spotify: %1").arg(reply->errorString()));
            done(status, data);
        });
    });
}

void SpotifyClient::fetchImage(const QString &url, QObject *context, std::function<void(const QImage &)> done)
{
    QNetworkRequest req{ QUrl(url) };
    req.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::PreferCache);
    QNetworkReply *reply = m_nam.get(req);
    connect(reply, &QNetworkReply::finished, context, [reply, done] {
        reply->deleteLater();
        done(QImage::fromData(reply->readAll()));
    });
}

// ---------------------------------------------------------------- library

void SpotifyClient::refreshLibrary()
{
    if (!isConnected() || m_loading)
        return;
    m_loading = true;
    emit libraryChanged();

    // Spotify sidebar order without "Recents": Liked Songs, playlists, albums, artists.
    request("GET", "/me", {}, [this](int status, const QByteArray &body) {
        if (status == 200) {
            const QJsonObject me = QJsonDocument::fromJson(body).object();
            m_userId = me["id"].toString();
            m_userName = me["display_name"].toString(m_userId);
            // Spotify has no UI-language field; the account country drives "auto" language.
            Localization::instance().setSpotifyCountry(me["country"].toString());
        }
        fetchPage("/me/playlists?limit=50", LibraryItem::Playlist, {}, [this](QList<LibraryItem> playlists) {
            fetchPage("/me/albums?limit=50", LibraryItem::Album, {}, [this, playlists](QList<LibraryItem> albums) {
                fetchPage("/me/following?type=artist&limit=50", LibraryItem::Artist, {},
                          [this, playlists, albums](QList<LibraryItem> artists) {
                              QList<LibraryItem> all;
                              if (!m_userId.isEmpty())
                                  all.append({ LibraryItem::Liked, "spotify:user:" + m_userId + ":collection",
                                               tr("Улюблені треки"), {}, {} });
                              m_library = all + playlists + albums + artists;
                              m_loading = false;
                              saveLibraryCache();
                              emit libraryChanged();
                          });
            });
        });
    });
}

void SpotifyClient::fetchPage(const QString &path, LibraryItem::Kind kind, QList<LibraryItem> collected,
                              std::function<void(QList<LibraryItem>)> done)
{
    request("GET", path, {}, [=](int status, const QByteArray &body) mutable {
        if (status != 200) {
            if ((kind == LibraryItem::Playlist || kind == LibraryItem::Artist) && (status == 401 || status == 403))
                emit error(tr("Щоб бачити плейлисти й виконавців, перепідключи Spotify (налаштування → Spotify)"));
            else if (status != 0)
                emit error(apiError(status, body));
            done(collected); // keep whatever loaded; the rest of the library still shows
            return;
        }
        const QJsonObject root = QJsonDocument::fromJson(body).object();
        // Followed artists are wrapped in {"artists": {...page...}}.
        const QJsonObject page = kind == LibraryItem::Artist ? root["artists"].toObject() : root;
        for (const QJsonValue &v : page["items"].toArray()) {
            const QJsonObject o = v.toObject();
            switch (kind) {
            case LibraryItem::Album: {
                const QJsonObject album = o["album"].toObject();
                collected.append({ kind, album["uri"].toString(), album["name"].toString(),
                                   artistNames(album["artists"].toArray()), pickImage(album["images"].toArray()) });
                break;
            }
            case LibraryItem::Playlist:
                if (!o.isEmpty()) // Spotify returns null for playlists it cannot show
                    collected.append({ kind, o["uri"].toString(), o["name"].toString(),
                                       o["owner"].toObject()["display_name"].toString(),
                                       pickImage(o["images"].toArray()) });
                break;
            case LibraryItem::Artist:
                collected.append({ kind, o["uri"].toString(), o["name"].toString(), {}, pickImage(o["images"].toArray()) });
                break;
            case LibraryItem::Liked:
                break;
            }
        }
        const QString next = page["next"].toString();
        if (!next.isEmpty())
            fetchPage(next, kind, collected, done);
        else
            done(collected);
    });
}

void SpotifyClient::fetchTracks(const LibraryItem &item, std::function<void(const QList<Track> &, bool)> done)
{
    const QString id = item.uri.section(':', 2, 2);
    QString path;
    switch (item.kind) {
    case LibraryItem::Liked: path = "/me/tracks?limit=50"; break;
    case LibraryItem::Album: path = "/albums/" + id + "/tracks?limit=50"; break;
    case LibraryItem::Playlist: path = "/playlists/" + id + "/tracks?limit=100"; break;
    case LibraryItem::Artist: path = "/artists/" + id + "/top-tracks"; break;
    }
    fetchTrackPage(path, {}, [=](QList<Track> tracks, int status) {
        // Newer API versions renamed playlist "tracks" to "items": try that before giving up.
        if (status != 200 && item.kind == LibraryItem::Playlist && tracks.isEmpty()) {
            fetchTrackPage("/playlists/" + id + "/items?limit=100", {}, [done](QList<Track> t, int st) {
                done(t, st == 200 || !t.isEmpty());
            });
            return;
        }
        done(tracks, status == 200 || !tracks.isEmpty());
    });
}

void SpotifyClient::fetchTrackPage(const QString &path, QList<Track> collected,
                                   std::function<void(QList<Track>, int)> done)
{
    request("GET", path, {}, [=](int status, const QByteArray &body) mutable {
        if (status != 200) {
            done(collected, status);
            return;
        }
        const QJsonObject page = QJsonDocument::fromJson(body).object();
        // Album pages list tracks directly, saved tracks / playlists wrap them in "track"
        // (or "item"), artist top tracks come as {"tracks": [...]}.
        const QJsonArray items = page.contains("items") ? page["items"].toArray() : page["tracks"].toArray();
        for (const QJsonValue &v : items) {
            QJsonObject t = v.toObject();
            if (t["track"].isObject())
                t = t["track"].toObject();
            else if (t["item"].isObject())
                t = t["item"].toObject();
            if (t.isEmpty() || (t.contains("type") && t["type"].toString() != "track"))
                continue; // podcast episodes, removed tracks
            collected.append({ t["uri"].toString(), t["name"].toString(), artistNames(t["artists"].toArray()),
                               qint64(t["duration_ms"].toDouble()), pickImage(t["album"].toObject()["images"].toArray()) });
        }
        const QString next = page["next"].toString();
        if (!next.isEmpty() && collected.size() < kMaxTracks)
            fetchTrackPage(next, collected, done);
        else
            done(collected, status);
    });
}

// ---------------------------------------------------------------- playback

void SpotifyClient::setActiveUri(const QString &uri)
{
    if (uri == m_activeUri)
        return;
    m_activeUri = uri;
    emit activeChanged();
}

void SpotifyClient::updateNowPlaying()
{
    if (!isConnected())
        return;
    request("GET", "/me/player", {}, [this](int status, const QByteArray &body) {
        if (status == 200)
            setActiveUri(QJsonDocument::fromJson(body).object()["context"].toObject()["uri"].toString());
    });
}

QString SpotifyClient::contextUri(const QString &link)
{
    static const QRegularExpression web(
        QStringLiteral("open\\.spotify\\.com/(?:intl-[a-z-]+/)?(album|playlist|artist|track)/([A-Za-z0-9]+)"));
    const QRegularExpressionMatch m = web.match(link);
    return m.hasMatch() ? QStringLiteral("spotify:%1:%2").arg(m.captured(1), m.captured(2)) : link.trimmed();
}

void SpotifyClient::play(const QString &contextUri)
{
    if (contextUri.startsWith(QLatin1String("spotify:track:"))) { // a track is not a context
        startPlayback(json({ { "uris", QJsonArray{ contextUri } } }));
        return;
    }
    setActiveUri(contextUri); // highlight right away, the request takes a moment
    // Spotify picks the track (it resumes the context), but always from the start of it.
    startPlayback(json({ { "context_uri", contextUri }, { "position_ms", 0 } }));
}

void SpotifyClient::playTrack(const LibraryItem &context, const QList<Track> &tracks, int index)
{
    if (index < 0 || index >= tracks.size())
        return;
    QJsonObject body;
    if (context.kind == LibraryItem::Artist) {
        // An artist context cannot start at a given track: play the list itself.
        QJsonArray uris;
        for (const Track &t : tracks)
            uris.append(t.uri);
        body["uris"] = uris;
        body["offset"] = QJsonObject{ { "position", index } };
    } else {
        body["context_uri"] = context.uri;
        body["offset"] = QJsonObject{ { "uri", tracks.at(index).uri } };
    }
    setActiveUri(context.uri);
    startPlayback(json(body));
}

void SpotifyClient::startPlayback(const QByteArray &body)
{
    request("PUT", "/me/player/play", body, [this, body](int status, const QByteArray &reply) {
        if (status == 404) {
            playOnAnyDevice(body); // usually nothing is active right now
            return;
        }
        if (status == 403)
            emit error(tr("Spotify не дозволив відтворення (потрібен Premium?)"));
        else if (status >= 300)
            emit error(apiError(status, reply));
        if (status >= 300)
            updateNowPlaying(); // undo the optimistic highlight
    });
}

// Pick a device (preferably this computer). If Spotify runs nowhere, start the desktop
// app quietly and wait for it to register as a device.
void SpotifyClient::playOnAnyDevice(const QByteArray &body, int attempt)
{
    request("GET", "/me/player/devices", {}, [this, body, attempt](int status, const QByteArray &reply) {
        if (status != 200) {
            if (status != 0)
                emit error(apiError(status, reply));
            SpotifyLauncher::instance().finished();
            return;
        }
        QString id;
        for (const QJsonValue &v : QJsonDocument::fromJson(reply).object()["devices"].toArray()) {
            const QJsonObject d = v.toObject();
            const bool computer = d["type"].toString() == "Computer";
            if (id.isEmpty() || computer)
                id = d["id"].toString();
            if (computer)
                break;
        }
        if (id.isEmpty()) {
            if (attempt == 0 && !SpotifyLauncher::instance().launch())
                return;
            if (attempt >= kDeviceWaitAttempts) {
                SpotifyLauncher::instance().finished();
                emit error(tr("Відкрий Spotify на комп'ютері, телефоні чи в браузері"));
                return;
            }
            QTimer::singleShot(1000, this, [this, body, attempt] { playOnAnyDevice(body, attempt + 1); });
            return;
        }
        SpotifyLauncher::instance().finished();
        request("PUT", "/me/player/play?device_id=" + id, body, [this](int st, const QByteArray &r) {
            if (st == 404)
                emit error(tr("Spotify не зміг увімкнути цей плейлист чи альбом: він недоступний"));
            else if (st >= 300)
                emit error(apiError(st, r));
            if (st >= 300)
                updateNowPlaying(); // undo the optimistic highlight
        });
    });
}

void SpotifyClient::fetchPlayback(std::function<void(const MediaState &, const QString &)> done)
{
    if (!isConnected())
        return;
    request("GET", "/me/player", {}, [this, done](int status, const QByteArray &body) {
        MediaState st;
        QString coverUrl;
        if (status == 200) {
            const QJsonObject o = QJsonDocument::fromJson(body).object();
            const QJsonObject item = o["item"].toObject();
            const QJsonObject album = item["album"].toObject();
            const QString repeat = o["repeat_state"].toString();
            st.available = !item.isEmpty();
            st.remote = true;
            st.playing = o["is_playing"].toBool();
            st.shuffle = o["shuffle_state"].toBool();
            st.repeat = repeat == "track" ? 1 : repeat == "context" ? 2 : 0;
            st.positionMs = qint64(o["progress_ms"].toDouble());
            st.durationMs = qint64(item["duration_ms"].toDouble());
            st.sampledAtMs = MediaState::nowMs();
            st.title = item["name"].toString();
            st.artist = artistNames(item["artists"].toArray());
            st.album = album["name"].toString();
            st.deviceName = o["device"].toObject()["name"].toString();
            coverUrl = pickImage(album["images"].toArray());
            setActiveUri(o["context"].toObject()["uri"].toString());
        }
        done(st, coverUrl);
    });
}

void SpotifyClient::playerCommand(const QByteArray &verb, const QString &path)
{
    request(verb, path, {}, [this](int status, const QByteArray &reply) {
        if (status == 403)
            emit error(tr("Spotify не дозволив відтворення (потрібен Premium?)"));
        else if (status >= 300)
            emit error(apiError(status, reply));
    });
}

void SpotifyClient::setVolume(int percent)
{
    request("PUT", QStringLiteral("/me/player/volume?volume_percent=%1").arg(percent), {},
            [this](int status, const QByteArray &reply) {
        if (status == 403) {
            if (!m_volumeRefused.isValid() || m_volumeRefused.elapsed() > 30000) {
                m_volumeRefused.start();
                emit error(tr("Spotify не дає змінювати гучність цього пристрою віддалено (телефон — тільки на ньому самому)"));
            }
        } else if (status >= 300) {
            emit error(apiError(status, reply));
        }
    });
}
