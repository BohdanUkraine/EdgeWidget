#include "spotify/SpotifyAuth.h"

#include "platform/SecretStore.h"

#include <QCryptographicHash>
#include <QDesktopServices>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QRandomGenerator>
#include <QSettings>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QUrlQuery>

#include <utility>

namespace {
const char kScopes[] = "user-library-read playlist-read-private playlist-read-collaborative "
                       "user-follow-read user-read-private user-read-playback-state user-modify-playback-state";
const QString kAccounts = QStringLiteral("https://accounts.spotify.com");
constexpr quint16 kRedirectPort = 8888;
constexpr int kLoginTimeoutMs = 5 * 60 * 1000;
const QString kRefreshTokenKey = QStringLiteral("spotify/refreshToken"); // existing logins stay valid

QString randomString(int length)
{
    static const char chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-._~";
    QString s;
    s.reserve(length);
    for (int i = 0; i < length; ++i)
        s += QLatin1Char(chars[QRandomGenerator::system()->bounded(int(sizeof(chars) - 1))]);
    return s;
}
} // namespace

QString SpotifyAuth::redirectUri()
{
    // Spotify requires a loopback IP literal here, "localhost" is rejected.
    return QStringLiteral("http://127.0.0.1:%1/callback").arg(kRedirectPort);
}

SpotifyAuth::SpotifyAuth(QNetworkAccessManager *nam, QObject *parent)
    : QObject(parent)
    , m_nam(nam)
{
    QSettings s;
    m_clientId = s.value("spotify/clientId").toString();
    m_refreshToken = SecretStore::load(kRefreshTokenKey);
}

void SpotifyAuth::setClientId(const QString &id)
{
    if (id == m_clientId)
        return;
    logout();
    m_clientId = id;
    QSettings().setValue("spotify/clientId", id);
}

void SpotifyAuth::saveRefreshToken() const
{
    SecretStore::save(kRefreshTokenKey, m_refreshToken);
}

void SpotifyAuth::logout()
{
    const bool was = isConnected();
    m_refreshToken.clear();
    m_accessToken.clear();
    saveRefreshToken();
    emit loggedOut(was);
}

// ---------------------------------------------------------------- login

void SpotifyAuth::login()
{
    if (m_clientId.isEmpty()) {
        emit error(tr("Спершу вкажи Client ID"));
        return;
    }

    stopServer();
    m_server = new QTcpServer(this);
    if (!m_server->listen(QHostAddress::LocalHost, kRedirectPort)) {
        emit error(tr("Порт %1 зайнятий іншою програмою").arg(kRedirectPort));
        stopServer();
        return;
    }
    connect(m_server, &QTcpServer::newConnection, this, [this] {
        while (QTcpSocket *socket = m_server ? m_server->nextPendingConnection() : nullptr) {
            connect(socket, &QTcpSocket::readyRead, this, [this, socket] { onCallback(socket); });
            connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        }
    });

    m_verifier = randomString(64);
    m_state = randomString(16);
    const QByteArray challenge = QCryptographicHash::hash(m_verifier.toLatin1(), QCryptographicHash::Sha256)
                                     .toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);

    QUrlQuery q;
    q.addQueryItem("client_id", m_clientId);
    q.addQueryItem("response_type", "code");
    q.addQueryItem("redirect_uri", redirectUri());
    q.addQueryItem("code_challenge_method", "S256");
    q.addQueryItem("code_challenge", QString::fromLatin1(challenge));
    q.addQueryItem("state", m_state);
    q.addQueryItem("scope", QString::fromLatin1(kScopes));
    QUrl url(kAccounts + "/authorize");
    url.setQuery(q);
    QDesktopServices::openUrl(url);

    QTimer::singleShot(kLoginTimeoutMs, m_server, [this] { stopServer(); });
}

void SpotifyAuth::stopServer()
{
    if (m_server) {
        m_server->close();
        m_server->deleteLater();
        m_server = nullptr;
    }
}

