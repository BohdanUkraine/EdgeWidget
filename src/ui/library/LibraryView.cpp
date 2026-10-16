#include "ui/library/LibraryView.h"

#include "ui/SpotifySetup.h"

#include <QMouseEvent>
#include <QToolTip>

#include <algorithm>

using namespace LibraryStyle;

namespace {
constexpr int kDetailAnimMs = 260;
} // namespace

LibraryView::LibraryView(SpotifyClient *spotify, CoverCache *covers, QWidget *parent)
    : ScaledWidget(parent)
    , m_spotify(spotify)
    , m_covers(covers)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setMouseTracking(true);

    m_detailAnim.setDuration(kDetailAnimMs);
    m_detailAnim.setEasingCurve(QEasingCurve::OutCubic);
    connect(&m_detailAnim, &QVariantAnimation::valueChanged, this, [this](const QVariant &v) {
        m_detailT = v.toReal();
        update();
    });
    connect(&m_detailAnim, &QVariantAnimation::finished, this, [this] {
        if (!m_detailOpen) {
            m_detail = -1;
            update();
        }
    });

    connect(m_spotify, &SpotifyClient::libraryChanged, this, [this] {
        m_scroll.clamp(maxScroll());
        if (m_detail >= 0) {
            // Rows may have moved; follow the opened item or close it.
            const auto &lib = m_spotify->library();
            const auto it = std::find_if(lib.begin(), lib.end(),
                                         [this](const LibraryItem &i) { return i.uri == m_detailUri; });
            if (it == lib.end())
                closeDetail();
            else
                m_detail = int(it - lib.begin());
        }
        update();
    });
    connect(m_spotify, &SpotifyClient::activeChanged, this, qOverload<>(&QWidget::update));
    connect(m_spotify, &SpotifyClient::connectedChanged, this, qOverload<>(&QWidget::update));
    connect(m_covers, &CoverCache::coverReady, this, qOverload<>(&QWidget::update));
    connect(&AppSettings::instance(), &AppSettings::changed, this, [this] {
        m_scroll.clamp(maxScroll());
        update();
    });
}

void LibraryView::setPlaying(bool playing)
{
    if (playing == m_playing)
        return;
    m_playing = playing;
    update();
}

// ---------------------------------------------------------------- open / close

void LibraryView::openDetail(int index)
{
    const LibraryItem item = m_spotify->library().at(index);
    m_detail = index;
    m_detailUri = item.uri;
    m_detailOpen = true;
    m_hover = {};
    m_detailAnim.stop();
    m_detailAnim.setStartValue(m_detailT);
    m_detailAnim.setEndValue(1.0);
    m_detailAnim.start();
    emit detailOpened(item, mapToGlobal(QPointF(0, rowRect(index).top() * uiScale()).toPoint()).y());
}

void LibraryView::closeDetail(bool fadeHeader)
{
    if (!m_detailOpen)
        return;
    m_detailOpen = false;
    m_hover = {};
    m_detailAnim.stop();
    m_detailAnim.setStartValue(m_detailT);
    m_detailAnim.setEndValue(0.0);
    m_detailAnim.start();
    emit detailClosed(fadeHeader);
}

// ---------------------------------------------------------------- geometry

QRect LibraryView::rowRect(int index) const
{
    return QRect(0, index * kRowH - m_scroll.offset, int(lw()), kRowH);
}

int LibraryView::maxScroll() const
{
    return std::max(0, int(m_spotify->library().size()) * kRowH - int(lh()));
}

LibraryView::Hover LibraryView::hitTest(const QPoint &pos) const
{
    if (!m_spotify->isConnected() || m_detail >= 0 || !logicalRect().contains(pos))
        return {}; // while open, the item lives in TrackListPopup
    const int index = (pos.y() + m_scroll.offset) / kRowH;
    if (index < 0 || index >= m_spotify->library().size())
        return {};
    return { index, partAt(rowRect(index), pos) };
}

// ---------------------------------------------------------------- painting

