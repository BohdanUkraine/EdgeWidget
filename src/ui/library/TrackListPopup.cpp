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
// Faster than kAnimMs, and shared by the scroll offset and the box position (see m_scrollAnim /
// m_boxAnim): previewIndex() can be re-triggered every ~120 ms while the device fader is
// scrubbing, and a 260 ms ease barely gets going before the next call restarts it — this settles
// quickly enough to actually keep up and read as responsive instead of laggy.
constexpr int kPreviewAnimMs = 110;
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

    m_scrollAnim.setDuration(kPreviewAnimMs);
    m_scrollAnim.setEasingCurve(QEasingCurve::OutCubic);
    connect(&m_scrollAnim, &QVariantAnimation::valueChanged, this, [this](const QVariant &v) {
        m_scroll.offset = v.toInt();
        update();
    });

    m_boxAnim.setDuration(kPreviewAnimMs);
    m_boxAnim.setEasingCurve(QEasingCurve::OutCubic);
    connect(&m_boxAnim, &QVariantAnimation::valueChanged, this, [this](const QVariant &v) {
        m_boxY = v.toReal();
        update();
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
    m_previewIndex = -1;
    m_previewPinned = false;
    m_boxAnim.stop();
    m_boxPositioned = false;

    const int id = ++m_requestId;
    m_spotify->fetchTracks(item, [this, id](const QList<Track> &tracks, bool ok) {
        if (id != m_requestId)
            return;
        m_tracks = tracks;
        m_state = ok ? TracksState::Ready : TracksState::Failed;
        syncPreviewFromNowTitle();
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
    // The API caught up with a pinned pick (or there was never one): safe to follow it again.
    if (m_previewPinned && m_previewIndex >= 0 && m_previewIndex < m_tracks.size()
        && m_tracks.at(m_previewIndex).name == title)
        m_previewPinned = false;
    if (!m_previewPinned)
        syncPreviewFromNowTitle();
    update();
}

void TrackListPopup::syncPreviewFromNowTitle()
{
    m_previewIndex = -1;
    if (!m_nowTitle.isEmpty()) {
        for (int i = 0; i < m_tracks.size(); ++i) {
            if (m_tracks.at(i).name == m_nowTitle) {
                m_previewIndex = i;
                break;
            }
        }
    }
    // Passive sync (the real track changed by some other means): just box wherever that row
    // already sits — unlike previewIndex() below, this never scrolls the list on its own.
    if (m_previewIndex < 0)
        m_boxPositioned = false;
    else
        animateBoxTo(m_previewIndex * kTrackH - m_scroll.offset);
}

void TrackListPopup::animateBoxTo(qreal target)
{
    if (!m_boxPositioned) {
        m_boxPositioned = true;
        m_boxAnim.stop();
        m_boxY = target;
        update();
        return;
    }
    if (std::abs(m_boxY - target) < 0.5)
        return;
    m_boxAnim.stop();
    m_boxAnim.setStartValue(m_boxY);
    m_boxAnim.setEndValue(target);
    m_boxAnim.start();
}

// Called every ~120 ms while the device fader scrubs: pins the box to this track right away,
// independent of whatever Spotify's API confirms (or never confirms, while paused) — see
// DeviceController::playlistScrub().
void TrackListPopup::previewIndex(int index)
{
    if (!m_open || index < 0 || index >= int(m_tracks.size()))
        return;
    m_previewPinned = true;
    if (m_previewIndex != index) {
        m_previewIndex = index;
        update();
    }
    const int raw = index * kTrackH - (tracksRect().height() - kTrackH) / 2;
    const int target = std::clamp(raw, 0, maxScroll());
    // Enough tracks above and below to actually reach the centre: the box can stay put there
    // and let the list scroll under it. Too close to either end of the playlist, and it just
    // settles wherever that row lands instead (same as scrolling any list to its start / end) —
    // either way animateBoxTo() eases it there, so picking through several tracks in a row
    // (even ones that can't be centred) glides instead of jumping row to row.
    const bool centered = (target == raw);
    if (target != m_scroll.offset) {
        m_scrollAnim.stop();
        m_scrollAnim.setStartValue(m_scroll.offset);
        m_scrollAnim.setEndValue(target);
        m_scrollAnim.start();
    }
    animateBoxTo(centered ? (tracksRect().height() - kTrackH) / 2.0 : qreal(index * kTrackH - target));
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
        const bool showBox = active && m_previewIndex >= 0 && m_boxPositioned;
        const int first = m_scroll.offset / kTrackH;
        const int last = std::min(int(m_tracks.size()) - 1, (m_scroll.offset + list.height()) / kTrackH);
        for (int i = first; i <= last; ++i) {
            const Track &t = m_tracks.at(i);
            const QRect row(list.left(), list.top() + i * kTrackH - m_scroll.offset, list.width(), kTrackH);
            const bool current = active && i == m_previewIndex;
            const QImage cover = m_covers->get(t.imageUrl.isEmpty() ? m_item.imageUrl : t.imageUrl);
            paintTrack(p, font(), row, cover, t, m_hover.track == i && !m_scroll.dragged, current, m_playing,
                       /*drawBorder=*/!showBox);
        }
        // A single overlay eased independently of any one row (see animateBoxTo()): it glides
        // between positions smoothly, instead of a per-row border snapping row to row.
        if (showBox) {
            const QRect box(list.left(), list.top() + int(std::lround(m_boxY)), list.width(), kTrackH);
            p.setPen(QPen(th().accent, 2));
            p.setBrush(Qt::NoBrush);
            p.drawRoundedRect(QRectF(box).adjusted(1, 1, -7, -1), 6, 6);
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
        m_previewPinned = true;
        if (m_previewIndex != hit.track) {
            m_previewIndex = hit.track;
            update();
        }
        animateBoxTo(hit.track * kTrackH - m_scroll.offset); // already visible: just box it, don't scroll
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
