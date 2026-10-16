#pragma once

#include "app/MediaState.h"
#include "ui/ScaledWidget.h"

#include <QElapsedTimer>
#include <QTimer>

class MediaController;

// The player part of the panel: spinning vinyl with the transport buttons on the left,
// the current track and a seekable progress bar under the library column.
class PlayerView : public ScaledWidget
{
    Q_OBJECT

public:
    explicit PlayerView(MediaController *media, QWidget *parent = nullptr);

    // Colour of the physical record on the player's reader; invalid = classic black vinyl.
    void setRecordColor(const QColor &color);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;

private:
    enum Hit { None, Shuffle, Prev, Play, Next, Repeat, Progress, Vinyl };

    void onState(const MediaState &state);
    void tick();
    void updateSpin();
    qint64 displayedPosition() const;
    Hit hitTest(const QPoint &pos) const;
    QRect buttonRect(Hit button) const;
    QRect progressRect() const;
    qint64 positionAt(int x) const;

    void paintVinyl(QPainter &p);
    void paintNowPlaying(QPainter &p);
    void paintButton(QPainter &p, Hit button);
    void paintProgress(QPainter &p);

    MediaController *m_media;
    MediaState m_state;
    QImage m_cover; // scaled for the vinyl label
    QColor m_recordColor;

    QTimer m_frameTimer; // spins the record while playing
    QElapsedTimer m_sinceFrame;
    qreal m_angle = 0.0;

    Hit m_hover = None;
    bool m_seeking = false;
    qint64 m_seekPreview = 0;
};
