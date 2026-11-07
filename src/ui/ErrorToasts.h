#pragma once

#include "ui/ScaledWidget.h"

#include <QElapsedTimer>
#include <QList>
#include <QTimer>

class ErrorToasts : public ScaledWidget
{
    Q_OBJECT

public:
    explicit ErrorToasts(QWidget *parent);

    void setAnchor(const QPoint &topLeft, int width);
    void showError(const QString &message);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void hideEvent(QHideEvent *event) override;

private:
    struct Card
    {
        QString text;
        int count = 1;
        bool closing = false;
        qreal y = 0.0;
        qreal inset = 0.0;
        qreal opacity = 0.0;
    };
    struct Target
    {
        qreal y;
        qreal inset;
        qreal opacity;
    };

    Target target(int index) const;
    void animate();
    void tick();
    void fitToCards();
    void setExpanded(bool expanded);
    void updateHover(const QPoint &pos);
    int cardAt(const QPoint &pos) const;
    QRectF cardRect(const Card &card) const;
    QRectF closeRect(const QRectF &card) const;
    QRectF textRect(const Card &card, const QRectF &rect, const QFont &font) const;
    QFont textFont() const;

    QList<Card> m_cards;
    bool m_expanded = false;
    int m_hover = -1;
    bool m_hoverClose = false;
    QTimer m_tick;
    QElapsedTimer m_clock;
    QPoint m_anchor;
    int m_width = 0;
};
