// Host coverage for where BaseTheme::drawToast anchors fui::toast. The draw
// itself needs the renderer and is checked on the device.

#include <gtest/gtest.h>

#include "components/ToastLayout.h"

namespace {
constexpr int SCREEN_W = 480;
constexpr int SCREEN_H = 800;
// X4 Pro portrait viewable insets: BoardConfig.h ViewableInsets defaults.
constexpr int INSET_TOP = 9;
constexpr int INSET_RIGHT = 3;
constexpr int INSET_BOTTOM = 3;
constexpr int INSET_LEFT = 3;
// 19 text lane + (2+1)*2 thick progress bar + 1 margin: UITheme::getStatusBarHeight's maximum.
constexpr int WORST_STATUS_BAR = 26;

ToastLayout::Bounds x4ProBounds(const int statusBarHeight) {
  return ToastLayout::bounds(SCREEN_W, SCREEN_H, INSET_TOP, INSET_RIGHT, INSET_BOTTOM, INSET_LEFT,
                             statusBarHeight);
}
}  // namespace

TEST(ToastLayout, BottomEdgeStopsAboveTheWorstCaseStatusBar) {
  const ToastLayout::Bounds area = x4ProBounds(WORST_STATUS_BAR);
  EXPECT_EQ(area.y + area.height, SCREEN_H - INSET_BOTTOM - WORST_STATUS_BAR);
}

TEST(ToastLayout, BottomEdgeStopsAboveTheBezelWhenTheStatusBarIsHidden) {
  const ToastLayout::Bounds area = x4ProBounds(0);
  EXPECT_EQ(area.y + area.height, SCREEN_H - INSET_BOTTOM);
}

TEST(ToastLayout, SpansTheViewableWidth) {
  const ToastLayout::Bounds area = x4ProBounds(WORST_STATUS_BAR);
  EXPECT_EQ(area.x, INSET_LEFT);
  EXPECT_EQ(area.width, SCREEN_W - INSET_LEFT - INSET_RIGHT);
  EXPECT_EQ(area.y, INSET_TOP);
}

TEST(ToastLayout, AReservationTallerThanTheScreenClampsToEmpty) {
  const ToastLayout::Bounds area = x4ProBounds(SCREEN_H * 2);
  EXPECT_EQ(area.height, 0);
}

static_assert(ToastLayout::bounds(SCREEN_W, SCREEN_H, 0, 0, 0, 0, 0).height == SCREEN_H,
              "bounds must stay usable in a constant expression");

TEST(ToastLayout, PaddingIsHalfTheVerticalMarginAndTheFullSideMarginInsideTheFrame) {
  // Lyra popup metrics: marginX 16, marginY 12, frame 2.
  const ToastLayout::Padding pad = ToastLayout::padding(16, 12, 2);
  EXPECT_EQ(pad.top, 8);
  EXPECT_EQ(pad.bottom, 8);
  EXPECT_EQ(pad.left, 18);
  EXPECT_EQ(pad.right, 18);
}

TEST(ToastLayout, ClassicPaddingRoundsTheHalfMarginDown) {
  // Classic popup metrics: marginX 15, marginY 15, frame 2.
  const ToastLayout::Padding pad = ToastLayout::padding(15, 15, 2);
  EXPECT_EQ(pad.top, 9);
  EXPECT_EQ(pad.left, 17);
}

// The longest posted message (STR_TAG_LIMIT_PER_HIGHLIGHT) measures three
// lines at the toast's width; the fourth is headroom for kerning.
static_assert(ToastLayout::MAX_LINES == 4, "the longest refusal must fit without an ellipsis");
