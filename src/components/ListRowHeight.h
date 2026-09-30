#pragma once

#include <cstdint>

// Row height for a FreeInkUI list. Plain ints and free of FreeInkUI, Arduino
// and GfxRenderer so the host suite can exercise it (test/ui_layout).
namespace ListRowHeight {

struct Inputs {
  bool touch = false;
  bool hasSubtitle = false;
  int tokenRowHeight = 0;    // FreeInkUI theme().rowHeight, sized for label plus subtitle
  int minTouchSize = 0;      // FreeInkUI theme().minTouchSize
  int denseRow = 0;          // ThemeMetrics::listRowHeight
  int denseSubtitleRow = 0;  // ThemeMetrics::listWithSubtitleRowHeight
  int touchSingleRow = 0;    // ThemeMetrics::touchListRowHeight, 0 = use the token
};

constexpr int16_t resolve(const Inputs& in) {
  int height = in.tokenRowHeight;
  if (!in.touch) {
    height = in.hasSubtitle ? in.denseSubtitleRow : in.denseRow;
  } else if (!in.hasSubtitle && in.touchSingleRow > 0) {
    height = in.touchSingleRow > in.minTouchSize ? in.touchSingleRow : in.minTouchSize;
  }
  if (height <= 0) height = in.denseRow;
  return static_cast<int16_t>(height > 0 ? height : 1);
}

}  // namespace ListRowHeight
