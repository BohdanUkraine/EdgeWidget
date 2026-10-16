#include "platform/WindowLayer.h"

#include "app/AppSettings.h"

#include <QWidget>

// X11 (also under Wayland: AppInit forces XWayland, the only way a window can place itself).
// The window manager puts a focused full-screen window above "stay on top" ones, so
// "above windows" needs nothing more; "above everything" is the same here.
void WindowLayer::apply()
{
    if (!m_window->isVisible())
        return;
    const bool onTop = AppSettings::instance().layer() != AppSettings::Layer::BelowWindows;
    const quintptr state = onTop ? 1 : 2;
    if (state == m_applied)
        return;
    m_applied = state;
    if (m_window->windowFlags().testFlag(Qt::WindowStaysOnTopHint) != onTop) {
        m_window->setWindowFlag(Qt::WindowStaysOnTopHint, onTop);
        m_window->show(); // changing a flag hides the window
    }
}
