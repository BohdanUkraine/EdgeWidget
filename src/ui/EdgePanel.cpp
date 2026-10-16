#include "ui/EdgePanel.h"

#include "app/AppSettings.h"
#include "platform/WindowLayer.h"
#include "playback/MediaController.h"
#include "ui/PanelContent.h"
#include "ui/PanelLayout.h"

#include <QApplication>
#include <QAbstractSpinBox>
#include <QKeyEvent>
#include <QLineEdit>
#include <QPainter>
#include <QPainterPath>
#include <QScreen>
#include <QTimer>

#include <algorithm>
#include <cmath>

namespace {
constexpr int kRadius = 16;
constexpr int kBorder = 2;
constexpr int kScreenMargin = 12; // keeps the panel off the very top / bottom edge
constexpr int kDragThreshold = 6;
constexpr int kAnimMs = 260;
} // namespace

EdgePanel::EdgePanel(MediaController *media, SpotifyClient *spotify, DeviceController *device, QWidget *parent)
    : QWidget(parent, Qt::Tool | Qt::FramelessWindowHint) // topmost or not: WindowLayer
    , m_media(media)
    , m_anim(this, "reveal")
{
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_MacAlwaysShowToolWindow); // macOS hides tool windows of inactive apps
    setFocusPolicy(Qt::StrongFocus);
    setWindowTitle(QStringLiteral("Edge Widget"));

    m_content = new PanelContent(media, spotify, device, this);
    m_content->hide();
    // App-wide, so a focused button or check box in the settings doesn't take the Space.
    qApp->installEventFilter(this);

    m_anim.setDuration(kAnimMs);
    m_anim.setEasingCurve(QEasingCurve::OutCubic);

    if (QScreen *screen = QGuiApplication::primaryScreen())
        connect(screen, &QScreen::availableGeometryChanged, this, &EdgePanel::applyGeometry);
    connect(&AppSettings::instance(), &AppSettings::changed, this, &EdgePanel::applyGeometry);
    new WindowLayer(this);

    applyGeometry();
}

// ---------------------------------------------------------------- geometry

QRect EdgePanel::availableArea() const
{
    QScreen *screen = QGuiApplication::primaryScreen();
    return screen ? screen->availableGeometry() : QRect(0, 0, 1280, 720);
}

bool EdgePanel::rightSide() const
{
    return AppSettings::instance().rightSide();
}

QSize EdgePanel::panelSize() const
{
    const qreal s = uiScale();
    return QSize(int(std::lround(PanelLayout::kWidth * s)), int(std::lround(PanelLayout::kHeight * s)));
}

int EdgePanel::handleHeight() const
{
    return std::clamp(AppSettings::instance().handleHeight(), 40, panelSize().height());
}

QRect EdgePanel::handleRect() const
{
    const int w = AppSettings::instance().handleWidth();
    const int x = rightSide() ? 0 : width() - w;
    return QRect(x, 0, w, height());
}

// Panel centre for a position between 0 (top of the screen) and 1 (bottom).
qreal EdgePanel::centerY(qreal position) const
{
    const QRect area = availableArea();
    const qreal half = panelSize().height() / 2.0;
    const qreal top = area.top() + kScreenMargin + half;
    const qreal bottom = area.bottom() + 1 - kScreenMargin - half;
    return top + (std::max(top, bottom) - top) * std::clamp(position, 0.0, 1.0);
}

qreal EdgePanel::positionForCenter(qreal center) const
{
    const qreal top = centerY(0.0);
    const qreal bottom = centerY(1.0);
    return bottom > top ? std::clamp((center - top) / (bottom - top), 0.0, 1.0) : 0.0;
}

int EdgePanel::inwardDx(int dx) const
{
    return rightSide() ? -dx : dx;
}

void EdgePanel::setReveal(qreal value)
{
    m_reveal = std::clamp(value, 0.0, 1.0);
    applyGeometry();
}

// The window stays anchored to the screen edge, so nothing ever leaks onto a neighbouring
// monitor. Collapsed it is just the handle; while sliding out it grows to the panel size
// around the same vertical centre.
void EdgePanel::applyGeometry()
{
    const QRect area = availableArea();
    const QSize panel = panelSize();
    const int handleW = AppSettings::instance().handleWidth();
    const qreal position = m_dragPosition.value_or(AppSettings::instance().position());

    const int visible = int(std::lround(m_reveal * panel.width()));
    const int w = handleW + visible;
    const int h = int(std::lround(handleHeight() + (panel.height() - handleHeight()) * m_reveal));
    const int x = rightSide() ? area.right() + 1 - w : area.left();
    const int y = int(std::lround(centerY(position) - h / 2.0));
    setGeometry(x, y, w, h);

    // Content is glued to the handle and gets clipped by the window (= screen) edge.
    m_content->setFixedSize(panel);
    m_content->move(rightSide() ? handleW : visible - panel.width(), (h - panel.height()) / 2);
    m_content->setVisible(visible > 0);
    m_content->updatePopupAnchor(); // the track list card rides along
    update();
}

// ---------------------------------------------------------------- open / close

void EdgePanel::animateTo(bool expanded)
{
    m_expanded = expanded;
    m_anim.stop();
    m_anim.setStartValue(m_reveal);
    m_anim.setEndValue(expanded ? 1.0 : 0.0);
    m_anim.setDuration(int(kAnimMs * std::abs((expanded ? 1.0 : 0.0) - m_reveal)) + 60);
    m_anim.start();
    if (expanded) {
        raise();
        activateWindow();
        setFocus();
    }
}

void EdgePanel::expand() { animateTo(true); }

