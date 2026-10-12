// Host coverage for where BaseTheme::drawHeader and drawHeaderWithAction put
// the title reserves, the battery and the action. The draw itself needs the
// renderer and is checked on the device.

#include <gtest/gtest.h>

#include <algorithm>
#include <vector>

#include "activities/reader/TagChipRow.h"
#include "components/HeaderLayout.h"

namespace {

// FreeInkUI tokens for the 29 px UI_12 body line (themeTokensForLineHeight).
constexpr int SPACE_MD = 8;
constexpr int SPACE_LG = 16;
constexpr int MIN_TOUCH = 44;
// UI_10, the uiScale small font the action is measured in.
constexpr int SMALL_LINE_HEIGHT = 24;

}  // namespace

TEST(HeaderLayout, ActionWidthMatchesTheTagChipMeasure) {
  const int chipHeight = TagChips::chipHeight(SMALL_LINE_HEIGHT, SPACE_MD, MIN_TOUCH);
  for (const int textWidth : {0, 5, 12, 13, 52, 200}) {
    EXPECT_EQ(HeaderLayout::actionWidth(textWidth, SMALL_LINE_HEIGHT, SPACE_LG, SPACE_MD, MIN_TOUCH),
              std::max(textWidth + 2 * SPACE_LG, chipHeight))
        << "textWidth " << textWidth;
  }
}

TEST(HeaderLayout, ActionWidthFloorsAtTheChipHeight) {
  EXPECT_EQ(HeaderLayout::actionWidth(0, SMALL_LINE_HEIGHT, SPACE_LG, SPACE_MD, MIN_TOUCH), 44);
  EXPECT_EQ(HeaderLayout::actionWidth(52, SMALL_LINE_HEIGHT, SPACE_LG, SPACE_MD, MIN_TOUCH), 84);
  // A tall line lifts the floor past minTouchSize: 40 + 2 * 8 = 56.
  EXPECT_EQ(HeaderLayout::actionWidth(10, 40, SPACE_LG, SPACE_MD, MIN_TOUCH), 56);
  EXPECT_EQ(HeaderLayout::actionWidth(10, 40, SPACE_LG, SPACE_MD, MIN_TOUCH),
            std::max(10 + 2 * SPACE_LG, TagChips::chipHeight(40, SPACE_MD, MIN_TOUCH)));
}

