#include "platform/CoverFetcher.h"

#include <QEventLoop>
#include <QNetworkReply>
#include <QTimer>

namespace {
constexpr int kTimeoutMs = 5000;
constexpr int kMaxAttempts = 3;
} // namespace

QImage CoverFetcher::get(const QString &url)
{
    if (url != m_url) {
        m_url = url;
        m_image = {};
        m_attempts = 0;
    }
    if (url.isEmpty() || !m_image.isNull() || m_attempts >= kMaxAttempts)
        return m_image;

    ++m_attempts;
    QNetworkReply *reply = m_nam.get(QNetworkRequest(QUrl(url)));
    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QTimer::singleShot(kTimeoutMs, &loop, &QEventLoop::quit);
    loop.exec();
    if (reply->isFinished() && reply->error() == QNetworkReply::NoError)
        m_image = QImage::fromData(reply->readAll());
    else
        reply->abort();
    reply->deleteLater();
    return m_image;
}
