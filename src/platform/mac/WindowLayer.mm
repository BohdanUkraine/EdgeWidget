#include "platform/WindowLayer.h"

#include "app/AppSettings.h"

#include <QWidget>

#import <AppKit/AppKit.h>

// On every Space, out of Cmd+` / Mission Control. Full-screen apps live in their own Space,
// so "above windows" is just the floating level; "above everything" also joins those Spaces.
void WindowLayer::apply()
{
    if (!m_window->isVisible())
        return;
    const auto layer = AppSettings::instance().layer();
    const quintptr state = quintptr(layer) + 1;
    if (state == m_applied)
        return;

    NSView *view = reinterpret_cast<NSView *>(m_window->winId());
    NSWindow *window = view.window;
    if (!window)
        return;
    m_applied = state;

    NSWindowCollectionBehavior behavior = NSWindowCollectionBehaviorCanJoinAllSpaces
        | NSWindowCollectionBehaviorStationary | NSWindowCollectionBehaviorIgnoresCycle;
    switch (layer) {
    case AppSettings::Layer::BelowWindows:
        window.level = NSNormalWindowLevel;
        break;
    case AppSettings::Layer::AboveWindows:
        window.level = NSFloatingWindowLevel;
        break;
    case AppSettings::Layer::AboveAll:
        window.level = NSStatusWindowLevel;
        behavior |= NSWindowCollectionBehaviorFullScreenAuxiliary;
        break;
    }
    window.collectionBehavior = behavior;
    window.hidesOnDeactivate = NO;
}
