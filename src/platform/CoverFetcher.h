#pragma once

#include <QImage>
#include <QNetworkAccessManager>
#include <QString>

// Cover art the player gives as an https URL (macOS AppleScript, Linux MPRIS).
// Blocking: for LocalPlayer's worker thread. Fetches each URL once, retries a failed one a few times.
class CoverFetcher
{
public:
    QImage get(const QString &url);

private:
    QNetworkAccessManager m_nam;
    QString m_url;
    QImage m_image;
    int m_attempts = 0;
};
