#include "spotify/SpotifyClient.h"

#include "app/Localization.h"
#include "spotify/SpotifyAuth.h"
#include "spotify/SpotifyLauncher.h"
#include "spotify/SpotifyLocalLibrary.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QDebug>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkDiskCache>
#include <QNetworkReply>
#include <QPointer>
#include <QRegularExpression>
#include <QSet>
#include <QSettings>
#include <QStandardPaths>
#include <QSysInfo>
#include <QThread>
#include <QTimer>
#include <QUrl>

#include <algorithm>

namespace {
const QString kApi = QStringLiteral("https://api.spotify.com/v1");
constexpr int kMaxTracks = 500;
constexpr int kDeviceWaitAttempts = 18;
constexpr int kVolumeIntervalMs = 300;

QString libraryCachePath()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/library.json";
}

QString recentPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/recent.json";
}

QString tracklessPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/trackless.json";
}

constexpr int kMaxRecent = 200;
// Playlists up to this size get one request to see whether they hold any tracks (the API
// counts podcast episodes and unavailable items too); bigger ones surely do.
constexpr int kCheckMaxItems = 100;

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
}

SpotifyClient::SpotifyClient(QObject *parent)
    : QObject(parent)
    , m_auth(new SpotifyAuth(&m_nam, this))
{
    auto *cache = new QNetworkDiskCache(this);
    cache->setCacheDirectory(QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/images");
    cache->setMaximumCacheSize(100 * 1024 * 1024);
    m_nam.setCache(cache);

    m_volumeThrottle.setSingleShot(true);
    m_volumeThrottle.setInterval(kVolumeIntervalMs);
    connect(&m_volumeThrottle, &QTimer::timeout, this, [this] {
        if (m_volumeWanted != m_volumeSent)
            sendVolume();
    });

    connect(m_auth, &SpotifyAuth::error, this, &SpotifyClient::error);
    connect(m_auth, &SpotifyAuth::loggedOut, this, &SpotifyClient::onLoggedOut);
    connect(m_auth, &SpotifyAuth::loggedIn, this, [this] {
        emit connectedChanged(true);
        refreshLibrary();
        updateNowPlaying();
    });

    loadLibraryCache();
    QSettings s;
    m_lastContext = s.value("spotify/lastContext").toString();
    m_lastTrack = s.value("spotify/lastTrack").toString();
    if (isConnected()) {
        QTimer::singleShot(0, this, &SpotifyClient::refreshLibrary);
        QTimer::singleShot(0, this, [this] { updateNowPlaying(); });
    }
}

QString SpotifyClient::clientId() const { return m_auth->clientId(); }
void SpotifyClient::setClientId(const QString &id) { m_auth->setClientId(id); }
bool SpotifyClient::isConnected() const { return m_auth->isConnected(); }
void SpotifyClient::login() { m_auth->login(); }
void SpotifyClient::logout() { m_auth->logout(); }

void SpotifyClient::onLoggedOut(bool wasConnected)
{
    m_library.clear();
    m_recent.clear();
    m_trackless.clear();
    m_toCheck.clear();
    m_activeUri.clear();
    m_lastContext.clear();
    m_lastTrack.clear();
    QFile::remove(libraryCachePath());
    QFile::remove(recentPath());
    QFile::remove(tracklessPath());
    QSettings s;
    s.remove("spotify/lastContext");
    s.remove("spotify/lastTrack");
    emit libraryChanged();
    if (wasConnected)
        emit connectedChanged(false);
}

void SpotifyClient::loadLibraryCache()
{
    QFile recent(recentPath());
    if (recent.open(QIODevice::ReadOnly)) {
        for (const QJsonValue &v : QJsonDocument::fromJson(recent.readAll()).array())
            m_recent.append(v.toString());
    }

    QFile checked(tracklessPath());
    if (checked.open(QIODevice::ReadOnly)) {
        const QJsonObject o = QJsonDocument::fromJson(checked.readAll()).object();
        for (auto it = o.begin(); it != o.end(); ++it)
            m_trackless.insert(it.key(), { it->toObject()["snapshot"].toString(), it->toObject()["empty"].toBool() });
    }

    QFile f(libraryCachePath());
    if (!f.open(QIODevice::ReadOnly))
        return;
    for (const QJsonValue &v : QJsonDocument::fromJson(f.readAll()).array()) {
        const QJsonObject o = v.toObject();
        m_library.append({ LibraryItem::Kind(o["kind"].toInt()), o["uri"].toString(), o["name"].toString(),
                           o["owner"].toString(), o["image"].toString(), o["pin"].toInt(-1),
                           o["snapshot"].toString(), -1, o["folder"].toString() });
    }
}

void SpotifyClient::saveLibraryCache() const
{
    QJsonArray arr;
    for (const LibraryItem &i : m_library) {
        arr.append(QJsonObject{ { "kind", int(i.kind) }, { "uri", i.uri }, { "name", i.name }, { "owner", i.owner },
                                { "image", i.imageUrl }, { "pin", i.pin }, { "snapshot", i.snapshot },
                                { "folder", i.folder } });
    }
    QDir().mkpath(QFileInfo(libraryCachePath()).absolutePath());
    QFile f(libraryCachePath());
    if (f.open(QIODevice::WriteOnly))
        f.write(QJsonDocument(arr).toJson(QJsonDocument::Compact));
}

void SpotifyClient::bumpRecent(const QString &uri)
{
    if (uri.isEmpty() || (!m_recent.isEmpty() && m_recent.first() == uri))
        return;
    m_recent.removeAll(uri);
    m_recent.prepend(uri);
    while (m_recent.size() > kMaxRecent)
        m_recent.removeLast();

    QDir().mkpath(QFileInfo(recentPath()).absolutePath());
    QFile f(recentPath());
    if (f.open(QIODevice::WriteOnly))
        f.write(QJsonDocument(QJsonArray::fromStringList(m_recent)).toJson(QJsonDocument::Compact));

    QStringList before;
    for (const LibraryItem &i : m_library)
        before << i.uri;
    applyRecentOrder();
    for (int i = 0; i < m_library.size(); ++i) {
        if (m_library.at(i).uri != before.at(i)) {
            saveLibraryCache();
            emit libraryChanged();
            return;
        }
    }
}

// Pins first, always in their own order; then the most recently played. A folder ranks as
// its most recently played playlist.
void SpotifyClient::applyRecentOrder()
{
    QHash<QString, int> rank;
    for (int i = 0; i < m_recent.size(); ++i)
        rank.insert(m_recent.at(i), i);
    QHash<QString, QString> folderParent;
    for (const LibraryItem &i : std::as_const(m_library)) {
        if (i.kind == LibraryItem::Folder)
            folderParent.insert(i.uri, i.folder);
    }
    for (const LibraryItem &i : std::as_const(m_library)) {
        const int r = rank.value(i.uri, kMaxRecent);
        QString f = i.folder;
        for (int depth = 0; !f.isEmpty() && i.kind != LibraryItem::Folder && depth < 32; ++depth, f = folderParent.value(f)) {
            if (r < rank.value(f, kMaxRecent))
                rank.insert(f, r);
        }
    }
    std::stable_sort(m_library.begin(), m_library.end(), [&rank](const LibraryItem &a, const LibraryItem &b) {
        if ((a.pin >= 0) != (b.pin >= 0))
            return a.pin >= 0;
        if (a.pin >= 0)
            return a.pin < b.pin;
        return rank.value(a.uri, kMaxRecent) < rank.value(b.uri, kMaxRecent);
    });
}

bool SpotifyClient::isTrackless(const LibraryItem &item) const
{
    if (item.kind != LibraryItem::Playlist)
        return false;
    if (item.total == 0)
        return true;
    const auto it = m_trackless.constFind(item.uri);
    return it != m_trackless.cend() && it->first == item.snapshot && it->second;
}

// One playlist at a time, only those not checked at their current snapshot yet.
void SpotifyClient::checkTrackless()
{
    while (!m_toCheck.isEmpty()) {
        const QString uri = m_toCheck.takeFirst();
        const auto it = std::find_if(m_library.cbegin(), m_library.cend(),
                                     [&uri](const LibraryItem &i) { return i.uri == uri; });
        if (it == m_library.cend())
            continue;
        const QString snapshot = it->snapshot;
        fetchTrackPage("/playlists/" + uri.section(':', -1) + "/items?limit=" + QString::number(kCheckMaxItems), {},
                       [this, uri, snapshot](QList<Track> tracks, int status) {
                           if (status == 403 || status == 404) { // not readable here: leave it shown
                               m_trackless.insert(uri, { snapshot, false });
                               saveTrackless();
                           } else if (status == 200) {
                               m_trackless.insert(uri, { snapshot, tracks.isEmpty() });
                               saveTrackless();
                               if (tracks.isEmpty()) {
                                   m_library.erase(std::remove_if(m_library.begin(), m_library.end(),
                                                                  [&uri](const LibraryItem &i) { return i.uri == uri; }),
                                                   m_library.end());
                                   pruneFolders();
                                   saveLibraryCache();
                                   emit libraryChanged();
                               }
                           }
                           if (status != 0)
                               checkTrackless();
                       });
        return;
    }
}

void SpotifyClient::saveTrackless() const
{
    QJsonObject o;
    for (auto it = m_trackless.cbegin(); it != m_trackless.cend(); ++it)
        o.insert(it.key(), QJsonObject{ { "snapshot", it->first }, { "empty", it->second } });
    QDir().mkpath(QFileInfo(tracklessPath()).absolutePath());
    QFile f(tracklessPath());
    if (f.open(QIODevice::WriteOnly))
        f.write(QJsonDocument(o).toJson(QJsonDocument::Compact));
}

void SpotifyClient::readLocalLibrary()
{
    const QString userId = m_userId;
    QPointer<SpotifyClient> self(this);
    QThread *thread = QThread::create([self, userId] {
        const SpotifyLocalLibrary::Data data = SpotifyLocalLibrary::read(userId);
        QMetaObject::invokeMethod(qApp, [self, userId, data] {
            if (self && data.found && self->m_userId == userId)
                self->applyLocalLibrary(data.pinned, data.rootlist);
        });
    });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start(QThread::LowPriority);
}

// Pins and folders go onto the items: a refresh from the Web API, which knows neither,
// carries them over until the cache is read again.
void SpotifyClient::applyLocalLibrary(const QStringList &pinned, const QStringList &rootlist)
{
    const QString collection = "spotify:user:" + m_userId + ":collection";
    QList<LibraryItem> folders;
    QHash<QString, QString> parent;
    QStringList open;
    for (const QString &uri : rootlist) {
        if (uri.startsWith(QLatin1String("spotify:start-group:"))) {
            // spotify:start-group:<id>:<url-encoded name>
            const QString rest = uri.mid(int(qstrlen("spotify:start-group:")));
            QString name = rest.section(':', 1);
            name = QUrl::fromPercentEncoding(name.replace('+', ' ').toUtf8());
            const QString folder = "spotify:user:" + m_userId + ":folder:" + rest.section(':', 0, 0);
            folders.append({ LibraryItem::Folder, folder, name, {}, {}, -1, {}, -1, open.isEmpty() ? QString() : open.last() });
            open.append(folder);
        } else if (uri.startsWith(QLatin1String("spotify:end-group:"))) {
            if (!open.isEmpty())
                open.removeLast();
        } else if (!open.isEmpty()) {
            parent.insert(uri, open.last());
        }
    }

    m_library.erase(std::remove_if(m_library.begin(), m_library.end(),
                                   [](const LibraryItem &i) { return i.kind == LibraryItem::Folder; }),
                    m_library.end());
    m_library += folders;
    for (LibraryItem &i : m_library) {
        const QString key = i.uri == collection ? QStringLiteral("spotify:collection") : i.uri;
        i.pin = int(pinned.indexOf(key));
        if (i.kind != LibraryItem::Folder)
            i.folder = parent.value(i.uri);
    }
    pruneFolders();
    applyRecentOrder();
    saveLibraryCache();
    emit libraryChanged();
}

// Folders whose playlists all got hidden (or never were in the library) go too.
void SpotifyClient::pruneFolders()
{
    for (bool removed = true; removed;) {
        QSet<QString> used;
        for (const LibraryItem &i : std::as_const(m_library))
            used.insert(i.folder);
        const auto end = std::remove_if(m_library.begin(), m_library.end(), [&used](const LibraryItem &i) {
            return i.kind == LibraryItem::Folder && !used.contains(i.uri);
        });
        removed = end != m_library.end();
        m_library.erase(end, m_library.end());
    }
}

void SpotifyClient::request(const QByteArray &verb, const QString &path, const QByteArray &body, Reply done, int attempt)
{
    m_auth->withToken([=](const QString &token) {
        if (token.isEmpty()) {
            done(0, {});
            return;
        }
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

void SpotifyClient::fetchItem(const QString &uri, std::function<void(const LibraryItem &item)> done)
{
    const auto it = std::find_if(m_library.begin(), m_library.end(), [&](const LibraryItem &i) { return i.uri == uri; });
    if (it != m_library.end()) {
        done(*it);
        return;
    }
    static const QHash<QString, LibraryItem::Kind> kinds = {
        { "playlist", LibraryItem::Playlist }, { "album", LibraryItem::Album }, { "artist", LibraryItem::Artist }
    };
    const QString type = uri.section(':', 1, 1);
    const QString id = uri.section(':', 2, 2);
    if (!uri.startsWith(QLatin1String("spotify:")) || !kinds.contains(type) || id.isEmpty()) {
        done({});
        return;
    }
    QString path = "/" + type + "s/" + id;
    if (type == QLatin1String("playlist"))
        path += "?fields=name,images,owner(display_name)";
    request("GET", path, {}, [uri, kind = kinds.value(type), done](int status, const QByteArray &body) {
        if (status != 200) {
            done({});
            return;
        }
        const QJsonObject o = QJsonDocument::fromJson(body).object();
        LibraryItem item;
        item.kind = kind;
        item.uri = uri;
        item.name = o["name"].toString();
        item.imageUrl = pickImage(o["images"].toArray());
        if (kind == LibraryItem::Playlist)
            item.owner = o["owner"].toObject()["display_name"].toString();
        else if (kind == LibraryItem::Album)
            item.owner = artistNames(o["artists"].toArray());
        done(item);
    });
}

void SpotifyClient::refreshLibrary()
{
    if (!isConnected() || m_loading)
        return;
    m_loading = true;
    emit libraryChanged();

    auto finish = [this] {
        m_loading = false;
        emit libraryChanged();
    };
    request("GET", "/me", {}, [this, finish](int status, const QByteArray &body) {
        if (status == 0) {
            finish();
            return;
        }
        if (status == 200) {
            const QJsonObject me = QJsonDocument::fromJson(body).object();
            m_userId = me["id"].toString();
            m_userName = me["display_name"].toString(m_userId);
            Localization::instance().setSpotifyCountry(me["country"].toString());
        }
        fetchPage("/me/playlists?limit=50", LibraryItem::Playlist, {}, [=](QList<LibraryItem> playlists, bool ok) {
            if (!ok)
                return finish();
            fetchPage("/me/albums?limit=50", LibraryItem::Album, {}, [=](QList<LibraryItem> albums, bool ok) {
                if (!ok)
                    return finish();
                fetchPage("/me/following?type=artist&limit=50", LibraryItem::Artist, {},
                          [=](QList<LibraryItem> artists, bool ok) {
                              if (!ok)
                                  return finish();
                              QList<LibraryItem> all;
                              if (!m_userId.isEmpty())
                                  all.append({ LibraryItem::Liked, "spotify:user:" + m_userId + ":collection",
                                               tr("Улюблені треки"), {}, {} });
                              m_toCheck.clear();
                              for (const LibraryItem &p : playlists) {
                                  if (isTrackless(p))
                                      continue;
                                  all.append(p);
                                  const auto checked = m_trackless.constFind(p.uri);
                                  if (p.total > 0 && p.total <= kCheckMaxItems
                                      && (checked == m_trackless.cend() || checked->first != p.snapshot))
                                      m_toCheck.append(p.uri);
                              }
                              // pins and folders: kept until the cache is read again
                              QHash<QString, LibraryItem> old;
                              QList<LibraryItem> folders;
                              for (const LibraryItem &i : std::as_const(m_library)) {
                                  old.insert(i.uri, i);
                                  if (i.kind == LibraryItem::Folder)
                                      folders.append(i);
                              }
                              m_library = all + albums + artists;
                              for (LibraryItem &i : m_library) {
                                  const auto it = old.constFind(i.uri);
                                  if (it != old.cend()) {
                                      i.pin = it->pin;
                                      i.folder = it->folder;
                                  }
                              }
                              m_library += folders;
                              pruneFolders();
                              applyRecentOrder();
                              saveLibraryCache();
                              finish();
                              readLocalLibrary();
                              checkTrackless();
                          });
            });
        });
    });
}

void SpotifyClient::fetchPage(const QString &path, LibraryItem::Kind kind, QList<LibraryItem> collected,
                              std::function<void(QList<LibraryItem>, bool ok)> done)
{
    request("GET", path, {}, [=](int status, const QByteArray &body) mutable {
        if (status != 200) {
            if ((kind == LibraryItem::Playlist || kind == LibraryItem::Artist) && (status == 401 || status == 403))
                emit error(tr("Щоб бачити плейлисти й виконавців, перепідключи Spotify (налаштування → Spotify)"));
            else if (status != 0)
                emit error(apiError(status, body));
            done(collected, status != 0);
            return;
        }
        const QJsonObject root = QJsonDocument::fromJson(body).object();
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
            case LibraryItem::Playlist: {
                if (o.isEmpty())
                    break;
                // newer API versions renamed "tracks" to "items"
                const QJsonObject count = o[o.contains("items") ? "items" : "tracks"].toObject();
                collected.append({ kind, o["uri"].toString(), o["name"].toString(),
                                   o["owner"].toObject()["display_name"].toString(), pickImage(o["images"].toArray()),
                                   -1, o["snapshot_id"].toString(),
                                   count.contains("total") ? count["total"].toInt() : -1 });
                break;
            }
            case LibraryItem::Artist:
                collected.append({ kind, o["uri"].toString(), o["name"].toString(), {}, pickImage(o["images"].toArray()) });
                break;
            case LibraryItem::Liked:
            case LibraryItem::Folder:
                break;
            }
        }
        const QString next = page["next"].toString();
        if (!next.isEmpty())
            fetchPage(next, kind, collected, done);
        else
            done(collected, true);
    });
}

void SpotifyClient::fetchTracks(const LibraryItem &item, std::function<void(const QList<Track> &, bool)> done)
{
    const QString id = item.uri.section(':', -1);
    QString path;
    switch (item.kind) {
    case LibraryItem::Liked: path = "/me/tracks?limit=50"; break;
    case LibraryItem::Album: path = "/albums/" + id + "/tracks?limit=50"; break;
    case LibraryItem::Playlist: path = "/playlists/" + id + "/tracks?limit=100"; break;
    case LibraryItem::Artist: path = "/artists/" + id + "/top-tracks"; break;
    case LibraryItem::Folder: done({}, false); return;
    }
    fetchTrackPage(path, {}, [=](QList<Track> tracks, int status) {
        if (status != 200 && status != 0 && item.kind == LibraryItem::Playlist && tracks.isEmpty()) {
            // newer API versions renamed "tracks" to "items"
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
        const QJsonArray items = page.contains("items") ? page["items"].toArray() : page["tracks"].toArray();
        for (const QJsonValue &v : items) {
            QJsonObject t = v.toObject();
            if (t["track"].isObject())
                t = t["track"].toObject();
            else if (t["item"].isObject())
                t = t["item"].toObject();
            // episodes come as an empty wrapper unless asked for: no uri, skip it like any non-track
            if (t["uri"].toString().isEmpty() || (t.contains("type") && t["type"].toString() != "track"))
                continue;
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

void SpotifyClient::setActiveUri(const QString &uri)
{
    if (uri == m_activeUri)
        return;
    m_activeUri = uri;
    emit activeChanged();
    bumpRecent(uri);
}

void SpotifyClient::updateNowPlaying(const QString &currentTitle)
{
    if (!isConnected())
        return;
    request("GET", "/me/player", {}, [this, currentTitle](int status, const QByteArray &body) {
        if (status == 200) {
            const QJsonObject player = QJsonDocument::fromJson(body).object();
            rememberLastPlayed(player);
            setActiveUri(player["context"].toObject()["uri"].toString());
            return;
        }
        // 204: no active session (paused since before): the remembered playlist, if its track is still loaded
        if (status == 204)
            setActiveUri(currentTitle.isEmpty() || currentTitle == m_lastTrack ? m_lastContext : QString());
    });
}

void SpotifyClient::rememberLastPlayed(const QJsonObject &player)
{
    const QJsonObject item = player["item"].toObject();
    if (item.isEmpty())
        return;
    const QString context = player["context"].toObject()["uri"].toString();
    const QString track = item["name"].toString();
    if (context == m_lastContext && track == m_lastTrack)
        return;
    m_lastContext = context;
    m_lastTrack = track;
    QSettings s;
    s.setValue("spotify/lastContext", context);
    s.setValue("spotify/lastTrack", track);
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
    if (contextUri.startsWith(QLatin1String("spotify:track:"))) {
        startPlayback(json({ { "uris", QJsonArray{ contextUri } } }));
        return;
    }
    setActiveUri(contextUri);
    startPlayback(json({ { "context_uri", contextUri }, { "offset", QJsonObject{ { "position", 0 } } },
                          { "position_ms", 0 } })); // from the top, not where it stopped last time
}

void SpotifyClient::playTrack(const LibraryItem &context, const QList<Track> &tracks, int index)
{
    if (index < 0 || index >= tracks.size())
        return;
    QJsonObject body;
    if (context.kind == LibraryItem::Artist) {
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
            playOnAnyDevice(body);
            return;
        }
        if (status == 403)
            emit error(tr("Spotify не дозволив відтворення (потрібен Premium?)"));
        else if (status >= 300)
            emit error(apiError(status, reply));
        if (status >= 300)
            updateNowPlaying();
    });
}

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
                updateNowPlaying();
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
            rememberLastPlayed(o);
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

// This PC's Spotify among the devices: by name, or the only computer there is while Spotify
// runs here. Never someone else's laptop.
QJsonObject SpotifyClient::localDevice(const QJsonArray &devices)
{
    QList<QJsonObject> computers;
    for (const QJsonValue &v : devices) {
        const QJsonObject d = v.toObject();
        if (d["type"].toString() != "Computer")
            continue;
        if (d["name"].toString().compare(QSysInfo::machineHostName(), Qt::CaseInsensitive) == 0)
            return d;
        computers.append(d);
    }
    return computers.size() == 1 && SpotifyLauncher::isRunning() ? computers.first() : QJsonObject();
}

void SpotifyClient::transferHere(bool play, std::function<void(bool)> done, int attempt)
{
    request("GET", "/me/player/devices", {}, [this, play, done, attempt](int status, const QByteArray &reply) {
        const QJsonObject local =
            status == 200 ? localDevice(QJsonDocument::fromJson(reply).object()["devices"].toArray()) : QJsonObject();
        if (status == 200 && local.isEmpty()) {
            if (attempt == 0 && !SpotifyLauncher::instance().launch()) {
                done(false);
                return;
            }
            if (attempt < kDeviceWaitAttempts) {
                QTimer::singleShot(1000, this, [this, play, done, attempt] { transferHere(play, done, attempt + 1); });
                return;
            }
        }
        if (attempt > 0)
            SpotifyLauncher::instance().finished();
        if (local.isEmpty()) {
            done(false);
            return;
        }
        const QString id = local["id"].toString();
        if (local["is_active"].toBool()) {
            m_volumeDevice = id;
            done(true);
            return;
        }
        qInfo().noquote() << "[spotify] playback moves to this computer:" << local["name"].toString();
        request("PUT", "/me/player", json({ { "device_ids", QJsonArray{ id } }, { "play", play } }),
                [this, id, done](int st, const QByteArray &r) {
                    const bool ok = st > 0 && st < 300;
                    if (ok)
                        m_volumeDevice = id;
                    else if (st != 0)
                        emit error(apiError(st, r));
                    done(ok);
                });
    });
}

// Nothing active (paused for a while): this computer's Spotify is made active, without playing.
void SpotifyClient::fetchVolume(std::function<void(int)> done)
{
    request("GET", "/me/player", {}, [this, done](int status, const QByteArray &body) {
        if (status != 204) {
            const QJsonObject device = QJsonDocument::fromJson(body).object()["device"].toObject();
            const QJsonValue volume = device["volume_percent"];
            m_volumeDevice = device["id"].toString();
            done(status == 200 && volume.isDouble() ? volume.toInt() : -1);
            return;
        }
        request("GET", "/me/player/devices", {}, [this, done](int st, const QByteArray &reply) {
            const QJsonObject local = localDevice(QJsonDocument::fromJson(reply).object()["devices"].toArray());
            const QJsonValue volume = local["volume_percent"];
            if (st != 200 || !volume.isDouble()) {
                done(-1);
                return;
            }
            m_volumeDevice = local["id"].toString();
            request("PUT", "/me/player", json({ { "device_ids", QJsonArray{ m_volumeDevice } }, { "play", false } }),
                    [done, percent = volume.toInt()](int, const QByteArray &) { done(percent); });
        });
    });
}

void SpotifyClient::setVolume(int percent)
{
    m_volumeWanted = percent;
    if (!m_volumeThrottle.isActive())
        sendVolume();
}

void SpotifyClient::sendVolume()
{
    m_volumeSent = m_volumeWanted;
    m_volumeThrottle.start();
    QString path = QStringLiteral("/me/player/volume?volume_percent=%1").arg(m_volumeSent);
    if (!m_volumeDevice.isEmpty())
        path += QStringLiteral("&device_id=") + m_volumeDevice;
    request("PUT", path, {}, [this](int status, const QByteArray &reply) {
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
