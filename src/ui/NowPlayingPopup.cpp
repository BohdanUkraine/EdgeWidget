#include "ui/NowPlayingPopup.h"

#include "app/AppSettings.h"
#include "ui/VolumeOverlay.h"
#include "ui/library/LibraryStyle.h"

#include <QGuiApplication>
#include <QPainterPath>
#include <QScreen>

#include <cmath>

namespace {
constexpr int kW = 300;
constexpr int kH = 76;
constexpr int kRadius = 14;
constexpr int kBorder = 2;
constexpr int kMargin = 8;
constexpr int kArt = 52;
constexpr int kArtX = 12;
constexpr int kDiscR = 25;
constexpr int kDiscOut = 20; // how far the record sticks out of its sleeve
constexpr int kDiscX = kArtX + kArt - kDiscR + kDiscOut;
constexpr int kTextX = kDiscX + kDiscR + 12;
constexpr int kTextRight = 14;
constexpr int kShownMs = 2000;
constexpr int kFadeMs = 180;
}

NowPlayingPopup::NowPlayingPopup(SpotifyClient *spotify, CoverCache *covers, QWidget *owner)
    : ScaledWidget(owner, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint
                              | Qt::WindowDoesNotAcceptFocus | Qt::WindowTransparentForInput)
    , m_spotify(spotify)
    , m_covers(covers)
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

    if (m_covers)
        connect(m_covers, &CoverCache::coverReady, this, qOverload<>(&QWidget::update));
    connect(&AppSettings::instance(), &AppSettings::changed, this, [this] {
        if (isVisible())
            setGeometry(slot());
        update();
    });
}

void NowPlayingPopup::showRecord(const QString &contextUri, const QColor &recordColor)
{
    const int request = ++m_request;
    m_spotify->fetchItem(contextUri, [this, request, recordColor](const LibraryItem &item) {
        if (request == m_request && !item.uri.isEmpty())
            showItem(item, recordColor);
    });
}

void NowPlayingPopup::dismiss()
{
    ++m_request;
    m_hideTimer.stop();
    fadeTo(0.0);
}

void NowPlayingPopup::showItem(const LibraryItem &item, const QColor &recordColor)
{
    m_item = item;
    m_color = recordColor;
    setGeometry(slot());
    show();
    raise();
    fadeTo(1.0);
    m_hideTimer.start();
    update();
}

void NowPlayingPopup::fadeTo(qreal value)
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

// The corner of the panel's screen, left of the panel's handle and of the volume scale.
QRect NowPlayingPopup::slot() const
{
    const QWidget *panel = parentWidget();
    QScreen *screen = panel ? panel->screen() : QGuiApplication::primaryScreen();
    const QRect area = screen ? screen->availableGeometry() : QRect(0, 0, 1280, 720);
    const qreal scale = uiScale();
    const int margin = int(std::lround(kMargin * scale));
    QRect rect(0, 0, int(std::lround(kW * scale)), int(std::lround(kH * scale)));
    rect.moveBottomRight(area.bottomRight() - QPoint(margin, margin));

    if (panel && panel->isVisible() && panel->frameGeometry().intersects(rect))
        rect.moveRight(panel->frameGeometry().left() - 1 - margin);
    if (AppSettings::instance().volumeTarget() == AppSettings::VolumeTarget::Spotify) {
        const QRect volume = VolumeOverlay::slot(panel);
        if (rect.intersects(volume))
            rect.moveRight(volume.left() - 1 - margin);
    }
    return rect;
}

void NowPlayingPopup::paintEvent(QPaintEvent *)
{
    if (m_t <= 0.001)
        return;
    QPainter p(this);
    prepare(p);
    p.setOpacity(m_t);
    const Theme t = Theme::current();

    p.setPen(QPen(t.accent, kBorder));
    p.setBrush(t.bg);
    const qreal inset = kBorder / 2.0;
    p.drawRoundedRect(QRectF(inset, inset, kW - kBorder, kH - kBorder), kRadius, kRadius);

    paintRecord(p);

    const int textW = kW - kTextX - kTextRight;
    QFont caption = font();
    caption.setPixelSize(10);
    caption.setBold(true);
    caption.setCapitalization(QFont::AllUppercase);
    caption.setLetterSpacing(QFont::AbsoluteSpacing, 0.8);
    p.setFont(caption);
    p.setPen(t.accent);
    p.drawText(QRect(kTextX, 13, textW, 14), Qt::AlignLeft | Qt::AlignVCenter, tr("Зараз грає"));

    QFont title = font();
    title.setPixelSize(14);
    title.setBold(true);
    p.setFont(title);
    p.setPen(t.text);
    p.drawText(QRect(kTextX, 28, textW, 20), Qt::AlignLeft | Qt::AlignVCenter,
               QFontMetrics(title).elidedText(LibraryStyle::itemName(m_item), Qt::ElideRight, textW));

    QFont sub = font();
    sub.setPixelSize(11);
    p.setFont(sub);
    p.setPen(t.subText);
    p.drawText(QRect(kTextX, 48, textW, 16), Qt::AlignLeft | Qt::AlignVCenter,
               QFontMetrics(sub).elidedText(LibraryStyle::itemSubtitle(m_item), Qt::ElideRight, textW));
}

// The record half out of its sleeve, in the colour written on the tag, like the panel's record.
void NowPlayingPopup::paintRecord(QPainter &p)
{
    const QPointF c(kDiscX, kH / 2.0);
    QRadialGradient disc(c, kDiscR);
    disc.setColorAt(0.0, m_color.isValid() ? m_color.darker(130) : QColor(26, 26, 26));
    disc.setColorAt(1.0, m_color.isValid() ? m_color.darker(260) : QColor(8, 8, 8));
    p.setPen(QPen(QColor(55, 55, 55), 1));
    p.setBrush(disc);
    p.drawEllipse(c, kDiscR, kDiscR);

    p.setBrush(Qt::NoBrush);
    for (int r = 9; r < kDiscR - 1; r += 2) {
        p.setPen(QPen(QColor(255, 255, 255, (r / 2) % 2 ? 14 : 6), 1));
        p.drawEllipse(c, r, r);
    }

    QConicalGradient sheen(c, 45);
    const QColor clear(255, 255, 255, 0);
    const QColor shine(255, 255, 255, 30);
    for (qreal at : { 0.0, 0.12, 0.5, 0.62, 1.0 })
        sheen.setColorAt(at, clear);
    sheen.setColorAt(0.06, shine);
    sheen.setColorAt(0.56, shine);
    p.setPen(Qt::NoPen);
    p.setBrush(sheen);
    p.drawEllipse(c, kDiscR - 1, kDiscR - 1);

    const QRectF art(kArtX, (kH - kArt) / 2.0, kArt, kArt);
    QLinearGradient shadow(art.right(), 0, art.right() + 6, 0);
    shadow.setColorAt(0.0, QColor(0, 0, 0, 90));
    shadow.setColorAt(1.0, QColor(0, 0, 0, 0));
    QPainterPath discPath;
    discPath.addEllipse(c, kDiscR, kDiscR);
    p.save();
    p.setClipPath(discPath);
    p.fillRect(QRectF(art.right(), art.top(), 6, art.height()), shadow);
    p.restore();

    LibraryStyle::paintArt(p, font(), art, m_item, m_covers ? m_covers->get(m_item.imageUrl) : QImage());
}