namespace {

using HeaderLayout::Box;
using HeaderLayout::Inputs;
using HeaderLayout::Result;

constexpr int SCREEN_W = 480;
// BaseMetrics and LyraMetrics topPadding: every real band starts below it.
constexpr int TOP_PADDING = 5;
constexpr int SIDE_PADDING = 18;
constexpr int TITLE_LINE_HEIGHT = 29;
// Width of "100%" in the header's battery font; the layout takes it as an input.
constexpr int PERCENT_TEXT_W = 43;
constexpr int BATTERY_NUB = 2;
constexpr int PERCENT_SPACING = 4;  // BaseTheme::batteryPercentSpacing

int batteryReserve(const int batteryWidth, const bool showPercent) {
  const int icon = batteryWidth + BATTERY_NUB;
  return showPercent ? icon + PERCENT_SPACING + PERCENT_TEXT_W : icon;
}

// BaseMetrics: a 45 px band, a 20 px battery strip, a centred title, no rule.
Inputs classic(const bool showPercent, const int actionWidth) {
  Inputs in;
  in.band = Box{0, TOP_PADDING, SCREEN_W, 45};
  in.sidePadding = SIDE_PADDING;
  in.underline = 0;
  in.spaceMd = SPACE_MD;
  in.titleLineHeight = TITLE_LINE_HEIGHT;
  in.batteryReserve = batteryReserve(15, showPercent);
  in.batteryBarHeight = 20;
  in.titleCentred = true;
  in.actionWidth = actionWidth;
  return in;
}

// LyraMetrics: a 44 px band with the battery on the title line, a left title, a 3 px rule.
Inputs lyra(const bool showPercent, const int actionWidth) {
  Inputs in;
  in.band = Box{0, TOP_PADDING, SCREEN_W, 44};
  in.sidePadding = SIDE_PADDING;
  in.underline = 3;
  in.spaceMd = SPACE_MD;
  in.titleLineHeight = TITLE_LINE_HEIGHT;
  in.batteryReserve = batteryReserve(16, showPercent);
  in.batteryBarHeight = 44;
  in.titleCentred = false;
  in.actionWidth = actionWidth;
  return in;
}

Inputs withBatteryLeft(Inputs in) {
  in.batteryLeft = true;
  return in;
}

Inputs withBatteryDetached(Inputs in) {
  in.batteryDetached = true;
  return in;
}

// Every battery layout under both themes' metrics, percent on and off.
std::vector<Inputs> allLayouts(const int actionWidth) {
  std::vector<Inputs> out;
  out.reserve(16);
  for (const bool showPercent : {false, true}) {
    for (const Inputs& base : {classic(showPercent, actionWidth), lyra(showPercent, actionWidth)}) {
      out.push_back(base);
      out.push_back(withBatteryLeft(base));
      out.push_back(withBatteryDetached(base));
      out.push_back(withBatteryDetached(withBatteryLeft(base)));
    }
  }
  return out;
}

// What BaseTheme::drawHeader computed inline before HeaderLayout existed
// (the reserve, the detached titleOffsetY and the battery rect).
struct Legacy {
  int leftReserve = 0;
  int rightReserve = 0;
  int titleOffsetY = 0;
  Box battery{0, 0, 0, 0};
};

Legacy legacy(const Inputs& in) {
  Legacy out;
  const Box& band = in.band;
  if (in.batteryDetached) {
    const int titleTop = band.height - in.underline - in.spaceMd - in.titleLineHeight;
    out.titleOffsetY = titleTop - (band.height - in.titleLineHeight) / 2;
  } else if (in.batteryLeft) {
    out.leftReserve = in.batteryReserve + in.spaceMd;
  } else {
    out.rightReserve = in.batteryReserve + in.spaceMd;
  }
  const int inset = in.batteryDetached ? 12 : in.sidePadding;
  const int x = in.batteryLeft ? band.x + inset : band.x + band.width - inset - in.batteryReserve;
  out.battery = Box{x, band.y, in.batteryReserve, in.batteryBarHeight};
  return out;
}

void expectBox(const Box& actual, const Box& expected) {
  EXPECT_EQ(actual.x, expected.x);
  EXPECT_EQ(actual.y, expected.y);
  EXPECT_EQ(actual.width, expected.width);
  EXPECT_EQ(actual.height, expected.height);
}

}  // namespace

TEST(HeaderLayout, WithoutAnActionReproducesDrawHeader) {
  const std::vector<Inputs> layouts = allLayouts(0);
  for (size_t i = 0; i < layouts.size(); ++i) {
    SCOPED_TRACE(i);
    const Result r = HeaderLayout::layout(layouts[i]);
    const Legacy expected = legacy(layouts[i]);
    EXPECT_EQ(r.leftReserve, expected.leftReserve);
    EXPECT_EQ(r.rightReserve, expected.rightReserve);
    EXPECT_EQ(r.titleOffsetY, expected.titleOffsetY);
    expectBox(r.battery, expected.battery);
    expectBox(r.action, Box{0, 0, 0, 0});
  }
}

TEST(HeaderLayout, ClassicAndLyraReservesWithoutAnAction) {
  // Classic: 15 + 2 = 17 px of battery, 17 + 4 + 43 = 64 with the percent.
  Result r = HeaderLayout::layout(classic(false, 0));
  EXPECT_EQ(r.leftReserve, 0);
  EXPECT_EQ(r.rightReserve, 25);
  expectBox(r.battery, Box{445, 5, 17, 20});
  r = HeaderLayout::layout(classic(true, 0));
  EXPECT_EQ(r.rightReserve, 72);
  expectBox(r.battery, Box{398, 5, 64, 20});
  // Lyra: 16 + 2 = 18 px of battery, 18 + 4 + 43 = 65 with the percent.
  r = HeaderLayout::layout(lyra(false, 0));
  EXPECT_EQ(r.rightReserve, 26);
  expectBox(r.battery, Box{444, 5, 18, 44});
  r = HeaderLayout::layout(lyra(true, 0));
  EXPECT_EQ(r.rightReserve, 73);
  expectBox(r.battery, Box{397, 5, 65, 44});
}

