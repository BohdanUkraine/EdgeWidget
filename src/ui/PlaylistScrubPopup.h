#pragma once

#include "spotify/SpotifyClient.h" // Track
#include "ui/ScaledWidget.h"

#include <QList>
#include <QRect>
#include <QVariantAnimation>

class CoverCache;

// Small floating "wheel" preview shown while the device fader scrubs through a playlist: the
// track nearest the centre is the one that will be picked, with its neighbours above / below.
// It never takes focus and is purely a live preview — DeviceController decides when the move
// is final and actually switches playback; this widget just shows where the fader points.
class PlaylistScrubPopup : public ScaledWidget
{
    Q_OBJECT

public:
    PlaylistScrubPopup(CoverCache *covers, QWidget *owner);

    // The fader points at tracks[index] right now; eases the wheel to it.
    void showPreview(const QList<Track> &tracks, int index);
    void hidePreview();

    // Global rect of the main panel, which screen edge it docks to, and whether it is open right
    // now. Collapsed, it sits low near the screen's own bottom edge (not wherever the handle
    // itself is docked vertically); open, it goes above the panel instead, so it never covers
    // the library list the person is actually looking at.
    void setAnchor(const QRect &panelGlobalRect, bool rightSide, bool panelExpanded);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    void applyGeometry();

    CoverCache *m_covers;

    QList<Track> m_tracks;
    bool m_open = false;
    qreal m_t = 0.0;           // 0 = hidden, 1 = fully shown
    qreal m_centerIndex = 0.0; // animated, fractional while easing between tracks
    QVariantAnimation m_fadeAnim;
    QVariantAnimation m_indexAnim;

    QRect m_panelRect;  // last anchor given by setAnchor()
    bool m_rightSide = true;
    bool m_panelExpanded = false;
    // setAnchor() was called at least once: the panel can legitimately collapse to a 0x0 rect
    // (handle hidden entirely, see EdgePanel::handleHeight()), which QRect considers "null" —
    // this tells that apart from never having been anchored at all.
    bool m_haveAnchor = false;
};
