#pragma once

// Runs first in main(), before QApplication exists (environment for Qt's platform plugin).
namespace AppInit {
void beforeQApplication();
} // namespace AppInit
