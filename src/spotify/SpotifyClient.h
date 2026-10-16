#pragma once

#include "app/MediaState.h"

#include <QElapsedTimer>
#include <QImage>
#include <QList>
#include <QNetworkAccessManager>
#include <QObject>

#include <functional>

class SpotifyAuth;

// One row of the library list: Liked Songs, a playlist, a saved album or a followed artist.
struct LibraryItem
{
    enum Kind { Liked, Playlist, Album, Artist };

    Kind kind = Album;
    QString uri; // context URI to play
    QString name;
    QString owner; // artists of an album, owner of a playlist
    QString imageUrl;
};

struct Track
{
    QString uri;
    QString name;
    QString artists;
    qint64 durationMs = 0;
    QString imageUrl; // album cover; empty in album track lists (use the album's)
};

// Spotify Web API: the user's library, track lists, starting playback and remote control.
// Login and tokens are handled by SpotifyAuth.
class SpotifyClient : public QObject
{
    Q_OBJECT

public:
    explicit SpotifyClient(QObject *parent = nullptr);

    // Account (see SpotifyAuth)
    QString clientId() const;
    void setClientId(const QString &id);
    bool isConnected() const;
    void login();
    void logout();
    QString userName() const { return m_userName; }

    // Library
    bool isLoading() const { return m_loading; }
    const QList<LibraryItem> &library() const { return m_library; }
    void refreshLibrary();
    void fetchTracks(const LibraryItem &item, std::function<void(const QList<Track> &tracks, bool ok)> done);
    void fetchImage(const QString &url, QObject *context, std::function<void(const QImage &)> done);

    // Playback
    QString activeUri() const { return m_activeUri; } // playlist / album playing right now
    void updateNowPlaying();
    void play(const QString &contextUri); // album / playlist / artist, or a single track
    // spotify:album:ID from an open.spotify.com link (URIs pass through).
    static QString contextUri(const QString &link);
    void playTrack(const LibraryItem &context, const QList<Track> &tracks, int index);
    // What plays on any device (cover not loaded: coverUrl instead).
    void fetchPlayback(std::function<void(const MediaState &state, const QString &coverUrl)> done);
    void playerCommand(const QByteArray &verb, const QString &path); // e.g. PUT /me/player/pause
    void setVolume(int percent); // of the active device; phones refuse it

signals:
    void connectedChanged(bool connected);
    void libraryChanged();
    void activeChanged();
    void error(const QString &message);

private:
    using Reply = std::function<void(int status, const QByteArray &body)>;

    void request(const QByteArray &verb, const QString &path, const QByteArray &body, Reply done, int attempt = 0);
    void fetchPage(const QString &path, LibraryItem::Kind kind, QList<LibraryItem> collected,
                   std::function<void(QList<LibraryItem>)> done);
    void fetchTrackPage(const QString &path, QList<Track> collected, std::function<void(QList<Track>, int status)> done);
    void startPlayback(const QByteArray &body);
    void playOnAnyDevice(const QByteArray &body, int attempt = 0);
    void setActiveUri(const QString &uri);
    void onLoggedOut(bool wasConnected);
    void loadLibraryCache();
    void saveLibraryCache() const;

    QNetworkAccessManager m_nam;
    SpotifyAuth *m_auth;

    QList<LibraryItem> m_library;
    bool m_loading = false;
    QString m_activeUri;
    QElapsedTimer m_volumeRefused; // one "can't change the volume" message per drag, not per step
    QString m_userId;
    QString m_userName;
};
