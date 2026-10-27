#include "ui/PlaylistScrubPopup.h"

#include "app/AppSettings.h"
#include "ui/library/LibraryStyle.h"

#include <QGuiApplication>
#include <QPainter>
#include <QPainterPath>
#include <QScreen>

#include <algorithm>
#include <cmath>

namespace {
constexpr int kRowH = LibraryStyle::kTrackH;
constexpr int kOuterPad = 6;         // window margin so a box border never gets clipped
constexpr int kPopupW = 240;         // logical px, width of the centre (current-track) box
constexpr int kSideInset = 10;       // prev / next boxes are narrower by this much, each side
constexpr int kRadius = 12;
constexpr int kBorder = 2;
constexpr int kMargin = 10; // gap to the panel it's anchored to, when open
constexpr int kBottomMargin = 4; // collapsed: how close to the screen's own bottom edge it sits
constexpr int kSideMargin = 22; // horizontal gap to the panel too: keeps the two from touching
constexpr int kAnimMs = 220;      // open / close fade: triggered once per scrub session
// Faster than kAnimMs: showPreview() can be re-triggered every ~120 ms while the fader moves,
// and a 220 ms ease barely gets going before the next call restarts it — this settles quickly
// enough to actually keep up and read as responsive instead of laggy.
constexpr int kIndexAnimMs = 110;

// Box height is user-configurable (AppSettings::scrubPopupHeight); the track row inside it
// stays kRowH, so the padding around it just grows or shrinks with the box. Boxes sit flush
// against each other (the slot spacing equals the box height): no gap between them.
int boxHeight() { return AppSettings::instance().scrubPopupHeight(); }
int boxPad() { return std::max(2, (boxHeight() - kRowH) / 2); }
} // namespace

PlaylistScrubPopup::PlaylistScrubPopup(CoverCache *covers, QWidget *owner)
    : ScaledWidget(owner, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::WindowDoesNotAcceptFocus)
    , m_covers(covers)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setAttribute(Qt::WA_MacAlwaysShowToolWindow);

    m_fadeAnim.setDuration(kAnimMs);
    m_fadeAnim.setEasingCurve(QEasingCurve::OutCubic);
    connect(&m_fadeAnim, &QVariantAnimation::valueChanged, this, [this](const QVariant &v) {
        m_t = v.toReal();
        update();
    });
    connect(&m_fadeAnim, &QVariantAnimation::finished, this, [this] {
        if (!m_open)
            hide();
    });

    m_indexAnim.setDuration(kIndexAnimMs);
    m_indexAnim.setEasingCurve(QEasingCurve::OutCubic);
    connect(&m_indexAnim, &QVariantAnimation::valueChanged, this, [this](const QVariant &v) {
        m_centerIndex = v.toReal();
        update();
    });

    if (m_covers)
        connect(m_covers, &CoverCache::coverReady, this, qOverload<>(&QWidget::update));
    connect(&AppSettings::instance(), &AppSettings::changed, this, [this] {
        applyGeometry();
        update();
    });
}

void PlaylistScrubPopup::showPreview(const QList<Track> &tracks, int index)
{
    if (tracks.isEmpty() || index < 0 || index >= tracks.size())
        return;
    m_tracks = tracks;
    const qreal target = index;

    if (!m_open) {
        m_open = true;
        m_centerIndex = target; // fresh scrub session: start centred, nothing to ease from yet
        applyGeometry();
        show();
        raise();
        m_fadeAnim.stop();
        m_fadeAnim.setStartValue(m_t);
        m_fadeAnim.setEndValue(1.0);
        m_fadeAnim.start();
        update();
        return;
    }
    if (std::abs(m_centerIndex - target) < 0.01) {
        update(); // same track, new cover/metadata maybe: repaint, nothing to animate
        return;
    }
    m_indexAnim.stop();
    m_indexAnim.setStartValue(m_centerIndex);
    m_indexAnim.setEndValue(target);
    m_indexAnim.start();
}

void PlaylistScrubPopup::hidePreview()
{
    if (!m_open)
        return;
    m_open = false;
    m_fadeAnim.stop();
    m_fadeAnim.setStartValue(m_t);
    m_fadeAnim.setEndValue(0.0);
    m_fadeAnim.start();
}

