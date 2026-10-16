#include "ui/PlayerView.h"

#include "ui/library/LibraryStyle.h"
#include "playback/MediaController.h"
#include "ui/PanelLayout.h"
#include "spotify/SpotifyLauncher.h"

#include <QMouseEvent>
#include <QPainterPath>

#include <algorithm>
#include <cmath>

using namespace PanelLayout;

namespace {
constexpr int kVinylCx = 91;
constexpr int kVinylCy = 82;
constexpr int kVinylR = 64;
constexpr int kLabelR = 24;
constexpr int kBottomRowY = 176; // buttons and progress bar
constexpr int kButtonStep = 30;
constexpr int kNowPlayingY = 146;
constexpr int kTimeW = 34;
constexpr int kCoverPx = 128;       // cached cover size for the label (HiDPI-friendly)
constexpr qreal kDegPerSec = 200.0; // 33 1/3 rpm
} // namespace

PlayerView::PlayerView(MediaController *media, QWidget *parent)
    : ScaledWidget(parent)
    , m_media(media)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setMouseTracking(true);

    m_frameTimer.setTimerType(Qt::PreciseTimer);
    connect(&m_frameTimer, &QTimer::timeout, this, &PlayerView::tick);
    connect(m_media, &MediaController::stateChanged, this, &PlayerView::onState);
    connect(&SpotifyLauncher::instance(), &SpotifyLauncher::launchingChanged, this, qOverload<>(&QWidget::update));
    connect(&AppSettings::instance(), &AppSettings::changed, this, qOverload<>(&QWidget::update));
    m_sinceFrame.start();
    onState(m_media->state());
}

void PlayerView::onState(const MediaState &state)
{
    if (state.cover.cacheKey() != m_state.cover.cacheKey() || (m_cover.isNull() && !state.cover.isNull())) {
        m_cover = state.cover.isNull()
            ? QImage()
            : state.cover.scaled(kCoverPx, kCoverPx, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
    }
    m_state = state;
    updateSpin();
    update();
}

void PlayerView::setRecordColor(const QColor &color)
{
    m_recordColor = color;
    update();
}

// ---------------------------------------------------------------- spinning

void PlayerView::updateSpin()
{
    if (isVisible() && m_state.playing) {
        if (!m_frameTimer.isActive()) {
            m_sinceFrame.restart();
            m_frameTimer.start(16);
        }
    } else {
        m_frameTimer.stop();
    }
}

void PlayerView::tick()
{
    m_angle = std::fmod(m_angle + kDegPerSec * m_sinceFrame.restart() / 1000.0, 360.0);
    update();
}

void PlayerView::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    updateSpin();
}

void PlayerView::hideEvent(QHideEvent *event)
{
    QWidget::hideEvent(event);
    updateSpin();
}

// ---------------------------------------------------------------- geometry

qint64 PlayerView::displayedPosition() const
{
    return m_seeking ? m_seekPreview : m_state.positionNow();
}

QRect PlayerView::buttonRect(Hit button) const
{
    const int index = int(button) - int(Shuffle); // Shuffle..Repeat -> 0..4
    const int cx = kVinylCx + (index - 2) * kButtonStep;
    const int size = button == Play ? 30 : 26;
    return QRect(cx - size / 2, kBottomRowY - size / 2, size, size);
}

QRect PlayerView::progressRect() const
{
    const int left = kRightX + kTimeW + 6;
    const int right = kRightEnd - kTimeW - 6;
    return QRect(left, kBottomRowY - 8, right - left, 16);
}

qint64 PlayerView::positionAt(int x) const
{
    const QRect r = progressRect();
    const qreal t = std::clamp(qreal(x - r.left()) / std::max(1, r.width()), 0.0, 1.0);
    return qint64(t * m_state.durationMs);
}

