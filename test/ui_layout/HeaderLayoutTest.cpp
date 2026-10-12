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
