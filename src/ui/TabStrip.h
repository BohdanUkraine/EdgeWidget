#pragma once

#include "ui/ScaledWidget.h"

class TabStrip : public ScaledWidget
{
    Q_OBJECT

public:
    enum Tab { Library, Settings, Filters, None };

    explicit TabStrip(QWidget *parent = nullptr);
    void setCurrent(Tab tab);
    // Filters is a toggle, not a page: lit while its card is open, a dot while a filter applies.
    void setFilters(bool open, bool applied);

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
    bool m_filtersOpen = false;
    bool m_filterApplied = false;
};
