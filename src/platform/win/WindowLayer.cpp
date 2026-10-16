#include "platform/WindowLayer.h"

#include "app/AppSettings.h"

#include <QWidget>

#define NOMINMAX
#include <windows.h>

namespace {
constexpr int kPollMs = 500;

// The foreground window if it covers the whole monitor the panel is on (game, video, F11).
HWND fullScreenWindow(HWND panel)
{
    HWND fg = GetForegroundWindow();
    if (!fg || fg == panel)
        return nullptr;
    if (IsZoomed(fg) && (GetWindowLongW(fg, GWL_STYLE) & WS_CAPTION) == WS_CAPTION)
        return nullptr; // an ordinary maximised window (e.g. with an auto-hidden taskbar)
    wchar_t cls[32] = {};
    GetClassNameW(fg, cls, 32);
    if (!wcscmp(cls, L"Progman") || !wcscmp(cls, L"WorkerW") || !wcscmp(cls, L"Shell_TrayWnd"))
        return nullptr; // the desktop and the taskbar
    DWORD pid = 0;
    GetWindowThreadProcessId(fg, &pid);
    if (pid == GetCurrentProcessId())
        return nullptr;
    HMONITOR monitor = MonitorFromWindow(panel, MONITOR_DEFAULTTOPRIMARY);
    if (MonitorFromWindow(fg, MONITOR_DEFAULTTONULL) != monitor)
        return nullptr;
    MONITORINFO mi{ sizeof(mi) };
    RECT r{};
    if (!GetMonitorInfoW(monitor, &mi) || !GetWindowRect(fg, &r))
        return nullptr;
    const RECT &m = mi.rcMonitor;
    return r.left <= m.left && r.top <= m.top && r.right >= m.right && r.bottom >= m.bottom ? fg : nullptr;
}
} // namespace

void WindowLayer::apply()
{
    if (!m_window->isVisible())
        return;
    const auto layer = AppSettings::instance().layer();
    if (layer == AppSettings::Layer::AboveWindows)
        m_poll.start(kPollMs);
    else
        m_poll.stop();

    const HWND self = HWND(m_window->winId());
    const HWND fullScreen = layer == AppSettings::Layer::AboveWindows ? fullScreenWindow(self) : nullptr;
    // 1 topmost, 2 normal, otherwise tucked under that full-screen window
    const quintptr state = fullScreen ? quintptr(fullScreen) : layer == AppSettings::Layer::BelowWindows ? 2 : 1;
    if (state == m_applied)
        return;
    m_applied = state;

    constexpr UINT flags = SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE;
    SetWindowPos(self, state == 1 ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0, flags);
    if (fullScreen)
        SetWindowPos(self, fullScreen, 0, 0, 0, 0, flags); // tuck in right under it
}
