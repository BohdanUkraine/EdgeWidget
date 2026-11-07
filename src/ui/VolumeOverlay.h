#pragma once

#include "ui/ScaledWidget.h"

#include <QTimer>
#include <QVariantAnimation>

class VolumeOverlay : public ScaledWidget
{
    Q_OBJECT

public:
    explicit VolumeOverlay(QWidget *owner);

    void showLevel(int percent);
    static QRect slot(const QWidget *panel);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    void fadeTo(qreal value);

    int m_percent = 0;
    qreal m_t = 0.0;
    QVariantAnimation m_fadeAnim;
    QTimer m_hideTimer;
};
