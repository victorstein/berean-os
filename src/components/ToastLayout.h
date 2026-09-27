#pragma once

#include <cstdint>

// Geometry for BaseTheme::drawToast, free of the renderer and FreeInkUI so it
// can be host-tested (test/posted_message/ToastLayoutTest.cpp).
namespace ToastLayout {

struct Bounds {
  int x;
  int y;
  int width;
  int height;
};

// The area fui::toast anchors its bottom panel inside. The bottom edge stops
// above the bezel and the reader status bar. drawToast cannot tell whether the
// screen under it has a status bar, so it always reserves one; on list screens
// that only lifts the toast a little.
constexpr Bounds bounds(const int screenWidth, const int screenHeight, const int insetTop, const int insetRight,
                        const int insetBottom, const int insetLeft, const int statusBarHeight) {
  const int width = screenWidth - insetLeft - insetRight;
  const int height = screenHeight - insetTop - insetBottom - statusBarHeight;
  return Bounds{insetLeft, insetTop, width < 0 ? 0 : width, height < 0 ? 0 : height};
}

struct Padding {
  int top;
  int right;
  int bottom;
  int left;
};

// fui::popup strokes its frame inside the panel, where drawPopup drew it
// outside, so the frame is added to keep text off the rim. Half the vertical
// margin keeps the toast compact.
constexpr Padding padding(const int popupMarginX, const int popupMarginY, const int frameThickness) {
  const int vertical = popupMarginY / 2 + frameThickness;
  const int horizontal = popupMarginX + frameThickness;
  return Padding{vertical, horizontal, vertical, horizontal};
}

constexpr uint8_t MAX_LINES = 4;

}  // namespace ToastLayout