PlayerView::Hit PlayerView::hitTest(const QPoint &pos) const
{
    const QPointF d = QPointF(pos) - QPointF(kVinylCx, kVinylCy);
    const bool onVinyl = d.x() * d.x() + d.y() * d.y() <= kVinylR * kVinylR;
    if (!m_state.available) {
        // Nothing to control yet, but play still works: it starts Spotify quietly.
        if (buttonRect(Play).adjusted(-2, -2, 2, 2).contains(pos))
            return Play;
        return onVinyl ? Vinyl : None;
    }
    if (onVinyl)
        return Vinyl;
    for (Hit b : { Shuffle, Prev, Play, Next, Repeat }) {
        if (buttonRect(b).adjusted(-2, -2, 2, 2).contains(pos))
            return b;
    }
    if (m_state.durationMs > 0 && progressRect().adjusted(-4, -4, 4, 4).contains(pos))
        return Progress;
    return None;
}

// ---------------------------------------------------------------- input

void PlayerView::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton)
        return;
    const QPoint pos = logical(event->position());
    // Act on press, not release: feels instant.
    switch (hitTest(pos)) {
    case Shuffle: m_media->toggleShuffle(); break;
    case Prev: m_media->previous(); break;
    case Play:
    case Vinyl: m_media->togglePlayPause(); break;
    case Next: m_media->next(); break;
    case Repeat: m_media->cycleRepeat(); break;
    case Progress:
        m_seeking = true;
        m_seekPreview = positionAt(pos.x());
        update();
        break;
    case None: break;
    }
}

void PlayerView::mouseMoveEvent(QMouseEvent *event)
{
    const QPoint pos = logical(event->position());
    if (m_seeking) {
        m_seekPreview = positionAt(pos.x());
        update();
        return;
    }
    const Hit hover = hitTest(pos);
    if (hover != m_hover) {
        m_hover = hover;
        setCursor(hover == None ? Qt::ArrowCursor : Qt::PointingHandCursor);
        update();
    }
}

void PlayerView::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && m_seeking) {
        m_seeking = false;
        m_media->seek(m_seekPreview);
        update();
    }
}

void PlayerView::leaveEvent(QEvent *event)
{
    QWidget::leaveEvent(event);
    m_hover = None;
    update();
}

// ---------------------------------------------------------------- painting

void PlayerView::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    prepare(p);
    paintVinyl(p);
    paintNowPlaying(p);
    for (Hit b : { Shuffle, Prev, Play, Next, Repeat })
        paintButton(p, b);
    paintProgress(p);
}

