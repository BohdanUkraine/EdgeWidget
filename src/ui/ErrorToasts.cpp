#include "ui/ErrorToasts.h"

#include <QCursor>
#include <QMouseEvent>
#include <QToolTip>

#include <algorithm>
#include <cmath>

namespace {
constexpr int kCardH = 24;
constexpr int kGap = 4;
constexpr qreal kRadius = 8.0;
constexpr qreal kBorder = 1.5;
constexpr int kPeek = 4;
constexpr int kPeekInset = 8;
constexpr int kStackLayers = 2;
constexpr int kMaxCards = 6;
constexpr qreal kTauMs = 55.0;
const QColor kRed(0xE5, 0x48, 0x4D);

QColor cardFill(const Theme &t)
{
    const qreal a = t.dark ? 0.14 : 0.07;
    auto mix = [a](qreal bg, qreal red) { return bg + (red - bg) * a; };
    return QColor::fromRgbF(mix(t.bg.redF(), kRed.redF()), mix(t.bg.greenF(), kRed.greenF()),
                            mix(t.bg.blueF(), kRed.blueF()));
}
}

ErrorToasts::ErrorToasts(QWidget *parent)
    : ScaledWidget(parent)
{
    setMouseTracking(true);
    hide();
    m_tick.setInterval(16);
    connect(&m_tick, &QTimer::timeout, this, &ErrorToasts::tick);
    connect(&AppSettings::instance(), &AppSettings::changed, this, qOverload<>(&QWidget::update));
}

void ErrorToasts::setAnchor(const QPoint &topLeft, int width)
{
    m_anchor = topLeft;
    m_width = width;
    fitToCards();
}

void ErrorToasts::showError(const QString &message)
{
    const QString text = message.simplified();
    if (text.isEmpty())
        return;

    Card card{ text };
    card.y = -kCardH;
    for (int i = 0; i < m_cards.size(); ++i) {
        if (!m_cards.at(i).closing && m_cards.at(i).text == text) {
            card = m_cards.takeAt(i);
            ++card.count;
            break;
        }
    }
    m_cards.prepend(card);
    int alive = 0;
    for (Card &c : m_cards) {
        if (!c.closing && ++alive > kMaxCards)
            c.closing = true;
    }
    m_hover = -1;
    show();
    raise();
    animate();
}

ErrorToasts::Target ErrorToasts::target(int index) const
{
    const Card &card = m_cards.at(index);
    if (card.closing)
        return { card.y, card.inset, 0.0 };
    int rank = 0;
    for (int i = 0; i < index; ++i)
        rank += m_cards.at(i).closing ? 0 : 1;
    if (m_expanded)
        return { rank * qreal(kCardH + kGap), 0.0, 1.0 };
    const int layer = std::min(rank, kStackLayers);
    return { qreal(layer * kPeek), qreal(layer * kPeekInset), rank > kStackLayers ? 0.0 : 1.0 };
}

void ErrorToasts::animate()
{
    if (!m_tick.isActive()) {
        m_clock.start();
        m_tick.start();
    }
    fitToCards();
    update();
}

void ErrorToasts::tick()
{
    const qreal dt = qreal(std::min<qint64>(m_clock.restart(), 50));
    const qreal k = 1.0 - std::exp(-dt / kTauMs);
    bool moving = false;
    auto step = [&](qreal &value, qreal to, qreal eps) {
        value += (to - value) * k;
        if (std::abs(to - value) < eps)
            value = to;
        else
            moving = true;
    };
    for (int i = 0; i < m_cards.size(); ++i) {
        const Target t = target(i);
        Card &c = m_cards[i];
        step(c.y, t.y, 0.2);
        step(c.inset, t.inset, 0.2);
        step(c.opacity, t.opacity, 0.01);
    }
    if (m_cards.removeIf([](const Card &c) { return c.closing && c.opacity <= 0.0; }) > 0) {
        m_hover = -1;
        if (underMouse())
            updateHover(logical(mapFromGlobal(QCursor::pos())));
    }
    if (!moving)
        m_tick.stop();
    fitToCards();
    update();
}

// Covers only the cards, so the panel under them stays clickable.
void ErrorToasts::fitToCards()
{
    if (m_cards.isEmpty()) {
        hide();
        return;
    }
    qreal bottom = 0.0;
    for (const Card &c : m_cards) {
        if (c.opacity > 0.0)
            bottom = std::max(bottom, c.y + kCardH);
    }
    setGeometry(m_anchor.x(), m_anchor.y(), m_width, std::max(1, int(std::ceil(bottom * uiScale()))));
}

void ErrorToasts::setExpanded(bool expanded)
{
    if (expanded == m_expanded)
        return;
    m_expanded = expanded;
    animate();
}

QRectF ErrorToasts::cardRect(const Card &card) const
{
    return QRectF(card.inset, card.y, lw() - 2 * card.inset, kCardH);
}

QRectF ErrorToasts::closeRect(const QRectF &card) const
{
    return QRectF(card.right() - kCardH, card.top(), kCardH, kCardH);
}

QRectF ErrorToasts::textRect(const Card &card, const QRectF &rect, const QFont &font) const
{
    qreal right = closeRect(rect).left();
    if (card.count > 1)
        right -= QFontMetricsF(font).horizontalAdvance(QStringLiteral("×%1").arg(card.count)) + 6;
    const qreal left = rect.left() + 26;
    return QRectF(left, rect.top(), std::max<qreal>(0, right - left), rect.height());
}