void PlaylistScrubPopup::setAnchor(const QRect &panelGlobalRect, bool rightSide, bool panelExpanded)
{
    m_panelRect = panelGlobalRect;
    m_rightSide = rightSide;
    m_panelExpanded = panelExpanded;
    m_haveAnchor = true;
    applyGeometry();
}

void PlaylistScrubPopup::applyGeometry()
{
    if (!m_haveAnchor)
        return;
    const qreal s = uiScale();
    const QSize size(int(std::lround((kPopupW + 2 * kOuterPad) * s)),
                      int(std::lround((3 * boxHeight() + 2 * kOuterPad) * s)));
    const int x = m_rightSide ? m_panelRect.right() + 1 - size.width() - kSideMargin
                              : m_panelRect.left() + kSideMargin;

    QScreen *screen = QGuiApplication::screenAt(m_panelRect.center());
    if (!screen)
        screen = QGuiApplication::primaryScreen();
    const QRect area = screen ? screen->availableGeometry() : QRect(0, 0, 1280, 720);

    int y;
    if (!m_panelExpanded) {
        // Collapsed: just the handle, nobody is looking at it right now — sit low, almost
        // against the screen's own bottom edge (by the taskbar), not wherever the handle itself
        // happens to be docked vertically (it can sit anywhere from the "position" setting, or
        // collapse to nothing when hidden entirely — see EdgePanel::handleHeight()).
        y = area.bottom() + 1 - size.height() - int(std::lround(kBottomMargin * s));
    } else {
        // Open: the library list is right there — go above the panel instead, so this never
        // covers rows the person is actually looking at.
        y = m_panelRect.top() - kMargin - size.height();
    }
    y = std::max(y, area.top());
    setGeometry(x, y, size.width(), size.height());
}

void PlaylistScrubPopup::paintEvent(QPaintEvent *)
{
    if (m_t <= 0.001 || m_tracks.isEmpty())
        return;

    QPainter p(this);
    prepare(p);
    const Theme theme = Theme::current();
    const qreal W = lw();
    const qreal H = lh();
    const qreal centerY = H / 2.0;

    const int base = int(std::floor(m_centerIndex));
    const qreal frac = m_centerIndex - base;
    const int nearest = int(std::lround(m_centerIndex));
    const int boxH = boxHeight();
    const int pad = boxPad();

    // Three separate boxes flush against each other (not one shared panel): the centre one is
    // the track the fader is on right now, full width and accent-bordered; its neighbours are
    // narrower with a plain border, like a wheel.
    for (int d = -1; d <= 1; ++d) {
        const int idx = base + d;
        if (idx < 0 || idx >= m_tracks.size())
            continue;
        const bool current = idx == nearest;
        const qreal boxCenterY = centerY + (d - frac) * boxH;
        const qreal inset = current ? 0.0 : qreal(kSideInset);
        const QRectF box(kOuterPad + inset, boxCenterY - boxH / 2.0, W - 2 * kOuterPad - 2 * inset, boxH);

        // Solid at rest (d = -1, 0, 1 are the three resting slots); fades out only over the
        // half-slot beyond that, where a box is actively sliding out of view mid-animation.
        const qreal dy = std::abs(boxCenterY - centerY);
        const qreal edgeFade = std::clamp((1.5 * boxH - dy) / (0.5 * boxH), 0.0, 1.0);
        const qreal opacity = m_t * edgeFade;
        if (opacity <= 0.01)
            continue;

        QPainterPath boxPath;
        boxPath.addRoundedRect(box, kRadius, kRadius);
        p.save();
        p.setOpacity(opacity);
        p.setPen(Qt::NoPen);
        p.setBrush(theme.bg);
        p.drawPath(boxPath);
        p.setPen(QPen(current ? theme.accent : theme.line, kBorder));
        p.setBrush(Qt::NoBrush);
        p.drawPath(boxPath);

        p.setClipPath(boxPath);
        const QRect row(int(box.left()) + pad, int(box.top()) + pad, int(box.width()) - 2 * pad, kRowH);
        const Track &track = m_tracks.at(idx);
        const QImage cover = m_covers ? m_covers->get(track.imageUrl) : QImage();
        // "current" here would also draw paintTrack()'s own row border: the box itself already
        // shows which one is current (bigger, accented), so that would just double it up.
        LibraryStyle::paintTrack(p, font(), row, cover, track, false, false, false);
        p.restore();
    }
}
