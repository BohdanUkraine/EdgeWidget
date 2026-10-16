#include "ui/library/TrackListPopup.h"

#include <QGuiApplication>
#include <QMouseEvent>
#include <QPainterPath>
#include <QScreen>
#include <QToolTip>

#include <algorithm>
#include <cmath>

using namespace LibraryStyle;

namespace {
constexpr int kInset = 10; // card is this much wider than the library column on each side
constexpr int kPad = 8;
constexpr int kRadius = 14;
constexpr int kBorder = 2;
constexpr int kAnimMs = 260;
} // namespace

TrackListPopup::TrackListPopup(SpotifyClient *spotify, CoverCache *covers, QWidget *owner)
    : ScaledWidget(owner, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::WindowDoesNotAcceptFocus)
    , m_spotify(spotify)
    , m_covers(covers)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setAttribute(Qt::WA_MacAlwaysShowToolWindow);
    setMouseTracking(true);

    m_anim.setDuration(kAnimMs);
    m_anim.setEasingCurve(QEasingCurve::OutCubic);
    connect(&m_anim, &QVariantAnimation::valueChanged, this, [this](const QVariant &v) {
        m_t = v.toReal();
        update();
    });
    connect(&m_anim, &QVariantAnimation::finished, this, [this] {
        if (!m_open)
            hide();
    });

    connect(m_covers, &CoverCache::coverReady, this, qOverload<>(&QWidget::update));
    connect(m_spotify, &SpotifyClient::activeChanged, this, qOverload<>(&QWidget::update));
    connect(&AppSettings::instance(), &AppSettings::changed, this, [this] {
        applyGeometry();
        update();
    });
}

// ---------------------------------------------------------------- geometry

qreal TrackListPopup::edgeY() const
{
    return m_down ? (m_panelBottom - m_column.top()) / uiScale() : AppSettings::instance().cardHeight();
}

// The header starts exactly where the clicked row was and moves to the top of the column
// (top of the card when it opens upwards).
QRect TrackListPopup::headerRect() const
{
    const qreal columnTop = m_down ? 0.0 : edgeY() + (m_column.top() - m_panelTop) / uiScale();
    const qreal start = columnTop + m_rowOffset;
    const qreal end = m_down ? 0.0 : kPad;
    // Fading out (settings opened): stay at the top instead of travelling back.
    const qreal y = m_fadeHeader ? end : start + (end - start) * m_t;
    return QRect(kInset, int(std::lround(y)), int(lw()) - 2 * kInset, kRowH);
}

QRect TrackListPopup::tracksRect() const
{
    const int top = (m_down ? 0 : kPad) + kRowH + 6;
    const int bottom = int(lh()) - (m_down ? kPad + kBorder : kBorder);
    return QRect(kInset, top, int(lw()) - 2 * kInset, bottom - top);
}

int TrackListPopup::maxScroll() const
{
    return std::max(0, int(m_tracks.size()) * kTrackH - tracksRect().height());
}

void TrackListPopup::setArea(const QRect &column, int panelTop, int panelBottom)
{
    m_column = column;
    m_panelTop = panelTop;
    m_panelBottom = panelBottom;
    applyGeometry();
}

void TrackListPopup::applyGeometry()
{
    if (m_column.isEmpty())
        return;
    const qreal s = uiScale();
    const int card = int(std::lround(AppSettings::instance().cardHeight() * s));
    const int inset = int(std::lround(kInset * s));
    const int x = m_column.left() - inset;
    const int w = m_column.width() + 2 * inset;
    if (m_down)
        setGeometry(x, m_column.top(), w, (m_panelBottom - m_column.top()) + card);
    else
        setGeometry(x, m_panelTop - card, w, card + (m_column.bottom() + 1 - m_panelTop));
}

TrackListPopup::Hover TrackListPopup::hitTest(const QPoint &pos) const
{
    Hover h;
    if (m_t < 1.0)
        return h;
    h.part = partAt(headerRect(), pos);
    if (h.part != RowPart::None)
        return h;
    const QRect list = tracksRect();
    if (m_state == TracksState::Ready && list.contains(pos)) {
        const int index = (pos.y() - list.top() + m_scroll.offset) / kTrackH;
        if (index >= 0 && index < m_tracks.size())
            h.track = index;
    }
    return h;
}

// ---------------------------------------------------------------- open / close

