#pragma once

#include <QImage>

// The app's vinyl record icon at one size. Shared by the tray / window icon and
// tools/make_icon.cpp, which renders resources/EdgeWidget.ico from it.
QImage vinylImage(int size);
