#include "ui/library/LibraryCard.h"

#include <QGuiApplication>
#include <QMouseEvent>
#include <QPainterPath>
#include <QScreen>

#include <algorithm>
#include <cmath>

using namespace LibraryStyle;

namespace {
constexpr int kInset = 10; // the same frame as TrackListPopup's card
constexpr int kRadius = 14;
constexpr int kBorder = 2;
constexpr int kPad = 6;
constexpr int kTitleH = 22;
constexpr int kChipH = 22;
constexpr int kGap = 4;
constexpr int kMaxH = kPad + kTitleH + kGap + kChipH + kPad;
constexpr int kAnimMs = 220;

const LibraryView::Filter kChips[] = { LibraryView::Filter::Playlists, LibraryView::Filter::Albums,
                                       LibraryView::Filter::Artists };

QString chipText(int i)
{
    switch (i) {
    case 0: return QObject::tr("Плейлисти");
    case 1: return QObject::tr("Альбоми");
    default: return QObject::tr("Виконавці");
    }
}
}

LibraryCard::LibraryCard(QWidget *owner)
    : ScaledWidget(owner, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::WindowDoesNotAcceptFocus)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setAttribute(Qt::WA_MacAlwaysShowToolWindow);
    setMouseTracking(true);

    for (QVariantAnimation *anim : { &m_titleAnim, &m_filtersAnim }) {
        anim->setDuration(kAnimMs);
        anim->setEasingCurve(QEasingCurve::OutCubic);
        connect(anim, &QVariantAnimation::finished, this, [this] {
            if (m_titleOut <= 0.0)
                m_shownTitle = m_title;
            if (m_titleOut <= 0.0 && m_filtersOut <= 0.0 && m_titleAnim.state() != QAbstractAnimation::Running
                && m_filtersAnim.state() != QAbstractAnimation::Running)
                hide();
        });
    }
    connect(&m_titleAnim, &QVariantAnimation::valueChanged, this, [this](const QVariant &v) {
        m_titleOut = v.toReal();
        update();
    });
    connect(&m_filtersAnim, &QVariantAnimation::valueChanged, this, [this](const QVariant &v) {
        m_filtersOut = v.toReal();
        update();
    });
    connect(&AppSettings::instance(), &AppSettings::changed, this, [this] {
        applyGeometry();
        update();
    });
}

void LibraryCard::setArea(const QRect &column, int panelTop, int panelBottom)
{
    m_column = column;
    m_panelTop = panelTop;
    m_panelBottom = panelBottom;
    applyGeometry();
}

void LibraryCard::setTitle(const QString &title)
{
    if (title == m_title)
        return;
    m_title = title;
    if (!title.isEmpty())
        m_shownTitle = title;
    animate();
    update();
}

void LibraryCard::setFiltersOpen(bool open)
{
    if (open == m_filtersOpen)
        return;
    m_filtersOpen = open;
    animate();
}

void LibraryCard::setFilter(LibraryView::Filter filter)
{
    m_filter = filter;
    update();
}

void LibraryCard::setSuppressed(bool suppressed)
{
    if (suppressed == m_suppressed)
        return;
    m_suppressed = suppressed;
    animate();
}

void LibraryCard::animateRow(QVariantAnimation &anim, qreal from, bool out)
{
    const qreal target = out ? 1.0 : 0.0;
    const bool running = anim.state() == QAbstractAnimation::Running;
    if ((running && anim.endValue().toReal() == target) || (!running && from == target))
        return;
    anim.stop();
    anim.setStartValue(from);
    anim.setEndValue(target);
    anim.start();
}

void LibraryCard::animate()
{
    const bool titleOut = !m_suppressed && !m_title.isEmpty();
    const bool filtersOut = !m_suppressed && m_filtersOpen;
    m_hover = Part::None;
    if ((titleOut || filtersOut) && !isVisible()) {
        applyGeometry();
        show();
        raise();
    }
    animateRow(m_titleAnim, m_titleOut, titleOut);
    animateRow(m_filtersAnim, m_filtersOut, filtersOut);
}

// Window: the card's full height just outside the panel's edge. It stops at the panel's
// border and leaves it alone, so the card comes out from behind the frame (the playlist
// card instead paints over it and opens into the panel).
void LibraryCard::applyGeometry()
{
    if (m_column.isEmpty())
        return;
    const qreal s = uiScale();
    const int inset = int(std::lround(kInset * s));
    const int h = int(std::lround(kMaxH * s));
    QScreen *screen = QGuiApplication::screenAt(QPoint(m_column.left(), m_panelTop));
    if (!screen)
        screen = QGuiApplication::primaryScreen();
    const int screenTop = screen ? screen->availableGeometry().top() : 0;
    m_down = m_panelTop - h < screenTop;
    const int x = m_column.left() - inset;
    const int w = m_column.width() + 2 * inset;
    if (m_down)
        setGeometry(x, m_panelBottom, w, h);
    else
        setGeometry(x, m_panelTop - h, w, h);
}

