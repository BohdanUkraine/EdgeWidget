#pragma once

#include "ui/library/LibraryStyle.h"
#include "ui/ScaledWidget.h"

#include <QVariantAnimation>

// The opened album / playlist. One window covers the library column of the panel and the
// space above it (or below it, when the panel sits too high on the screen); together with
// the panel it reads as a single shape.
//  - opening: the card's outer edge slides out of the panel's edge, the clicked row moves
//    from its place in the list to the top of the column, the tracks fade in below it
//  - the track list scrolls all the way to the end of the card
// It is owned by the panel (always above it) and never takes focus, so the panel stays
// open; fully transparent parts let clicks through while it animates.
class TrackListPopup : public ScaledWidget
{
    Q_OBJECT

public:
    TrackListPopup(SpotifyClient *spotify, CoverCache *covers, QWidget *owner);

    // rowTop: global y of the clicked library row (the header starts there).
    void open(const LibraryItem &item, int rowTop);
    // fadeHeader: the header fades out in place instead of moving back to its row.
    void close(bool fadeHeader = false);
    // Global geometry of the library column and the panel's top / bottom edge.
    void setArea(const QRect &column, int panelTop, int panelBottom);
    void setNowPlaying(const QString &title, bool playing);

signals:
    void togglePlayRequested(); // header cover clicked while this item is playing
    void closeRequested();      // header name clicked
    void writeRecordRequested(const LibraryItem &item); // NFC button

protected:
    void paintEvent(QPaintEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    enum class TracksState { Loading, Ready, Failed };

    struct Hover
    {
        LibraryStyle::RowPart part = LibraryStyle::RowPart::None; // header row
        int track = -1;
    };

    qreal edgeY() const; // the panel edge the card grows out of, local logical y
    QRect headerRect() const;
    QRect tracksRect() const;
    int maxScroll() const;
    Hover hitTest(const QPoint &pos) const;
    void applyGeometry();
    void paintTracks(QPainter &p, const QRect &list);

    SpotifyClient *m_spotify;
    CoverCache *m_covers;

    LibraryItem m_item;
    QList<Track> m_tracks;
    TracksState m_state = TracksState::Loading;
    int m_requestId = 0; // drops answers for an album that is no longer open

    bool m_open = false;
    bool m_down = false; // opens below the panel (not enough room above)
    bool m_fadeHeader = false;
    qreal m_t = 0.0;     // 0 = closed (just the row in the list), 1 = fully open
    QVariantAnimation m_anim;
    QRect m_column;      // library column, global
    int m_panelTop = 0;
    int m_panelBottom = 0;
    qreal m_rowOffset = 0; // clicked row's top, logical, relative to the column top

    QString m_nowTitle;
    bool m_playing = false;

    LibraryStyle::ScrollGesture m_scroll;
    bool m_pressInList = false; // only presses in the track list may drag-scroll
    Hover m_hover;
};
