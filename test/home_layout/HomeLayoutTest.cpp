// Host coverage for where Home's sections sit (issue #203, spec A17/A21). The
// drawing itself needs the renderer and is checked on the device.

#include <gtest/gtest.h>

#include "activities/launcher/HomeLayout.h"
#include "components/CoverBandGeometry.h"
#include "components/themes/BaseTheme.h"
#include "components/themes/lyra/LyraTheme.h"

namespace {

// X4 Pro portrait panel and its viewable insets: the BoardConfig defaults
// (BoardConfig.h:625-630), as test/masthead uses.
constexpr int SCREEN_W = 480;
constexpr int SCREEN_H = 800;
constexpr HomeLayout::Insets INSETS{9, 3, 3, 3};
// getLineHeight is the regular face's advanceY: notosans_8 23, ubuntu_10 24,
// notoserif_12 34, notoserif_14 40.
constexpr HomeLayout::LineHeights LINES{23, 24, 34, 40};

HomeLayout::Layout layoutFor(const ThemeMetrics& metrics) {
  return HomeLayout::compute(SCREEN_W, SCREEN_H, INSETS, metrics, LINES);
}

using HomeLayout::bottomOf;

bool within(const HomeLayout::Box& inner, const HomeLayout::Box& outer) {
  return inner.x >= outer.x && inner.y >= outer.y && inner.x + inner.width <= outer.x + outer.width &&
         bottomOf(inner) <= bottomOf(outer);
}

}  // namespace

TEST(HomeLayout, TheHeroSharesTheMastheadThumbnail) {
  const auto layout = layoutFor(LyraMetrics::values);
  EXPECT_EQ(layout.hero.x, 8);
  EXPECT_EQ(layout.hero.y, 14);
  EXPECT_EQ(layout.hero.width, 464);
  EXPECT_EQ(CoverBandGeometry::thumbHeightFor(layout.hero.width, layout.hero.height), 773);
}

TEST(HomeLayout, FixedSectionHeights) {
  EXPECT_EQ(HomeLayout::iconRowHeight(LINES), 79);
  EXPECT_EQ(HomeLayout::meetingsHeight(LINES), 94);
  EXPECT_EQ(HomeLayout::verseCardHeight(LINES), 172);
  EXPECT_EQ(HomeLayout::verseTextHeight(LINES), 102);
  EXPECT_EQ(HomeLayout::recentHeight(LyraMetrics::values, LINES), 143);
  EXPECT_EQ(HomeLayout::recentHeight(BaseMetrics::values, LINES), 113);
  EXPECT_EQ(HomeLayout::plateHeight(LyraMetrics::values, LINES), 93);
  EXPECT_EQ(HomeLayout::plateHeight(BaseMetrics::values, LINES), 94);
}

TEST(HomeLayout, TheHeroTakesTheRemainder) {
  EXPECT_EQ(layoutFor(LyraMetrics::values).hero.height, 258);
  EXPECT_EQ(layoutFor(BaseMetrics::values).hero.height, 280);
}

TEST(HomeLayout, TheHeroKeepsItsArtAboveThePlate) {
  for (const ThemeMetrics* metrics : {&LyraMetrics::values, &BaseMetrics::values}) {
    const auto layout = layoutFor(*metrics);
    EXPECT_GE(layout.hero.height - layout.plate.height, HomeLayout::MIN_HERO_ART);
    EXPECT_EQ(bottomOf(layout.plate), bottomOf(layout.hero));
  }
}

TEST(HomeLayout, TheLastSectionEndsAtTheViewableFoot) {
  for (const ThemeMetrics* metrics : {&LyraMetrics::values, &BaseMetrics::values}) {
    const auto layout = layoutFor(*metrics);
    EXPECT_EQ(bottomOf(layout.icons[0]), SCREEN_H - INSETS.bottom - metrics->topPadding);
  }
}

TEST(HomeLayout, SectionsStackWithoutOverlap) {
  for (const ThemeMetrics* metrics : {&LyraMetrics::values, &BaseMetrics::values}) {
    const auto layout = layoutFor(*metrics);
    const int gap = metrics->verticalSpacing;
    EXPECT_EQ(layout.recentLabel.y, bottomOf(layout.hero) + gap);
    EXPECT_EQ(layout.verseCard.y, bottomOf(layout.recent[HomeLayout::RECENT_SLOTS - 1]) + gap);
    EXPECT_EQ(layout.meetings.y, bottomOf(layout.verseCard) + gap);
    EXPECT_EQ(layout.icons[0].y, bottomOf(layout.meetings) + gap);
  }
}