QFont ErrorToasts::textFont() const
{
    QFont f = font();
    f.setPixelSize(11);
    return f;
}

int ErrorToasts::cardAt(const QPoint &pos) const
{
    for (int i = 0; i < m_cards.size(); ++i) {
        if (m_cards.at(i).closing)
            continue;
        if (cardRect(m_cards.at(i)).contains(pos))
            return i;
        if (!m_expanded)
            return -1;
    }
    return -1;
}

void ErrorToasts::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    prepare(p);
    const Theme t = Theme::current();
    const QFont f = textFont();
    p.setFont(f);

    for (int i = int(m_cards.size()) - 1; i >= 0; --i) {
        const Card &c = m_cards.at(i);
        if (c.opacity <= 0.0)
            continue;
        p.setOpacity(c.opacity);
        const QRectF r = cardRect(c);
        const qreal depth = std::clamp(c.inset / kPeekInset, 0.0, qreal(kStackLayers));

        QColor border = kRed;
        border.setAlphaF(1.0 - 0.3 * depth);
        p.setPen(QPen(border, kBorder));
        p.setBrush(cardFill(t));
        const qreal half = kBorder / 2.0;
        p.drawRoundedRect(r.adjusted(half, half, -half, -half), kRadius, kRadius);

        const qreal content = std::clamp(1.0 - depth, 0.0, 1.0);
        if (content <= 0.0)
            continue;
        p.setOpacity(c.opacity * content);

        const QRectF badge(r.left() + 8, r.center().y() - 6, 12, 12);
        p.setPen(Qt::NoPen);
        p.setBrush(kRed);
        p.drawEllipse(badge);
        p.setPen(QPen(Qt::white, 1.6, Qt::SolidLine, Qt::RoundCap));
        const qreal bx = badge.center().x();
        p.drawLine(QPointF(bx, badge.top() + 3.0), QPointF(bx, badge.center().y() + 1.0));
        p.drawPoint(QPointF(bx, badge.bottom() - 3.0));

        const QRectF x = closeRect(r);
        const bool hot = i == m_hover && m_hoverClose;
        if (hot) {
            p.setPen(Qt::NoPen);
            p.setBrush(t.hover);
            p.drawEllipse(x.adjusted(3, 3, -3, -3));
        }
        p.setPen(QPen(hot ? t.text : t.subText, 1.5, Qt::SolidLine, Qt::RoundCap));
        const QPointF cc = x.center();
        constexpr qreal a = 3.5;
        p.drawLine(cc + QPointF(-a, -a), cc + QPointF(a, a));
        p.drawLine(cc + QPointF(a, -a), cc + QPointF(-a, a));

        const QRectF tr = textRect(c, r, f);
        if (c.count > 1) {
            p.setPen(t.subText);
            p.drawText(QRectF(tr.right(), r.top(), x.left() - tr.right(), r.height()), Qt::AlignCenter,
                       QStringLiteral("×%1").arg(c.count));
        }
        p.setPen(t.text);
        p.drawText(tr, Qt::AlignVCenter | Qt::AlignLeft, QFontMetricsF(f).elidedText(c.text, Qt::ElideRight, tr.width()));
    }
}

void ErrorToasts::updateHover(const QPoint &pos)
{
    const int hover = cardAt(pos);
    const bool onClose = hover >= 0 && closeRect(cardRect(m_cards.at(hover))).contains(pos);
    if (hover == m_hover && onClose == m_hoverClose)
        return;
    m_hover = hover;
    m_hoverClose = onClose;
    setCursor(onClose ? Qt::PointingHandCursor : Qt::ArrowCursor);

    const QFont f = textFont();
    if (hover >= 0 && !onClose) {
        const Card &c = m_cards.at(hover);
        const QRectF tr = textRect(c, cardRect(c), f);
        if (QFontMetricsF(f).horizontalAdvance(c.text) > tr.width()) {
            QToolTip::showText(QCursor::pos(), c.text, this);
            update();
            return;
        }
    }
    QToolTip::hideText();
    update();
}

void ErrorToasts::mousePressEvent(QMouseEvent *event)
{
    event->accept();
}

void ErrorToasts::mouseMoveEvent(QMouseEvent *event)
{
    updateHover(logical(event->position()));
}

void ErrorToasts::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton)
        return;
    const QPoint pos = logical(event->position());
    const int hit = cardAt(pos);
    if (hit < 0 || !closeRect(cardRect(m_cards.at(hit))).contains(pos))
        return;
    m_cards[hit].closing = true;
    m_hover = -1;
    QToolTip::hideText();
    animate();
}

void ErrorToasts::enterEvent(QEnterEvent *event)
{
    ScaledWidget::enterEvent(event);
    setExpanded(true);
}

void ErrorToasts::leaveEvent(QEvent *event)
{
    ScaledWidget::leaveEvent(event);
    m_hover = -1;
    m_hoverClose = false;
    QToolTip::hideText();
    setExpanded(false);
}

void ErrorToasts::hideEvent(QHideEvent *event)
{
    ScaledWidget::hideEvent(event);
    m_hover = -1;
    setExpanded(false);
}
