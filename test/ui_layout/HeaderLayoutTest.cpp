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
