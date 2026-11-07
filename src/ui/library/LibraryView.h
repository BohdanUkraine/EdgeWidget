#pragma once

#include "ui/library/LibraryStyle.h"
#include "ui/ScaledWidget.h"

#include <QVariantAnimation>

class LibraryView : public ScaledWidget
{
    Q_OBJECT

public:
    LibraryView(SpotifyClient *spotify, CoverCache *covers, QWidget *parent = nullptr);

    void setPlaying(bool playing);
    void closeDetail(bool fadeHeader = false);

signals:
    void togglePlayRequested();
    void detailOpened(const LibraryItem &item, int rowTopGlobal);
    void detailClosed(bool fadeHeader);
    void writeRecordRequested(const LibraryItem &item);

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

    int m_detail = -1;
    bool m_detailOpen = false;
    qreal m_detailT = 0.0;
    QVariantAnimation m_detailAnim;
    QString m_detailUri;
};