void TrackListPopup::open(const LibraryItem &item, int rowTop)
{
    m_item = item;
    m_tracks.clear();
    m_state = TracksState::Loading;
    m_scroll = {};
    m_hover = {};

    const int id = ++m_requestId;
    m_spotify->fetchTracks(item, [this, id](const QList<Track> &tracks, bool ok) {
        if (id != m_requestId)
            return;
        m_tracks = tracks;
        m_state = ok ? TracksState::Ready : TracksState::Failed;
        update();
    });

    if (!m_open) {
        // Not enough room above the panel? Open downwards instead.
        QScreen *screen = QGuiApplication::screenAt(QPoint(m_column.left(), m_panelTop));
        if (!screen)
            screen = QGuiApplication::primaryScreen();
        const int screenTop = screen ? screen->availableGeometry().top() : 0;
        const int card = int(std::lround(AppSettings::instance().cardHeight() * uiScale()));
        m_down = m_panelTop - card < screenTop;
        m_rowOffset = (rowTop - m_column.top()) / uiScale();

        m_open = true;
        m_fadeHeader = false;
        applyGeometry();
        show();
        raise();
        m_anim.stop();
        m_anim.setStartValue(m_t);
        m_anim.setEndValue(1.0);
        m_anim.start();
    }
    update();
}

void TrackListPopup::close(bool fadeHeader)
{
    if (!m_open)
        return;
    m_open = false;
    m_fadeHeader = fadeHeader;
    m_hover = {};
    ++m_requestId;
    m_anim.stop();
    m_anim.setStartValue(m_t);
    m_anim.setEndValue(0.0);
    m_anim.start();
}

void TrackListPopup::setNowPlaying(const QString &title, bool playing)
{
    if (title == m_nowTitle && playing == m_playing)
        return;
    m_nowTitle = title;
    m_playing = playing;
    update();
}

// ---------------------------------------------------------------- painting

void TrackListPopup::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    prepare(p);

    const Theme theme = th();
    const qreal W = lw();
    const qreal H = lh();
    const qreal half = kBorder / 2.0;
    const qreal edge = edgeY();
    const qreal card = AppSettings::instance().cardHeight();

    // The part outside the panel is rounded on its outer side and covers the panel's border,
    // so card and panel become one shape. The part over the library column fades in.
    QRectF outer;     // rounded rect of the outer part
    QRectF outerKeep; // what of it stays visible
    QRectF inside;
    QRectF content;   // everything is clipped to the visible shape
    if (m_down) {
        const qreal bottom = edge + card * m_t;
        outer = QRectF(half, edge - kBorder - kRadius, W - kBorder, bottom - (edge - kBorder - kRadius) - half);
        outerKeep = QRectF(0, edge - kBorder, W, H);
        inside = QRectF(0, 0, W, edge - kBorder);
        content = QRectF(kBorder, 0, W - 2 * kBorder, bottom - kBorder);
    } else {
        const qreal top = card * (1.0 - m_t);
        outer = QRectF(half, top + half, W - kBorder, (edge - top) + kRadius + kBorder);
        outerKeep = QRectF(0, 0, W, edge + kBorder);
        inside = QRectF(0, edge + kBorder, W, H - edge - kBorder);
        content = QRectF(kBorder, top + kBorder, W - 2 * kBorder, H - top - kBorder);
    }
    const bool outerVisible = m_t > 0.002;

    QPainterPath outerPath;
    outerPath.addRoundedRect(outer, kRadius, kRadius);
    QPainterPath keep;
    keep.addRect(outerKeep);
    if (outerVisible) {
        p.setPen(Qt::NoPen);
        p.setBrush(theme.bg);
        p.drawPath(outerPath.intersected(keep));
    }
    p.fillRect(inside, theme.fade(int(255 * m_t)));

    p.save();
    p.setClipRect(content);
    paintTracks(p, tracksRect());

    RowState header;
    header.hovered = m_hover.part != RowPart::None;
    header.hoverPart = m_hover.part;
    header.active = m_item.uri == m_spotify->activeUri();
    header.playing = m_playing;
    if (m_fadeHeader)
        p.setOpacity(m_t);
    paintRow(p, font(), headerRect(), m_item, m_covers->get(m_item.imageUrl), header);
    p.restore();

    // Border last, on top of everything: the outer part's sides and rounded end.
    if (outerVisible) {
        p.setClipRect(m_down ? QRectF(0, edge - half, W, H) : QRectF(0, 0, W, edge + half));
        p.setPen(QPen(theme.accent, kBorder));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(outer, kRadius, kRadius);
    }
}

