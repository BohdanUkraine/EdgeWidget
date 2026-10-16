#pragma once

#include <QDateTime>
#include <QObject>

#include <functional>

class QNetworkAccessManager;
class QTcpServer;
class QTcpSocket;

// Spotify login with the user's own developer app ("bring your own Client ID", like
// Home Assistant). OAuth Authorization Code + PKCE: the browser redirects back to a tiny
// server on 127.0.0.1, no client secret is stored, the refresh token is in SecretStore.
class SpotifyAuth : public QObject
{
    Q_OBJECT

public:
    static QString redirectUri(); // must be added to the app in the Spotify dashboard

    SpotifyAuth(QNetworkAccessManager *nam, QObject *parent = nullptr);

    QString clientId() const { return m_clientId; }
    void setClientId(const QString &id); // signs out: tokens belong to the old app
    bool isConnected() const { return !m_refreshToken.isEmpty(); }

    void login(); // opens the browser, finishes on the loopback redirect
    void logout();

    // Runs fn with a valid access token, refreshing it first when needed.
    void withToken(std::function<void(const QString &token)> fn);
    void invalidateToken() { m_accessToken.clear(); } // the API said 401

signals:
    void loggedIn();
    void loggedOut(bool wasConnected);
    void error(const QString &message);

private:
    void onCallback(QTcpSocket *socket);
    void postToken(const QList<QPair<QString, QString>> &form, std::function<void(bool)> done);
    void stopServer();
    void saveRefreshToken() const;

    QNetworkAccessManager *m_nam;
    QTcpServer *m_server = nullptr;
    QString m_verifier;
    QString m_state;

    QString m_clientId;
    QString m_refreshToken;
    QString m_accessToken;
    QDateTime m_expires;
    bool m_refreshing = false;
    QList<std::function<void(bool)>> m_tokenWaiters;
};
