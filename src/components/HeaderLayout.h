#pragma once

#include <algorithm>

// Geometry for BaseTheme::drawHeader and drawHeaderWithAction, free of the
// renderer and FreeInkUI so it can be host-tested
// (test/ui_layout/HeaderLayoutTest.cpp). All coordinates are absolute.
namespace HeaderLayout {

// TagChipView::measure's width, reproduced because the theme cannot include
// reader code: the bold label plus spaceLg each side, never narrower than a
// chip is tall (TagChips::chipHeight).
constexpr int actionWidth(const int boldTextWidth, const int textLineHeight, const int spaceLg, const int spaceMd,
                          const int minTouchSize) {
  const int chipHeight = std::max(textLineHeight + 2 * spaceMd, minTouchSize);
  return std::max(boldTextWidth + 2 * spaceLg, chipHeight);
}

}  // namespace HeaderLayout