namespace {

// "Done" (about 52 px of bold text) and a label narrow enough to hit the floor.
constexpr int DONE_W = 52 + 2 * SPACE_LG;
constexpr int FLOOR_W = MIN_TOUCH;

bool intersects(const Box& a, const Box& b) {
  if (a.width <= 0 || a.height <= 0 || b.width <= 0 || b.height <= 0) return false;
  return a.x < b.x + b.width && b.x < a.x + a.width && a.y < b.y + b.height && b.y < a.y + a.height;
}

// The rect fui::header draws the title into: the band inset by the side
// padding and the reserves, shifted by titleOffsetY.
Box titleBox(const Inputs& in, const Result& r) {
  return Box{in.band.x + in.sidePadding + r.leftReserve, in.band.y + r.titleOffsetY,
             in.band.width - 2 * in.sidePadding - r.leftReserve - r.rightReserve, in.band.height};
}

int right(const Box& box) { return box.x + box.width; }
int bottom(const Box& box) { return box.y + box.height; }

}  // namespace

TEST(HeaderLayout, ActionNeverOverlapsTheBattery) {
  for (const int width : {DONE_W, FLOOR_W}) {
    const std::vector<Inputs> layouts = allLayouts(width);
    for (size_t i = 0; i < layouts.size(); ++i) {
      SCOPED_TRACE(i);
      const Result r = HeaderLayout::layout(layouts[i]);
      EXPECT_FALSE(intersects(r.action, r.battery));
    }
  }
}

TEST(HeaderLayout, ActionNeverOverlapsTheTitle) {
  for (const int width : {DONE_W, FLOOR_W}) {
    const std::vector<Inputs> layouts = allLayouts(width);
    for (size_t i = 0; i < layouts.size(); ++i) {
      SCOPED_TRACE(i);
      const Result r = HeaderLayout::layout(layouts[i]);
      const Box title = titleBox(layouts[i], r);
      EXPECT_FALSE(intersects(title, r.action));
      EXPECT_LE(right(title), r.action.x - SPACE_MD);
    }
  }
}

TEST(HeaderLayout, ActionStaysInsideTheBandAboveTheRule) {
  for (const int width : {DONE_W, FLOOR_W}) {
    const std::vector<Inputs> layouts = allLayouts(width);
    for (size_t i = 0; i < layouts.size(); ++i) {
      SCOPED_TRACE(i);
      const Inputs& in = layouts[i];
      const Result r = HeaderLayout::layout(in);
      EXPECT_GE(r.action.x, in.band.x + in.sidePadding);
      EXPECT_LE(right(r.action), right(in.band) - in.sidePadding);
      EXPECT_GE(r.action.y, in.band.y);
      EXPECT_LE(bottom(r.action), bottom(in.band) - in.underline);
    }
  }
}

TEST(HeaderLayout, LyraPutsTheActionOnTheTitleLineLeftOfTheBattery) {
  for (const bool showPercent : {false, true}) {
    SCOPED_TRACE(showPercent);
    const Inputs in = lyra(showPercent, DONE_W);
    const Result r = HeaderLayout::layout(in);
    EXPECT_EQ(right(r.action), SCREEN_W - SIDE_PADDING - in.batteryReserve - SPACE_MD);
    EXPECT_EQ(r.rightReserve, in.batteryReserve + SPACE_MD + DONE_W + SPACE_MD);
    EXPECT_EQ(r.leftReserve, 0);
    // FreeInkUI's trailing-button box: the band less 4 px top and bottom.
    EXPECT_EQ(r.action.y, TOP_PADDING + 4);
    EXPECT_EQ(r.action.height, 36);
  }
}

TEST(HeaderLayout, ClassicPutsTheActionInTheCornerBelowTheStrip) {
  for (const bool showPercent : {false, true}) {
    SCOPED_TRACE(showPercent);
    const Result r = HeaderLayout::layout(classic(showPercent, DONE_W));
    EXPECT_EQ(right(r.action), SCREEN_W - SIDE_PADDING);
    EXPECT_EQ(r.action.y, TOP_PADDING + 20);
    EXPECT_EQ(bottom(r.action), TOP_PADDING + 45);
    EXPECT_EQ(r.action.height, 25);
  }
}

