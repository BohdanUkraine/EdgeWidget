#pragma once

#include "ui/library/LibraryStyle.h"
#include "ui/ScaledWidget.h"

#include <QVariantAnimation>

// The user's Spotify library (Liked Songs, playlists, albums, artists), styled like the
// "Your Library" sidebar.
//  - cover click: play / pause that item
//  - name click: open it; the other rows fade out while TrackListPopup lifts the row up
//    and shows its tracks
//  - button on the right: write the item to an NFC record (placeholder for now)
class LibraryView : public ScaledWidget
{
    Q_OBJECT

public:
    LibraryView(SpotifyClient *spotify, CoverCache *covers, QWidget *parent = nullptr);

    void setPlaying(bool playing);
    // fadeHeader: the opened row fades out with the card instead of moving back into the list.
    void closeDetail(bool fadeHeader = false);

signals:
    void togglePlayRequested(); // clicked the cover of the item that is already playing
    void detailOpened(const LibraryItem &item, int rowTopGlobal);
    void detailClosed(bool fadeHeader);
    void writeRecordRequested(const LibraryItem &item); // NFC button: put this on a record

protected:
    void paintEvent(QPaintEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    struct Hover
    {
        int index = -1;
        LibraryStyle::RowPart part = LibraryStyle::RowPart::None;
    };

    QRect rowRect(int index) const;
    int maxScroll() const;
    Hover hitTest(const QPoint &pos) const;
    void openDetail(int index);
    void paintPlaceholder(QPainter &p);

    SpotifyClient *m_spotify;
    CoverCache *m_covers;
    bool m_playing = false;
    LibraryStyle::ScrollGesture m_scroll;
    Hover m_hover;

    // Opened item: while it is open, the other rows are faded out.
    int m_detail = -1;          // library index, -1 = plain list
    bool m_detailOpen = false;  // animation target
    qreal m_detailT = 0.0;      // 0 = list, 1 = other rows faded out
    QVariantAnimation m_detailAnim;
    QString m_detailUri;
};