void LibraryView::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    prepare(p);

    const auto &items = m_spotify->library();
    if (!m_spotify->isConnected() || items.isEmpty()) {
        paintPlaceholder(p);
        return;
    }

    // The other rows fade out by the middle of the opening animation.
    if (m_detail < 0 || m_detailT < 1.0) {
        p.save();
        p.setOpacity(std::max(0.0, 1.0 - 2.0 * m_detailT));
        const int first = m_scroll.offset / kRowH;
        const int last = std::min(int(items.size()) - 1, (m_scroll.offset + int(lh())) / kRowH);
        for (int i = first; i <= last; ++i) {
            if (i == m_detail)
                continue;
            const LibraryItem &item = items.at(i);
            RowState state;
            state.hovered = m_hover.index == i && !m_scroll.dragged;
            state.hoverPart = state.hovered ? m_hover.part : RowPart::None;
            state.active = item.uri == m_spotify->activeUri();
            state.playing = m_playing;
            paintRow(p, font(), rowRect(i), item, m_covers->get(item.imageUrl), state);
        }
        p.restore();
    }
    if (m_detail >= 0)
        return;

    paintEdgeFades(p, logicalRect(), m_scroll.offset, maxScroll());
    if (m_hover.index >= 0 || m_scroll.dragged)
        paintScrollbar(p, QRectF(logicalRect()).adjusted(0, 0, -1, 0), int(items.size()) * kRowH, m_scroll.offset,
                       maxScroll());
}

void LibraryView::paintPlaceholder(QPainter &p)
{
    QString head;
    QString hint;
    if (!m_spotify->isConnected()) {
        head = tr("Підключи Spotify");
        hint = tr("Натисни, щоб увійти й побачити свою бібліотеку");
    } else if (m_spotify->isLoading()) {
        head = tr("Завантажую бібліотеку…");
    } else {
        head = tr("Бібліотека порожня");
    }
    const QRect area = logicalRect();
    QFont f = font();
    f.setPixelSize(13);
    f.setBold(true);
    p.setFont(f);
    p.setPen(m_spotify->isConnected() ? th().text : th().accent);
    p.drawText(area.adjusted(0, 0, 0, -18), Qt::AlignCenter, head);
    f.setPixelSize(11);
    f.setBold(false);
    p.setFont(f);
    p.setPen(th().subText);
    p.drawText(area.adjusted(0, 22, 0, 0), Qt::AlignCenter, hint);
}

// ---------------------------------------------------------------- input

void LibraryView::wheelEvent(QWheelEvent *event)
{
    if (m_detail >= 0) {
        event->ignore(); // the opened item does not scroll
        return;
    }
    m_scroll.wheel(event, kRowH, maxScroll());
    m_hover = hitTest(logical(event->position()));
    update();
    event->accept();
}

void LibraryView::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton)
        m_scroll.press(logical(event->position()).y());
}

void LibraryView::mouseMoveEvent(QMouseEvent *event)
{
    const QPoint pos = logical(event->position());
    if (m_detail < 0 && m_scroll.drag(pos.y(), maxScroll())) {
        update();
        return;
    }

    const Hover hover = hitTest(pos);
    setCursor(hover.index >= 0 || !m_spotify->isConnected() ? Qt::PointingHandCursor : Qt::ArrowCursor);
    if (hover.index != m_hover.index || hover.part != m_hover.part) {
        if (hover.part == RowPart::Nfc)
            QToolTip::showText(event->globalPosition().toPoint(), nfcTooltip(), this);
        else if (m_hover.part == RowPart::Nfc)
            QToolTip::hideText();
        m_hover = hover;
        update();
    }
}

void LibraryView::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton || !m_scroll.pressed)
        return;
    if (m_scroll.release()) {
        update();
        return;
    }
    if (!m_spotify->isConnected()) {
        runSpotifySetup(m_spotify, window());
        return;
    }

    const Hover hit = hitTest(logical(event->position()));
    if (hit.index < 0)
        return;
    const LibraryItem item = m_spotify->library().at(hit.index);
    switch (hit.part) {
    case RowPart::Art:
        if (item.uri == m_spotify->activeUri())
            emit togglePlayRequested(); // pause / resume instead of restarting it
        else
            m_spotify->play(item.uri);
        break;
    case RowPart::Nfc:
        emit writeRecordRequested(item);
        break;
    case RowPart::Text:
        openDetail(hit.index);
        break;
    case RowPart::None:
        break;
    }
}

void LibraryView::leaveEvent(QEvent *event)
{
    QWidget::leaveEvent(event);
    m_hover = {};
    update();
}
