#include "ui/VolumeOverlay.h"

#include "app/AppSettings.h"

#include <QGuiApplication>
#include <QPainterPath>
#include <QScreen>

#include <algorithm>
#include <cmath>

namespace {
constexpr int kW = 44; // logical px
constexpr int kH = 176;
constexpr int kRadius = 14;
constexpr int kBorder = 2;
constexpr int kMargin = 8;       // gap to the screen edges and the panel
constexpr int kTrackW = 6;
constexpr int kTrackTop = 16;
constexpr int kTrackBottom = 42; // room for the speaker under the scale
constexpr int kIconY = kH - 22;  // speaker centre
constexpr int kShownMs = 1500;   // stays this long after the last change
constexpr int kFadeMs = 180;
} // namespace

VolumeOverlay::VolumeOverlay(QWidget *owner)
    : ScaledWidget(owner, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint
                              | Qt::WindowDoesNotAcceptFocus | Qt::WindowTransparentForInput)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setAttribute(Qt::WA_MacAlwaysShowToolWindow);

    m_fadeAnim.setDuration(kFadeMs);
    m_fadeAnim.setEasingCurve(QEasingCurve::OutCubic);
    connect(&m_fadeAnim, &QVariantAnimation::valueChanged, this, [this](const QVariant &v) {
        m_t = v.toReal();
        update();
    });
    connect(&m_fadeAnim, &QVariantAnimation::finished, this, [this] {
        if (m_t <= 0.0)
            hide();
    });

    m_hideTimer.setSingleShot(true);
    m_hideTimer.setInterval(kShownMs);
    connect(&m_hideTimer, &QTimer::timeout, this, [this] { fadeTo(0.0); });

    connect(&AppSettings::instance(), &AppSettings::changed, this, qOverload<>(&QWidget::update));
}

void VolumeOverlay::showLevel(int percent)
{
    m_percent = std::clamp(percent, 0, 100);
    setGeometry(slot(parentWidget()));
    show();
    fadeTo(1.0);
    m_hideTimer.start();
    update();
}

void VolumeOverlay::fadeTo(qreal value)
{
    if (m_fadeAnim.state() == QAbstractAnimation::Running && m_fadeAnim.endValue().toReal() == value)
        return;
    if (m_fadeAnim.state() != QAbstractAnimation::Running && m_t == value)
        return;
    m_fadeAnim.stop();
    m_fadeAnim.setStartValue(m_t);
    m_fadeAnim.setEndValue(value);
    m_fadeAnim.start();
}

QRect VolumeOverlay::slot(const QWidget *panel)
{
    QScreen *screen = QGuiApplication::primaryScreen();
    const QRect area = screen ? screen->availableGeometry() : QRect(0, 0, 1280, 720);
    const qreal scale = uiScale();
    const int margin = int(std::lround(kMargin * scale));
    QRect rect(0, 0, int(std::lround(kW * scale)), int(std::lround(kH * scale)));
    rect.moveBottomRight(area.bottomRight() - QPoint(margin, margin));

    if (panel && panel->isVisible() && panel->frameGeometry().intersects(rect))
        rect.moveRight(panel->frameGeometry().left() - 1 - margin);
    return rect;
}

void VolumeOverlay::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    prepare(p);
    p.setOpacity(m_t);
    const Theme t = Theme::current();

    p.setPen(QPen(t.accent, kBorder));
    p.setBrush(t.bg);
    const qreal inset = kBorder / 2.0;
    p.drawRoundedRect(QRectF(inset, inset, kW - kBorder, kH - kBorder), kRadius, kRadius);

    // The scale: the filled part grows from the bottom.
    const QRectF track((kW - kTrackW) / 2.0, kTrackTop, kTrackW, kH - kTrackTop - kTrackBottom);
    p.setPen(Qt::NoPen);
    p.setBrush(t.track);
    p.drawRoundedRect(track, kTrackW / 2.0, kTrackW / 2.0);
    if (m_percent > 0) {
        const qreal filled = std::max<qreal>(kTrackW, track.height() * m_percent / 100.0);
        p.setBrush(t.accent);
        p.drawRoundedRect(QRectF(track.left(), track.bottom() - filled, kTrackW, filled), kTrackW / 2.0, kTrackW / 2.0);
    }

    // Speaker in the theme's colour: waves follow the level, a cross when silent.
    const qreal cx = kW / 2.0 - 5.0;
    QPainterPath speaker;
    speaker.moveTo(cx - 6.0, kIconY - 3.0);
    speaker.lineTo(cx - 2.5, kIconY - 3.0);
    speaker.lineTo(cx + 2.5, kIconY - 7.0);
    speaker.lineTo(cx + 2.5, kIconY + 7.0);
    speaker.lineTo(cx - 2.5, kIconY + 3.0);
    speaker.lineTo(cx - 6.0, kIconY + 3.0);
    speaker.closeSubpath();
    p.setBrush(t.accent);
    p.drawPath(speaker);

    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(t.accent, 1.8, Qt::SolidLine, Qt::RoundCap));
    if (m_percent == 0) {
        p.drawLine(QPointF(cx + 6.0, kIconY - 3.5), QPointF(cx + 13.0, kIconY + 3.5));
        p.drawLine(QPointF(cx + 13.0, kIconY - 3.5), QPointF(cx + 6.0, kIconY + 3.5));
    } else {
        const int waves = m_percent > 66 ? 3 : m_percent > 33 ? 2 : 1;
        for (int i = 0; i < waves; ++i) {
            const qreal r = 5.0 + 3.5 * i;
            p.drawArc(QRectF(cx + 2.5 - r, kIconY - r, 2 * r, 2 * r), -50 * 16, 100 * 16);
        }
    }
}
