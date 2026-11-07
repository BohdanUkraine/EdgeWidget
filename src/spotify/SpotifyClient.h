#pragma once

#include "app/MediaState.h"

#include <QElapsedTimer>
#include <QTimer>
#include <QImage>
#include <QList>
#include <QNetworkAccessManager>
#include <QObject>

#include <functional>

class QJsonObject;
class SpotifyAuth;

struct LibraryItem
{
    enum Kind { Liked, Playlist, Album, Artist };

    Kind kind = Album;
    QString uri;
    QString name;
    QString owner;
    QString imageUrl;
};

struct Track
{
    QString uri;
    QString name;
    QString artists;
    qint64 durationMs = 0;
    QString imageUrl;
};

class SpotifyClient : public QObject
{
    Q_OBJECT

public:
    explicit SpotifyClient(QObject *parent = nullptr);

    QString clientId() const;
    void setClientId(const QString &id);
    bool isConnected() const;
    void login();
    void logout();
    QString userName() const { return m_userName; }

    bool isLoading() const { return m_loading; }
    const QList<LibraryItem> &library() const { return m_library; }
    void refreshLibrary();
    void fetchTracks(const LibraryItem &item, std::function<void(const QList<Track> &tracks, bool ok)> done);
    void fetchImage(const QString &url, QObject *context, std::function<void(const QImage &)> done);

    QString activeUri() const { return m_activeUri; }
    void updateNowPlaying(const QString &currentTitle = {});
    void play(const QString &contextUri);
    static QString contextUri(const QString &link);
    void playTrack(const LibraryItem &context, const QList<Track> &tracks, int index);
    void fetchPlayback(std::function<void(const MediaState &state, const QString &coverUrl)> done);
    void playerCommand(const QByteArray &verb, const QString &path);
    void setVolume(int percent);
    void fetchVolume(std::function<void(int percent)> done); // -1: Spotify runs nowhere

signals:
    void connectedChanged(bool connected);
    void libraryChanged();
    void activeChanged();
    void error(const QString &message);

private:
    using Reply = std::function<void(int status, const QByteArray &body)>;

    void request(const QByteArray &verb, const QString &path, const QByteArray &body, Reply done, int attempt = 0);
    void fetchPage(const QString &path, LibraryItem::Kind kind, QList<LibraryItem> collected,
                   std::function<void(QList<LibraryItem>, bool ok)> done);
    void fetchTrackPage(const QString &path, QList<Track> collected, std::function<void(QList<Track>, int status)> done);
    void startPlayback(const QByteArray &body);
    void playOnAnyDevice(const QByteArray &body, int attempt = 0);
    void setActiveUri(const QString &uri);
    void rememberLastPlayed(const QJsonObject &player);
    void sendVolume();
    void onLoggedOut(bool wasConnected);
    void loadLibraryCache();
    void saveLibraryCache() const;
    void bumpRecent(const QString &uri);
    void applyRecentOrder();

    QNetworkAccessManager m_nam;
    SpotifyAuth *m_auth;

    QList<LibraryItem> m_library;
    QStringList m_recent;
    bool m_loading = false;
    QString m_activeUri;
    QString m_lastContext;
    QString m_lastTrack;
    QElapsedTimer m_volumeRefused;
    QTimer m_volumeThrottle;
    int m_volumeWanted = -1;
    int m_volumeSent = -1;
    QString m_volumeDevice;
    QString m_userId;
    QString m_userName;
};