void PlayerView::paintVinyl(QPainter &p)
{
    const Theme th = Theme::current();
    const QPointF c(kVinylCx, kVinylCy);

    p.save();
    p.translate(c);

    // Black vinyl, or coloured vinyl in the colour of the record lying on the player.
    QRadialGradient disc(QPointF(), kVinylR);
    disc.setColorAt(0.0, m_recordColor.isValid() ? m_recordColor.darker(130) : QColor(26, 26, 26));
    disc.setColorAt(1.0, m_recordColor.isValid() ? m_recordColor.darker(260) : QColor(8, 8, 8));
    p.setPen(QPen(m_hover == Vinyl ? th.accent : QColor(55, 55, 55), m_hover == Vinyl ? 1.5 : 1));
    p.setBrush(disc);
    p.drawEllipse(QPointF(), kVinylR, kVinylR);

    p.setBrush(Qt::NoBrush);
    for (int r = kLabelR + 4; r < kVinylR - 2; r += 3) { // grooves
        p.setPen(QPen(QColor(255, 255, 255, (r / 3) % 2 ? 14 : 6), 1));
        p.drawEllipse(QPointF(), r, r);
    }

    // Label with the album cover spins with the record.
    p.rotate(m_angle);
    QPainterPath label;
    label.addEllipse(QPointF(), kLabelR, kLabelR);
    if (!m_cover.isNull() && m_state.available) {
        p.save();
        p.setClipPath(label);
        // Windows thumbnails from Spotify have a frame and a logo strip: use their centre.
        // Web API covers (music on another device) are clean.
        const QRectF src = m_state.remote ? QRectF(m_cover.rect())
                                          : QRectF(m_cover.rect()).adjusted(kCoverPx * 0.18, kCoverPx * 0.12,
                                                                            -kCoverPx * 0.18, -kCoverPx * 0.24);
        p.drawImage(QRectF(-kLabelR, -kLabelR, 2 * kLabelR, 2 * kLabelR), m_cover, src);
        p.restore();
    } else {
        p.setPen(Qt::NoPen);
        p.setBrush(th.accent);
        p.drawPath(label);
        p.setBrush(QColor(255, 255, 255, 170));
        p.drawEllipse(QPointF(0, -kLabelR * 0.62), 2.5, 2.5);
    }
    p.setPen(Qt::NoPen);
    p.setBrush(Qt::black);
    p.drawEllipse(QPointF(), 3, 3); // spindle hole
    p.restore();

    // Static light reflection on the grooves (does not rotate, so the record looks lit).
    QConicalGradient sheen(c, 45);
    const QColor clear(255, 255, 255, 0);
    const QColor shine(255, 255, 255, 30);
    for (qreal at : { 0.0, 0.12, 0.5, 0.62, 1.0 })
        sheen.setColorAt(at, clear);
    sheen.setColorAt(0.06, shine);
    sheen.setColorAt(0.56, shine);
    QPainterPath ring;
    ring.addEllipse(c, kVinylR - 1, kVinylR - 1);
    ring.addEllipse(c, kLabelR + 1, kLabelR + 1);
    p.setPen(Qt::NoPen);
    p.setBrush(sheen);
    p.drawPath(ring);
}

void PlayerView::paintNowPlaying(QPainter &p)
{
    const Theme th = Theme::current();
    const QRect r(kRightX + 2, kNowPlayingY, kRightEnd - kRightX - 2, 20);
    QFont title = font();
    title.setPixelSize(12);
    title.setBold(true);
    QFont sub = font();
    sub.setPixelSize(11);

    const bool launching = SpotifyLauncher::instance().isLaunching();
    const QString name = launching ? tr("Запускаю Spotify…")
        : m_state.available && !m_state.title.isEmpty() ? m_state.title : tr("Нічого не грає");
    QString artist = m_state.available && !launching ? m_state.artist : QString();
    if (m_state.remote && !m_state.deviceName.isEmpty() && !launching)
        artist += QStringLiteral("  ·  ") + m_state.deviceName; // playing on another device

    const QFontMetrics tm(title);
    const QString shownTitle = tm.elidedText(name, Qt::ElideRight, r.width() * 2 / 3);
    const int titleW = tm.horizontalAdvance(shownTitle);
    p.setFont(title);
    p.setPen(th.text);
    p.drawText(QRect(r.x(), r.y(), titleW + 1, r.height()), Qt::AlignLeft | Qt::AlignVCenter, shownTitle);

    if (!artist.isEmpty()) {
        const int x = r.x() + titleW;
        const int w = r.right() - x;
        p.setFont(sub);
        p.setPen(th.subText);
        p.drawText(QRect(x, r.y(), w, r.height()), Qt::AlignLeft | Qt::AlignVCenter,
                   QFontMetrics(sub).elidedText(QStringLiteral("  •  ") + artist, Qt::ElideRight, w));
    }
}

