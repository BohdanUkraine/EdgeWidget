#pragma once

// Panel content layout in logical units (multiplied by AppSettings::scale() on screen).
//
//  +--------------------------------------------------------+------+
//  |  vinyl           | library list (kLibraryTop..+H)       | tabs |
//  |                  | now playing                          |      |
//  |  transport       | time  progress  time                 |      |
//  +--------------------------------------------------------+------+
//  0                kRightX                 right column end   kWidth
namespace PanelLayout {

constexpr int kWidth = 546;
constexpr int kHeight = 200;

constexpr int kTabsW = 32;       // vertical tab strip at the right edge
constexpr int kRightMargin = 12; // gap between the right column and the tab strip
constexpr int kRightX = 186;     // left edge of the right column
constexpr int kRightEnd = kWidth - kTabsW - kRightMargin;

constexpr int kLibraryTop = 10;
constexpr int kLibraryH = 132; // 3 rows of 44

} // namespace PanelLayout
