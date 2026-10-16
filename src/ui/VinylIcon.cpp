#include "ui/VinylIcon.h"

#include <QPainter>

#include <algorithm>

// Black disc, a couple of grooves, green label, spindle hole and a light reflection.
QImage vinylImage(int size)
{
    QImage img(size, size, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);
    const QPointF c(size / 2.0, size / 2.0);
    const qreal r = size / 2.0 - 0.5;

    p.setPen(QPen(QColor(90, 90, 90), std::max(1.0, size / 32.0)));
    p.setBrush(QColor(12, 12, 12));
    p.drawEllipse(c, r, r);

    if (size >= 24) { // grooves only where they don't turn into mush
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(QColor(255, 255, 255, 45), size / 48.0));
        for (qreal k : { 0.82, 0.68 })
            p.drawEllipse(c, r * k, r * k);
    }

    p.setPen(Qt::NoPen);
    p.setBrush(QColor(30, 215, 96));
    p.drawEllipse(c, r * 0.42, r * 0.42);
    p.setBrush(QColor(12, 12, 12));
    p.drawEllipse(c, r * 0.1, r * 0.1);

    p.setPen(QPen(QColor(255, 255, 255, 90), std::max(1.0, size / 24.0), Qt::SolidLine, Qt::RoundCap));
    p.setBrush(Qt::NoBrush);
    p.drawArc(QRectF(c.x() - r * 0.75, c.y() - r * 0.75, r * 1.5, r * 1.5), 100 * 16, 50 * 16);
    return img;
}
