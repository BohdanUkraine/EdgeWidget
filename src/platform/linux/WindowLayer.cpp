#include "platform/WindowLayer.h"

#include "app/AppSettings.h"

#include <QWidget>

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
        m_window->show();
    }
}
