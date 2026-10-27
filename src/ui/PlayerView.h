#pragma once

#include "app/MediaState.h"
#include "ui/ScaledWidget.h"

#include <QElapsedTimer>
#include <QTimer>

class CoverCache;
class MediaController;

// The player part of the panel: spinning vinyl with the transport buttons on the left,
// the current track and a seekable progress bar under the library column.
class PlayerView : public ScaledWidget
{
    Q_OBJECT

public:
    PlayerView(MediaController *media, CoverCache *covers, QWidget *parent = nullptr);

    // Colour of the physical record on the player's reader; invalid = classic black vinyl.
    void setRecordColor(const QColor &color);

    // The playlist fader just picked this track: show it as the current one right away —
    // title, cover, duration — instead of waiting for Spotify to confirm the switch (which,
    // while paused, may not happen at all until Play is pressed; see DeviceController). Clears
    // itself the moment the real track actually changes to anything, matching or not.
    void setPreview(const QString &title, const QString &artist, const QString &imageUrl, qint64 durationMs);

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
    CoverCache *m_covers;
    MediaState m_state;
    QImage m_cover; // scaled for the vinyl label
    QColor m_recordColor;

    bool m_previewPinned = false;
    QString m_previewTitle;
    QString m_previewArtist;
    QString m_previewImageUrl;
    qint64 m_previewDurationMs = 0;
    QString m_previewBaseTrackKey; // m_state's track key when the preview was set; any change unpins

    QTimer m_frameTimer; // spins the record while playing
    QElapsedTimer m_sinceFrame;
    qreal m_angle = 0.0;

    Hit m_hover = None;
    bool m_seeking = false;
    qint64 m_seekPreview = 0;
};
