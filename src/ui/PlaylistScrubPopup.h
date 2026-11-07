#pragma once

#include "spotify/SpotifyClient.h"
#include "ui/ScaledWidget.h"

#include <QList>
#include <QRect>
#include <QVariantAnimation>

class CoverCache;

class PlaylistScrubPopup : public ScaledWidget
{
    Q_OBJECT

public:
    PlaylistScrubPopup(CoverCache *covers, QWidget *owner);

    void showPreview(const QList<Track> &tracks, int index);
    void hidePreview();

    void setAnchor(const QRect &panelGlobalRect, bool rightSide, bool panelExpanded);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    void applyGeometry();

    CoverCache *m_covers;

    QList<Track> m_tracks;
    bool m_open = false;
    qreal m_t = 0.0;
    qreal m_centerIndex = 0.0;
    QVariantAnimation m_fadeAnim;
    QVariantAnimation m_indexAnim;

    QRect m_panelRect;
    bool m_rightSide = true;
    bool m_panelExpanded = false;
    bool m_haveAnchor = false;
};
