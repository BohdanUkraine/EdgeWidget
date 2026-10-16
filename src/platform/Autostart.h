#pragma once

// "Start with the system", no admin rights needed.
// Windows: a value in HKCU\...\CurrentVersion\Run. macOS: a LaunchAgent in ~/Library/LaunchAgents.
namespace Autostart {
bool isEnabled();
void setEnabled(bool enabled);
void refresh(); // if enabled, point the entry at this exe (it may have been moved)
} // namespace Autostart
