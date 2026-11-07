#pragma once

#include <QDateTime>
#include <QObject>

#include <functional>

class QNetworkAccessManager;
class QTcpServer;
class QTcpSocket;

class SpotifyAuth : public QObject
{
    Q_OBJECT

public:
    static QString redirectUri();

    SpotifyAuth(QNetworkAccessManager *nam, QObject *parent = nullptr);

    QString clientId() const { return m_clientId; }
    void setClientId(const QString &id);
    bool isConnected() const { return !m_refreshToken.isEmpty(); }

    void login();
    void logout();

    void withToken(std::function<void(const QString &token)> fn); // empty token: offline or signed out
    void invalidateToken() { m_accessToken.clear(); }

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
