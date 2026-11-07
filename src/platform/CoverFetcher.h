#pragma once

#include <QImage>
#include <QNetworkAccessManager>
#include <QString>

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
