#include <gtest/gtest.h>

#include "components/ListRowHeight.h"

namespace {

// Lyra on the X4 Pro: the 29 px body line height gives FreeInkUI's 66 px
// rowHeight token (lineHeight * 2 + 8) and leaves minTouchSize at its 44 default.
ListRowHeight::Inputs lyra(const bool touch, const bool hasSubtitle) {
  ListRowHeight::Inputs in;
  in.touch = touch;
  in.hasSubtitle = hasSubtitle;
  in.tokenRowHeight = 66;
  in.minTouchSize = 44;
  in.denseRow = 40;
  in.denseSubtitleRow = 60;
  in.touchSingleRow = 50;
  return in;
}

}  // namespace

TEST(ListRowHeight, NonTouchKeepsTheDenseThemeHeights) {
  EXPECT_EQ(ListRowHeight::resolve(lyra(false, false)), 40);
  EXPECT_EQ(ListRowHeight::resolve(lyra(false, true)), 60);
}

TEST(ListRowHeight, TouchSubtitleRowsKeepTheToken) { EXPECT_EQ(ListRowHeight::resolve(lyra(true, true)), 66); }

TEST(ListRowHeight, TouchOneLineRowsUseTheirOwnHeight) { EXPECT_EQ(ListRowHeight::resolve(lyra(true, false)), 50); }

TEST(ListRowHeight, TouchOneLineHeightIsNeverBelowMinTouchSize) {
  auto in = lyra(true, false);
  in.touchSingleRow = 30;
  EXPECT_EQ(ListRowHeight::resolve(in), 44);
}

TEST(ListRowHeight, UnsetTouchOneLineHeightKeepsTheToken) {
  auto in = lyra(true, false);
  in.touchSingleRow = 0;
  EXPECT_EQ(ListRowHeight::resolve(in), 66);
}

TEST(ListRowHeight, NonPositiveChoiceFallsBackToTheDenseRow) {
  auto in = lyra(true, false);
  in.touchSingleRow = 0;
  in.tokenRowHeight = 0;
  EXPECT_EQ(ListRowHeight::resolve(in), 40);
}

TEST(ListRowHeight, NeverBelowOne) {
  ListRowHeight::Inputs in;
  in.touch = true;
  EXPECT_EQ(ListRowHeight::resolve(in), 1);
  in.touch = false;
  EXPECT_EQ(ListRowHeight::resolve(in), 1);
}