// The filters come from behind the panel's frame; the name from behind the filters (or the
// frame, without them). The space between the two closes to the padding without filters.
qreal LibraryCard::filtersY() const
{
    return m_down ? -kChipH + (kPad + kChipH) * m_filtersOut : kMaxH - (kPad + kChipH) * m_filtersOut;
}

qreal LibraryCard::titleY() const
{
    const qreal gap = kGap + (kPad - kGap) * (1.0 - m_filtersOut);
    return m_down ? filtersY() + (kChipH + gap) * m_titleOut : filtersY() - (kTitleH + gap) * m_titleOut;
}

qreal LibraryCard::cardEdge() const
{
    const qreal pad = kPad * std::max(m_titleOut, m_filtersOut);
    return m_down ? titleY() + kTitleH + pad : titleY() - pad;
}

QRect LibraryCard::titleRect() const
{
    return QRect(kInset + 2, int(std::lround(titleY())), int(lw()) - 2 * kInset - 4, kTitleH);
}

QRect LibraryCard::chipRect(int i) const
{
    QFont f = font();
    f.setPixelSize(11);
    const QFontMetrics fm(f);
    int x = kInset + 2;
    for (int k = 0; k < i; ++k)
        x += fm.horizontalAdvance(chipText(k)) + 22 + 6;
    return QRect(x, int(std::lround(filtersY())), fm.horizontalAdvance(chipText(i)) + 22, kChipH);
}

LibraryCard::Part LibraryCard::partAt(const QPoint &pos) const
{
    if (m_titleAnim.state() == QAbstractAnimation::Running || m_filtersAnim.state() == QAbstractAnimation::Running)
        return Part::None;
    if (m_titleOut >= 1.0 && titleRect().contains(pos))
        return Part::Back;
    if (m_filtersOut >= 1.0) {
        for (int i = 0; i < 3; ++i) {
            if (chipRect(i).contains(pos))
                return Part(int(Part::Playlists) + i);
        }
    }
    return Part::None;
}

void LibraryCard::paintEvent(QPaintEvent *)
{
    const qreal edge = cardEdge();
    const qreal size = m_down ? edge : kMaxH - edge;
    if (size <= 0.5)
        return;
    QPainter p(this);
    prepare(p);
    const Theme theme = th();
    const qreal W = lw();
    const qreal half = kBorder / 2.0;

    // A rounded rect running on past the window's panel side, cut there: its sides meet the
    // panel's border, which stays drawn, as if the card went on behind it.
    const QRectF outer = m_down ? QRectF(half, -kRadius - kBorder, W - kBorder, edge - half + kRadius + kBorder)
                                : QRectF(half, edge + half, W - kBorder, size + kRadius + kBorder);
    QPainterPath outerPath;
    outerPath.addRoundedRect(outer, kRadius, kRadius);
    QPainterPath window;
    window.addRect(QRectF(0, 0, W, lh()));
    const QPainterPath card = outerPath.intersected(window);
    p.setPen(Qt::NoPen);
    p.setBrush(theme.bg);
    p.drawPath(card);

    p.save();
    p.setClipPath(card);
    if (m_titleOut > 0.0)
        paintHeader(p, font(), titleRect(), m_shownTitle, true, m_hover == Part::Back);
    if (m_filtersOut > 0.0) {
        // a solid band: the name slides out from under it
        p.fillRect(QRectF(kBorder, filtersY(), W - 2 * kBorder, kChipH), theme.bg);
        QFont f = font();
        f.setPixelSize(11);
        p.setFont(f);
        const QColor onAccent = theme.accent.lightness() > 150 ? QColor(0, 0, 0) : QColor(255, 255, 255);
        for (int i = 0; i < 3; ++i) {
            const QRectF r = QRectF(chipRect(i)).adjusted(0.5, 0.5, -0.5, -0.5);
            const bool on = m_filter == kChips[i];
            const bool hovered = m_hover == Part(int(Part::Playlists) + i);
            p.setPen(QPen(on || hovered ? theme.accent : theme.line, 1));
            p.setBrush(on ? theme.accent : theme.hover);
            p.drawRoundedRect(r, kChipH / 2.0, kChipH / 2.0);
            p.setPen(on ? onAccent : theme.text);
            p.drawText(r, Qt::AlignCenter, chipText(i));
        }
    }
    p.restore();

    p.setClipPath(window);
    p.setPen(QPen(theme.accent, kBorder));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(outer, kRadius, kRadius);
}

void LibraryCard::mouseMoveEvent(QMouseEvent *event)
{
    const Part hover = partAt(logical(event->position()));
    if (hover != m_hover) {
        m_hover = hover;
        setCursor(hover == Part::None ? Qt::ArrowCursor : Qt::PointingHandCursor);
        update();
    }
}

void LibraryCard::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton)
        return;
    const Part part = partAt(logical(event->position()));
    if (part == Part::Back)
        emit backClicked();
    else if (part != Part::None)
        emit filterClicked(kChips[int(part) - int(Part::Playlists)]);
}

void LibraryCard::leaveEvent(QEvent *event)
{
    QWidget::leaveEvent(event);
    m_hover = Part::None;
    update();
}