void EdgePanel::collapse()
{
    m_content->closeTrackList();
    m_content->closeSettings();
    animateTo(false);
}

void EdgePanel::toggle()
{
    if (m_expanded)
        collapse();
    else
        expand();
}

// ---------------------------------------------------------------- painting

void EdgePanel::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const Theme theme = Theme::current();
    const bool right = rightSide();
    const QRect hr = handleRect();
    const qreal half = kBorder / 2.0;
    const qreal radius = std::min<qreal>(kRadius, height() / 2.0);
    const QPen pen(theme.accent, kBorder, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);

    // One rounded rectangle; its outer side runs past the window (= screen) edge.
    const QRectF frame = right
        ? QRectF(half, half, width() + radius, height() - kBorder)
        : QRectF(-radius, half, width() + radius - half, height() - kBorder);
    p.setPen(pen);
    p.setBrush(theme.bg);
    p.drawRoundedRect(frame, radius, radius);

    // Divider between the handle and the panel body.
    if (m_reveal > 0.0) {
        const qreal dx = right ? hr.right() + 1 - half : hr.left() + half;
        p.drawLine(QPointF(dx, half), QPointF(dx, height() - half));
    }

    // Minimal chevron, sized with the handle. It flips horizontally while the panel
    // slides: points "out" of the edge when collapsed, back "in" when expanded.
    QPainterPath arrow;
    arrow.moveTo(-3, -7);
    arrow.lineTo(3, 0);
    arrow.lineTo(-3, 7);

    const qreal size = std::clamp(hr.width() / 24.0, 0.6, 1.8);
    const qreal dir = right ? -1.0 + 2.0 * m_reveal : 1.0 - 2.0 * m_reveal;
    p.save();
    p.translate(QRectF(hr).center());
    p.scale(size * (std::abs(dir) < 0.05 ? (dir < 0 ? -0.05 : 0.05) : dir), size);
    QPen arrowPen = pen;
    arrowPen.setCosmetic(true); // keep the stroke width constant while flipping
    p.setPen(arrowPen);
    p.setBrush(Qt::NoBrush);
    p.drawPath(arrow);
    p.restore();
}

// ---------------------------------------------------------------- input

void EdgePanel::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton || !handleRect().contains(event->position().toPoint())) {
        QWidget::mousePressEvent(event);
        return;
    }
    m_anim.stop();
    m_drag = Drag::Pending;
    m_pressGlobal = event->globalPosition().toPoint();
    m_pressCenter = centerY(AppSettings::instance().position());
    m_pressReveal = m_reveal;
}

void EdgePanel::mouseMoveEvent(QMouseEvent *event)
{
    if (m_drag == Drag::None)
        return;

    const QPoint d = event->globalPosition().toPoint() - m_pressGlobal;
    if (m_drag == Drag::Pending) {
        if (std::max(std::abs(d.x()), std::abs(d.y())) < kDragThreshold)
            return;
        m_drag = std::abs(d.x()) >= std::abs(d.y()) ? Drag::Horizontal : Drag::Vertical;
    }

    if (m_drag == Drag::Horizontal) {
        // Swipe: the panel follows the cursor.
        setReveal(m_pressReveal + qreal(inwardDx(d.x())) / panelSize().width());
    } else {
        // Vertical drag moves the panel along the edge (saved on release).
        m_dragPosition = positionForCenter(m_pressCenter + d.y());
        applyGeometry();
    }
}

void EdgePanel::mouseReleaseEvent(QMouseEvent *event)
{
    const Drag drag = m_drag;
    m_drag = Drag::None;

    switch (drag) {
    case Drag::Pending:
        toggle();
        break;
    case Drag::Horizontal: {
        const int dx = inwardDx((event->globalPosition().toPoint() - m_pressGlobal).x());
        // Snap by direction of the swipe, with a small dead zone falling back to position.
        if (dx > 20)
            expand();
        else if (dx < -20)
            collapse();
        else
            animateTo(m_reveal > 0.5);
        break;
    }
    case Drag::Vertical:
        if (m_dragPosition)
            AppSettings::instance().setPosition(*m_dragPosition);
        m_dragPosition.reset();
        break;
    case Drag::None:
        QWidget::mouseReleaseEvent(event);
        break;
    }
}

void EdgePanel::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape && m_expanded) {
        collapse();
        return;
    }
    QWidget::keyPressEvent(event);
}

bool EdgePanel::ownsWindow(QWidget *window) const
{
    for (QWidget *w = window; w; w = w->parentWidget()) {
        if (w == this)
            return true;
    }
    return false;
}

bool EdgePanel::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::KeyPress && m_expanded) {
        auto *key = static_cast<QKeyEvent *>(event);
        auto *widget = qobject_cast<QWidget *>(watched);
        // Typing (a text field) and open combo box lists keep their Space.
        if (key->key() == Qt::Key_Space && key->modifiers() == Qt::NoModifier && widget
            && widget->window() == this && !qobject_cast<QLineEdit *>(widget)
            && !qobject_cast<QAbstractSpinBox *>(widget)) {
            if (!key->isAutoRepeat())
                m_media->togglePlayPause();
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

bool EdgePanel::event(QEvent *event)
{
    // Clicking anywhere outside the panel hides it, like on Android. Our own dialogs
    // (e.g. the colour picker from the settings) do not count as "outside".
    if (event->type() == QEvent::WindowDeactivate && m_expanded && m_drag == Drag::None) {
        QTimer::singleShot(0, this, [this] {
            QWidget *active = QApplication::activeWindow();
            if (m_expanded && !isActiveWindow() && !(active && ownsWindow(active)))
                collapse();
        });
    }
    return QWidget::event(event);
}
