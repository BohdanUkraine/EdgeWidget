#include "ui/TabStrip.h"

#include <QMouseEvent>
#include <QPainterPath>

namespace {
constexpr int kTabSize = 28;
constexpr int kTabTop = 14;
constexpr int kTabGap = 6;

void paintLibraryIcon(QPainter &p, const QColor &color)
{
    p.setPen(QPen(color, 1.8, Qt::SolidLine, Qt::RoundCap));
    p.drawLine(QPointF(-5.5, -6.5), QPointF(-5.5, 6.5));
    p.drawLine(QPointF(-1.5, -6.5), QPointF(-1.5, 6.5));
    p.drawLine(QPointF(2.5, -6), QPointF(6, 6.5));
}

void paintFilterIcon(QPainter &p, const QColor &color)
{
    p.setPen(QPen(color, 1.8, Qt::SolidLine, Qt::RoundCap));
    p.drawLine(QPointF(-6.5, -5), QPointF(6.5, -5));
    p.drawLine(QPointF(-4, 0), QPointF(4, 0));
    p.drawLine(QPointF(-1.5, 5), QPointF(1.5, 5));
}

// Top to bottom: the library, its filters right under it, the settings.
int slot(TabStrip::Tab tab)
{
    switch (tab) {
    case TabStrip::Library: return 0;
    case TabStrip::Filters: return 1;
    default: return 2;
    }
}

const TabStrip::Tab kTabs[] = { TabStrip::Library, TabStrip::Filters, TabStrip::Settings };

void paintGearIcon(QPainter &p, const QColor &color)
{
    p.setPen(Qt::NoPen);
    p.setBrush(color);
    for (int i = 0; i < 8; ++i) {
        p.drawRoundedRect(QRectF(-1.6, -7.5, 3.2, 4), 0.8, 0.8);
        p.rotate(45);
    }
    QPainterPath body;
    body.addEllipse(QPointF(), 5.4, 5.4);
    body.addEllipse(QPointF(), 2.2, 2.2);
    p.drawPath(body);
}
}

TabStrip::TabStrip(QWidget *parent)
    : ScaledWidget(parent)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setMouseTracking(true);
    connect(&AppSettings::instance(), &AppSettings::changed, this, qOverload<>(&QWidget::update));
}

void TabStrip::setCurrent(Tab tab)
{
    m_current = tab;
    update();
}

void TabStrip::setFilters(bool open, bool applied)
{
    m_filtersOpen = open;
    m_filterApplied = applied;
    update();
}

QRect TabStrip::tabRect(Tab tab) const
{
    const int x = (int(lw()) - kTabSize) / 2;
    return QRect(x, kTabTop + slot(tab) * (kTabSize + kTabGap), kTabSize, kTabSize);
}

TabStrip::Tab TabStrip::tabAt(const QPoint &pos) const
{
    for (Tab tab : kTabs) {
        if (tabRect(tab).contains(pos))
            return tab;
    }
    return None;
}

void TabStrip::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    prepare(p);
    const Theme th = Theme::current();

    p.setPen(QPen(th.line, 1));
    p.drawLine(QPointF(0.5, 12), QPointF(0.5, lh() - 12));

    for (Tab tab : kTabs) {
        const QRectF r = tabRect(tab);
        const bool active = tab == Filters ? m_filtersOpen : tab == m_current;
        const bool hovered = tab == m_hover;
        if (active || hovered) {
            QColor bg = active ? th.accent : th.hover;
            if (active)
                bg.setAlpha(38);
            p.setPen(Qt::NoPen);
            p.setBrush(bg);
            p.drawRoundedRect(r, 7, 7);
        }
        const QColor color = active ? th.accent : (hovered ? th.text : th.subText);
        p.save();
        p.translate(r.center());
        if (tab == Library)
            paintLibraryIcon(p, color);
        else if (tab == Filters)
            paintFilterIcon(p, color);
        else
            paintGearIcon(p, color);
        p.restore();
        if (tab == Filters && m_filterApplied && !m_filtersOpen) {
            p.setPen(Qt::NoPen);
            p.setBrush(th.accent);
            p.drawEllipse(QPointF(r.right() - 4, r.top() + 4), 2.5, 2.5);
        }
    }
}

void TabStrip::mousePressEvent(QMouseEvent *event)
{
    const Tab tab = tabAt(logical(event->position()));
    if (event->button() == Qt::LeftButton && tab != None)
        emit tabClicked(tab);
}

void TabStrip::mouseMoveEvent(QMouseEvent *event)
{
    const Tab hover = tabAt(logical(event->position()));
    if (hover != m_hover) {
        m_hover = hover;
        setCursor(hover == None ? Qt::ArrowCursor : Qt::PointingHandCursor);
        update();
    }
}

void TabStrip::leaveEvent(QEvent *event)
{
    QWidget::leaveEvent(event);
    m_hover = None;
    update();
}
