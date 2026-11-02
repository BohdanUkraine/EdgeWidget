#pragma once

#include "ui/ScaledWidget.h"

#include <QTimer>
#include <QVariantAnimation>

// Small vertical volume scale in the bottom right corner of the screen, shown for a moment
// while the device's knob changes Spotify's volume (the OS shows nothing for that one).
// Never takes focus or clicks, and steps left of the panel rather than covering it.
class VolumeOverlay : public ScaledWidget
{
    Q_OBJECT

public:
    explicit VolumeOverlay(QWidget *owner);

    void showLevel(int percent); // 0..100
    // Where it shows, also while hidden: other popups keep clear of this spot.
    static QRect slot(const QWidget *panel);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    void fadeTo(qreal value);

    int m_percent = 0;
    qreal m_t = 0.0; // 0 = hidden, 1 = fully shown
    QVariantAnimation m_fadeAnim;
    QTimer m_hideTimer;
};