TEST(HomeLayout, ThePlateHoldsTheHeaderAndTheButtons) {
  const auto layout = layoutFor(LyraMetrics::values);
  EXPECT_EQ(layout.plateHeader.y, layout.plate.y + 1);
  EXPECT_EQ(layout.plateHeader.height, LyraMetrics::values.headerHeight);
  EXPECT_TRUE(within(layout.continueButton, layout.plate));
  EXPECT_TRUE(within(layout.goToButton, layout.plate));
  EXPECT_TRUE(within(layout.buttonRow, layout.plate));
  EXPECT_EQ(layout.continueButton.y, bottomOf(layout.plateHeader));
  EXPECT_LT(layout.continueButton.x + layout.continueButton.width, layout.goToButton.x);
  EXPECT_EQ(bottomOf(layout.buttonRow) + HomeLayout::PAD, bottomOf(layout.plate));
}

TEST(HomeLayout, TheVerseTextBoxHoldsTwoThreeOrFourLines) {
  const auto layout = layoutFor(LyraMetrics::values);
  EXPECT_EQ(layout.verseText.height, 102);
  EXPECT_EQ(layout.verseText.height / LINES.serif14, 2);
  EXPECT_EQ(layout.verseText.height / LINES.serif12, 3);
  EXPECT_EQ(layout.verseText.height / LINES.ui10, 4);
  EXPECT_TRUE(within(layout.verseLabel, layout.verseCard));
  EXPECT_TRUE(within(layout.verseText, layout.verseCard));
  EXPECT_TRUE(within(layout.verseReference, layout.verseCard));
  EXPECT_EQ(bottomOf(layout.verseReference) + HomeLayout::PAD, bottomOf(layout.verseCard));
}

TEST(HomeLayout, TheMeetingsCardKeepsTheRangeBelowTheStrip) {
  const auto layout = layoutFor(LyraMetrics::values);
  EXPECT_EQ(layout.strip.width, 182);
  EXPECT_EQ(layout.meetingsTitle.width, 218);
  EXPECT_EQ(layout.meetingsRange.width, 448);
  EXPECT_EQ(layout.meetingsRange.y, bottomOf(layout.strip));
  EXPECT_EQ(bottomOf(layout.meetingsRange) + HomeLayout::PAD, bottomOf(layout.meetings));
  EXPECT_LE(layout.meetingsTitle.x + layout.meetingsTitle.width + HomeLayout::PAD, layout.strip.x);
  EXPECT_TRUE(within(layout.meetingsIcon, layout.meetings));
  EXPECT_TRUE(within(layout.strip, layout.meetings));
  EXPECT_TRUE(within(layout.meetingsPercent, layout.meetings));
  EXPECT_LE(bottomOf(layout.meetingsPercent), layout.meetingsRange.y);
}

TEST(HomeLayout, FourIconTilesFillTheWidth) {
  const auto layout = layoutFor(LyraMetrics::values);
  EXPECT_EQ(layout.icons[0].x, layout.hero.x);
  const auto& last = layout.icons[HomeLayout::ICON_TILES - 1];
  EXPECT_EQ(last.x + last.width, layout.hero.x + layout.hero.width);
  for (int i = 1; i < HomeLayout::ICON_TILES; ++i) {
    EXPECT_EQ(layout.icons[i].x, layout.icons[i - 1].x + layout.icons[i - 1].width + HomeLayout::PAD);
  }
  EXPECT_EQ(layout.icons[0].width, 110);
}

TEST(HomeLayout, ContainsIsHalfOpen) {
  const HomeLayout::Box box{10, 20, 5, 5};
  EXPECT_TRUE(HomeLayout::contains(box, 10, 20));
  EXPECT_TRUE(HomeLayout::contains(box, 14, 24));
  EXPECT_FALSE(HomeLayout::contains(box, 15, 20));
  EXPECT_FALSE(HomeLayout::contains(box, 10, 25));
}