// The browser comes back to http://127.0.0.1:8888/callback?code=...&state=...
void SpotifyAuth::onCallback(QTcpSocket *socket)
{
    const QByteArray head = socket->peek(8192);
    if (!head.contains("\r\n"))
        return; // request line not complete yet
    const QList<QByteArray> requestLine = head.left(head.indexOf("\r\n")).split(' ');
    socket->readAll();

    const QUrl url(QStringLiteral("http://127.0.0.1") + QString::fromLatin1(requestLine.value(1)));
    const QUrlQuery q(url);
    if (url.path() != "/callback") {
        socket->write("HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
        socket->disconnectFromHost();
        return;
    }

    const QString code = q.queryItemValue("code");
    const bool ok = !code.isEmpty() && q.queryItemValue("state") == m_state;
    const QByteArray page = QStringLiteral(
        "<!doctype html><meta charset=utf-8><title>EdgeWidget</title>"
        "<body style='font-family:sans-serif;background:#000;color:#1ed760;text-align:center;padding-top:80px'>"
        "<h2>%1</h2><p style='color:#ccc'>%2</p>")
        .arg(ok ? tr("Spotify підключено") : tr("Не вдалося підключити Spotify"),
             ok ? tr("Цю вкладку можна закрити.") : q.queryItemValue("error"))
        .toUtf8();
    socket->write("HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nConnection: close\r\nContent-Length: "
                  + QByteArray::number(page.size()) + "\r\n\r\n" + page);
    socket->disconnectFromHost();
    stopServer();

    if (!ok) {
        emit error(tr("Вхід у Spotify скасовано"));
        return;
    }
    postToken({ { "grant_type", "authorization_code" },
                { "code", code },
                { "redirect_uri", redirectUri() },
                { "client_id", m_clientId },
                { "code_verifier", m_verifier } },
              [this](bool success) {
                  if (success)
                      emit loggedIn();
              });
}

// ---------------------------------------------------------------- tokens

void SpotifyAuth::postToken(const QList<QPair<QString, QString>> &form, std::function<void(bool)> done)
{
    QByteArray body;
    for (const auto &[key, value] : form) {
        if (!body.isEmpty())
            body += '&';
        body += QUrl::toPercentEncoding(key) + '=' + QUrl::toPercentEncoding(value);
    }

    QNetworkRequest req(QUrl(kAccounts + "/api/token"));
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");
    req.setAttribute(QNetworkRequest::CacheSaveControlAttribute, false);
    QNetworkReply *reply = m_nam->post(req, body);
    connect(reply, &QNetworkReply::finished, this, [this, reply, done] {
        reply->deleteLater();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QJsonObject o = QJsonDocument::fromJson(reply->readAll()).object();
        if (status != 200 || !o.contains("access_token")) {
            const QString reason = o.value("error_description").toString(o.value("error").toString());
            emit error(tr("Spotify: не вдалося отримати токен (%1)").arg(reason.isEmpty() ? reply->errorString() : reason));
            if (o.value("error").toString() == "invalid_grant")
                logout(); // refresh token revoked or expired
            done(false);
            return;
        }
        m_accessToken = o["access_token"].toString();
        m_expires = QDateTime::currentDateTimeUtc().addSecs(o["expires_in"].toInt(3600) - 60);
        if (o.contains("refresh_token")) { // Spotify may rotate it
            m_refreshToken = o["refresh_token"].toString();
            saveRefreshToken();
        }
        done(true);
    });
}

void SpotifyAuth::withToken(std::function<void(const QString &)> fn)
{
    if (!m_accessToken.isEmpty() && QDateTime::currentDateTimeUtc() < m_expires) {
        fn(m_accessToken);
        return;
    }
    if (m_refreshToken.isEmpty()) {
        emit error(tr("Spotify не підключено"));
        return;
    }
    // Concurrent callers wait for a single refresh.
    m_tokenWaiters.append([this, fn](bool ok) {
        if (ok)
            fn(m_accessToken);
    });
    if (m_refreshing)
        return;
    m_refreshing = true;
    postToken({ { "grant_type", "refresh_token" }, { "refresh_token", m_refreshToken }, { "client_id", m_clientId } },
              [this](bool ok) {
                  m_refreshing = false;
                  for (const auto &waiter : std::exchange(m_tokenWaiters, {}))
                      waiter(ok);
              });
}