void TrackListPopup::paintTracks(QPainter &p, const QRect &list)
{
    // Tracks fade and slide in after the library rows have faded out.
    p.save();
    p.setOpacity(std::clamp((m_t - 0.4) / 0.6, 0.0, 1.0));
    p.setClipRect(list, Qt::IntersectClip);
    p.translate(0, (1.0 - m_t) * 24);
    if (m_state != TracksState::Ready || m_tracks.isEmpty()) {
        QFont f = font();
        f.setPixelSize(11);
        p.setFont(f);
        p.setPen(th().subText);
        const QString text = m_state == TracksState::Loading ? tr("Завантажую треки…")
            : m_state == TracksState::Failed                 ? tr("Не вдалося завантажити треки")
                                                             : tr("Тут порожньо");
        p.drawText(QRect(list.left(), list.top(), list.width(), 80), Qt::AlignCenter, text);
    } else {
        const bool active = m_item.uri == m_spotify->activeUri();
        const int first = m_scroll.offset / kTrackH;
        const int last = std::min(int(m_tracks.size()) - 1, (m_scroll.offset + list.height()) / kTrackH);
        for (int i = first; i <= last; ++i) {
            const Track &t = m_tracks.at(i);
            const QRect row(list.left(), list.top() + i * kTrackH - m_scroll.offset, list.width(), kTrackH);
            const bool current = active && !m_nowTitle.isEmpty() && t.name == m_nowTitle;
            const QImage cover = m_covers->get(t.imageUrl.isEmpty() ? m_item.imageUrl : t.imageUrl);
            paintTrack(p, font(), row, cover, t, m_hover.track == i && !m_scroll.dragged, current, m_playing);
        }
    }
    p.restore();

    if (m_t < 1.0)
        return;
    paintEdgeFades(p, list, m_scroll.offset, maxScroll());
    if (m_hover.track >= 0 || m_scroll.dragged)
        paintScrollbar(p, QRectF(list).adjusted(0, 0, 4, 0), int(m_tracks.size()) * kTrackH, m_scroll.offset,
                       maxScroll());
    p.setPen(th().line);
    p.drawLine(QPointF(list.left() + 4, list.top() - 3.5), QPointF(list.right() - 4, list.top() - 3.5));
}

// ---------------------------------------------------------------- input

void TrackListPopup::wheelEvent(QWheelEvent *event)
{
    m_scroll.wheel(event, kTrackH, maxScroll());
    m_hover = hitTest(logical(event->position()));
    update();
    event->accept();
}

void TrackListPopup::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton)
        return;
    const QPoint pos = logical(event->position());
    m_scroll.press(pos.y());
    m_pressInList = tracksRect().contains(pos);
}

void TrackListPopup::mouseMoveEvent(QMouseEvent *event)
{
    const QPoint pos = logical(event->position());
    if (m_pressInList && m_scroll.drag(pos.y(), maxScroll())) {
        update();
        return;
    }

    const Hover hover = hitTest(pos);
    setCursor(hover.part != RowPart::None || hover.track >= 0 ? Qt::PointingHandCursor : Qt::ArrowCursor);
    if (hover.part != m_hover.part || hover.track != m_hover.track) {
        if (hover.part == RowPart::Nfc)
            QToolTip::showText(event->globalPosition().toPoint(), nfcTooltip(), this);
        else if (m_hover.part == RowPart::Nfc)
            QToolTip::hideText();
        m_hover = hover;
        update();
    }
}

void TrackListPopup::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton || !m_scroll.pressed)
        return;
    if (m_scroll.release()) {
        update();
        return;
    }

    const Hover hit = hitTest(logical(event->position()));
    if (hit.track >= 0) {
        m_spotify->playTrack(m_item, m_tracks, hit.track);
        return;
    }
    switch (hit.part) {
    case RowPart::Art:
        if (m_item.uri == m_spotify->activeUri())
            emit togglePlayRequested();
        else
            m_spotify->play(m_item.uri);
        break;
    case RowPart::Nfc:
        emit writeRecordRequested(m_item);
        break;
    case RowPart::Text:
        emit closeRequested();
        break;
    case RowPart::None:
        break;
    }
}

void TrackListPopup::leaveEvent(QEvent *event)
{
    QWidget::leaveEvent(event);
    m_hover = {};
    update();
}
