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

}  // namespace ToastLayout
