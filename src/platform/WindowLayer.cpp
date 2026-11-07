#include "platform/WindowLayer.h"

#include "app/AppSettings.h"

#include <QEvent>
#include <QWidget>

WindowLayer::WindowLayer(QWidget *window)
    : QObject(window)
    , m_window(window)
{
    connect(&AppSettings::instance(), &AppSettings::changed, this, &WindowLayer::apply);
    connect(&m_poll, &QTimer::timeout, this, &WindowLayer::apply);
    window->installEventFilter(this);
}

bool WindowLayer::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_window && event->type() == QEvent::Show) {
        m_applied = 0;
        QTimer::singleShot(0, this, &WindowLayer::apply);
    }
    return QObject::eventFilter(watched, event);
}
