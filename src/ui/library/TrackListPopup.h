#pragma once

#include "ui/library/LibraryStyle.h"
#include "ui/ScaledWidget.h"

#include <QVariantAnimation>

class TrackListPopup : public ScaledWidget
{
    Q_OBJECT

public:
    TrackListPopup(SpotifyClient *spotify, CoverCache *covers, QWidget *owner);

    void open(const LibraryItem &item, int rowTop);
    void close(bool fadeHeader = false);
    void setArea(const QRect &column, int panelTop, int panelBottom);
    void setNowPlaying(const QString &title, bool playing);

    QString openUri() const { return m_open ? m_item.uri : QString(); }
    void previewIndex(int index);

signals:
    void togglePlayRequested();
    void closeRequested();
    void writeRecordRequested(const LibraryItem &item);

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
        LibraryStyle::RowPart part = LibraryStyle::RowPart::None;
        int track = -1;
    };

    qreal edgeY() const;
    QRect headerRect() const;
    QRect tracksRect() const;
    int maxScroll() const;
    Hover hitTest(const QPoint &pos) const;
    void applyGeometry();
    void paintTracks(QPainter &p, const QRect &list);
    void syncPreviewFromNowTitle();
    void animateBoxTo(qreal target);

    SpotifyClient *m_spotify;
    CoverCache *m_covers;

    LibraryItem m_item;
    QList<Track> m_tracks;
    TracksState m_state = TracksState::Loading;
    int m_requestId = 0;

    bool m_open = false;
    bool m_down = false;
    bool m_fadeHeader = false;
    qreal m_t = 0.0;
    QVariantAnimation m_anim;
    QRect m_column;
    int m_panelTop = 0;
    int m_panelBottom = 0;
    qreal m_rowOffset = 0;

    QString m_nowTitle;
    bool m_playing = false;

    int m_previewIndex = -1;
    bool m_previewPinned = false;
    qreal m_boxY = 0;
    bool m_boxPositioned = false;
    QVariantAnimation m_boxAnim;

    LibraryStyle::ScrollGesture m_scroll;
    bool m_pressInList = false;
    Hover m_hover;

    QVariantAnimation m_scrollAnim;
};
