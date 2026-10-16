#pragma once

#include "ui/ScaledWidget.h"

// Vertical tabs at the right edge of the panel: library (the player) and settings.
class TabStrip : public ScaledWidget
{
    Q_OBJECT

public:
    enum Tab { Library, Settings, None };

    explicit TabStrip(QWidget *parent = nullptr);
    void setCurrent(Tab tab);

signals:
    void tabClicked(TabStrip::Tab tab);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    QRect tabRect(Tab tab) const;
    Tab tabAt(const QPoint &pos) const;

    Tab m_current = Library;
    Tab m_hover = None;
};
