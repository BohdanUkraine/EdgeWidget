#pragma once

#include "app/AppSettings.h"

#include <QPainter>
#include <QWidget>

// Base for the hand-painted views: they lay out and paint in logical units, and the panel
// size setting (uiScale) scales everything at once.
class ScaledWidget : public QWidget
{
public:
    using QWidget::QWidget;

protected:
    qreal lw() const { return width() / uiScale(); }
    qreal lh() const { return height() / uiScale(); }
    QRect logicalRect() const { return QRect(0, 0, int(lw()), int(lh())); }
    QPoint logical(const QPointF &pos) const { return (pos / uiScale()).toPoint(); }

    // Antialiased painter working in logical units.
    static void prepare(QPainter &p)
    {
        p.setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform | QPainter::TextAntialiasing);
        p.scale(uiScale(), uiScale());
    }
};
