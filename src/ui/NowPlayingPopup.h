#pragma once

#include "spotify/SpotifyClient.h"
#include "ui/ScaledWidget.h"

#include <QColor>
#include <QTimer>
#include <QVariantAnimation>

class CoverCache;

// A record put on the player while the panel is collapsed: what it plays, for a moment, bottom right.
class NowPlayingPopup : public ScaledWidget
{
    Q_OBJECT

public:
    NowPlayingPopup(SpotifyClient *spotify, CoverCache *covers, QWidget *owner);

    void showRecord(const QString &contextUri, const QColor &recordColor);
    void dismiss();

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    void showItem(const LibraryItem &item, const QColor &recordColor);
    void fadeTo(qreal value);
    QRect slot() const;
    void paintRecord(QPainter &p);

    SpotifyClient *m_spotify;
    CoverCache *m_covers;
    LibraryItem m_item;
    QColor m_color;
    int m_request = 0;
    qreal m_t = 0.0;
    QVariantAnimation m_fadeAnim;
    QTimer m_hideTimer;
};