TEST(HeaderLayout, ClassicWideActionGivesUpItsExcessOverTheBatteryOnBothSides) {
  // Percent on: 84 - 64 = 20 px beyond the battery's 64.
  Result r = HeaderLayout::layout(classic(true, DONE_W));
  EXPECT_EQ(r.rightReserve, 72 + 20);
  EXPECT_EQ(r.leftReserve, 20);
  // Percent off: 84 - 17 = 67 px beyond the battery's 17.
  r = HeaderLayout::layout(classic(false, DONE_W));
  EXPECT_EQ(r.rightReserve, 25 + 67);
  EXPECT_EQ(r.leftReserve, 67);
}

TEST(HeaderLayout, ClassicNarrowActionTakesNoTitleWidthBeyondTheBattery) {
  const Inputs in = classic(true, FLOOR_W);  // 44 <= 64
  const Result r = HeaderLayout::layout(in);
  const Legacy base = legacy(in);
  EXPECT_EQ(r.leftReserve, base.leftReserve);
  EXPECT_EQ(r.rightReserve, base.rightReserve);
  EXPECT_FALSE(intersects(r.action, r.battery));
  EXPECT_LE(right(titleBox(in, r)), r.action.x - SPACE_MD);
}

TEST(HeaderLayout, CentredTitlesGiveUpTheSameWidthOnBothSides) {
  for (const int width : {DONE_W, FLOOR_W}) {
    const std::vector<Inputs> layouts = allLayouts(width);
    for (size_t i = 0; i < layouts.size(); ++i) {
      SCOPED_TRACE(i);
      const Inputs& in = layouts[i];
      const Result r = HeaderLayout::layout(in);
      const Legacy base = legacy(in);
      if (in.titleCentred) {
        EXPECT_EQ(r.leftReserve - base.leftReserve, r.rightReserve - base.rightReserve);
      } else {
        EXPECT_EQ(r.leftReserve, base.leftReserve);
      }
    }
  }
}

TEST(HeaderLayout, BatteryLeftOrDetachedPutsTheActionAtTheTitleLineRightEdge) {
  const std::vector<Inputs> layouts = allLayouts(DONE_W);
  for (size_t i = 0; i < layouts.size(); ++i) {
    const Inputs& in = layouts[i];
    if (!in.batteryLeft && !in.batteryDetached) continue;
    SCOPED_TRACE(i);
    const Result r = HeaderLayout::layout(in);
    EXPECT_EQ(right(r.action), right(in.band) - in.sidePadding);
    EXPECT_EQ(r.rightReserve - legacy(in).rightReserve, DONE_W + SPACE_MD);
  }
}

TEST(HeaderLayout, DetachedActionCentresOnTheTitleLineBelowTheStrip) {
  // Battery left, Lyra metrics: the title line starts at 5 + 44 - 3 - 8 - 29 = 9,
  // and a 36 px action centred on its 29 px starts at 9 + (29 - 36) / 2 = 6.
  Result r = HeaderLayout::layout(withBatteryDetached(withBatteryLeft(lyra(false, DONE_W))));
  EXPECT_EQ(r.action.y, 6);
  EXPECT_EQ(r.action.height, 36);
  // Battery right, Classic metrics: the 20 px strip pushes the action to 25, and
  // the room below it is 25 px.
  r = HeaderLayout::layout(withBatteryDetached(classic(false, DONE_W)));
  EXPECT_EQ(r.action.y, TOP_PADDING + 20);
  EXPECT_EQ(r.action.height, 25);
}

TEST(HeaderLayout, DetachedStripAsTallAsTheBandLeavesNoRoom) {
  const Result r = HeaderLayout::layout(withBatteryDetached(lyra(true, DONE_W)));
  EXPECT_EQ(r.action.height, 0);
  EXPECT_FALSE(intersects(r.action, r.battery));
}

TEST(HeaderLayout, ActionMovesWithTheBatteryPercentOnTheTitleLine) {
  const Result off = HeaderLayout::layout(lyra(false, DONE_W));
  const Result on = HeaderLayout::layout(lyra(true, DONE_W));
  EXPECT_EQ(off.action.x - on.action.x, PERCENT_SPACING + PERCENT_TEXT_W);
}