void PlayerView::paintButton(QPainter &p, Hit button)
{
    const Theme th = Theme::current();
    const bool hovered = m_hover == button;
    const bool active = (button == Shuffle && m_state.shuffle) || (button == Repeat && m_state.repeat != 0);

    QColor color = active ? th.accent : (hovered ? th.text : th.subText);
    if (!m_state.available)
        color.setAlpha(80);

    p.save();
    p.translate(QRectF(buttonRect(button)).center());

    switch (button) {
    case Play: {
        const qreal radius = hovered ? 15.0 : 14.0;
        p.setPen(Qt::NoPen);
        p.setBrush(th.text);
        p.drawEllipse(QPointF(), radius, radius);
        p.setBrush(th.bg);
        if (m_state.playing) {
            p.drawRect(QRectF(-5, -6, 3.5, 12));
            p.drawRect(QRectF(1.5, -6, 3.5, 12));
        } else {
            p.drawPolygon(QPolygonF({ QPointF(-3.5, -6.5), QPointF(-3.5, 6.5), QPointF(6.5, 0) }));
        }
        break;
    }
    case Prev:
    case Next:
        if (button == Next)
            p.scale(-1, 1);
        p.setPen(Qt::NoPen);
        p.setBrush(color);
        p.drawPolygon(QPolygonF({ QPointF(6, -6), QPointF(6, 6), QPointF(-3, 0) }));
        p.drawRoundedRect(QRectF(-6.5, -6, 2.2, 12), 1, 1);
        break;
    case Shuffle: {
        QPainterPath path;
        path.moveTo(-7, -5);
        path.cubicTo(-1, -5, 1, 5, 7, 5);
        path.moveTo(-7, 5);
        path.cubicTo(-1, 5, 1, -5, 7, -5);
        path.moveTo(4.5, -7.5); // arrow heads
        path.lineTo(7, -5);
        path.lineTo(4.5, -2.5);
        path.moveTo(4.5, 2.5);
        path.lineTo(7, 5);
        path.lineTo(4.5, 7.5);
        p.setPen(QPen(color, 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.setBrush(Qt::NoBrush);
        p.drawPath(path);
        break;
    }
    case Repeat: {
        p.setPen(QPen(color, 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(QRectF(-7, -5, 14, 10), 3, 3);
        p.drawPolyline(QPolygonF({ QPointF(-1, -7.5), QPointF(1.5, -5), QPointF(-1, -2.5) }));
        if (m_state.repeat == 1) { // repeat one track
            QFont f = font();
            f.setPixelSize(8);
            f.setBold(true);
            p.setFont(f);
            p.drawText(QRectF(-5, -4, 10, 9), Qt::AlignCenter, QStringLiteral("1"));
        }
        break;
    }
    default:
        break;
    }

    if (active) {
        p.setPen(Qt::NoPen);
        p.setBrush(th.accent);
        p.drawEllipse(QPointF(0, 11), 1.5, 1.5);
    }
    p.restore();
}

void PlayerView::paintProgress(QPainter &p)
{
    const Theme th = Theme::current();
    const QRect r = progressRect();
    const qreal y = r.center().y() + 0.5;
    const qint64 pos = displayedPosition();
    const qreal ratio = m_state.durationMs > 0 ? qreal(pos) / m_state.durationMs : 0.0;
    const bool active = m_hover == Progress || m_seeking;
    const bool known = m_state.available && m_state.durationMs > 0;

    QFont f = font();
    f.setPixelSize(10);
    p.setFont(f);
    p.setPen(th.subText);
    p.drawText(QRect(kRightX, r.y(), kTimeW, r.height()), Qt::AlignRight | Qt::AlignVCenter,
               known ? LibraryStyle::formatTime(pos) : QStringLiteral("-:--"));
    p.drawText(QRect(r.right() + 7, r.y(), kTimeW, r.height()), Qt::AlignLeft | Qt::AlignVCenter,
               known ? LibraryStyle::formatTime(m_state.durationMs) : QStringLiteral("-:--"));

    p.setPen(Qt::NoPen);
    p.setBrush(th.track);
    p.drawRoundedRect(QRectF(r.left(), y - 2, r.width(), 4), 2, 2);
    p.setBrush(active ? th.accent : th.text);
    p.drawRoundedRect(QRectF(r.left(), y - 2, r.width() * ratio, 4), 2, 2);
    if (active) {
        p.setBrush(th.text);
        p.drawEllipse(QPointF(r.left() + r.width() * ratio, y), 6, 6);
    }
}
