// Host coverage for where the cover masthead sits, what it leaves the content
// below it, and that it shares the launcher's Bible thumbnail. The blit and the
// plate's pixels need the renderer and are checked on the device.

#include <gtest/gtest.h>

#include "activities/reader/NumberGridLayout.h"
#include "components/CoverBandGeometry.h"
#include "components/MastheadLayout.h"
#include "components/themes/BaseTheme.h"
#include "components/themes/lyra/LyraTheme.h"

namespace {
// X4 Pro portrait panel and its viewable insets: the BoardConfig defaults
// (BoardConfig.h:625-630), which the X4 Pro profile does not override.
constexpr int SCREEN_W = 480;
constexpr int SCREEN_H = 800;
constexpr int INSET_TOP = 9;
constexpr int INSET_RIGHT = 3;
constexpr int INSET_LEFT = 3;
// The launcher's Bible tile under Lyra, as test/cover_band derives it.
constexpr int LAUNCHER_BIBLE_W = 464;
constexpr int LAUNCHER_BIBLE_H = 321;

MastheadLayout::Box bandFor(const ThemeMetrics& metrics) {
  return MastheadLayout::band(SCREEN_W, INSET_TOP, INSET_RIGHT, INSET_LEFT, metrics.topPadding,
                              metrics.mastheadHeight);
}

// BibleNavigationActivity::buildScreen under a masthead: content from
// contentTop to the foot of the safe area (touch zeroes the button hints, so
// that is the panel's foot), less its one verticalSpacing spacer.
int gridBodyHeight(const ThemeMetrics& metrics) {
  return SCREEN_H - MastheadLayout::contentTop(bandFor(metrics)) - metrics.verticalSpacing;
}
}  // namespace

TEST(MastheadMetrics, BothThemesSetTheBandHeight) {
  EXPECT_EQ(LyraMetrics::values.mastheadHeight, 120);
  EXPECT_EQ(BaseMetrics::values.mastheadHeight, 120);
}

TEST(MastheadLayout, BandMirrorsTheLauncherBibleTile) {
  const MastheadLayout::Box band = bandFor(LyraMetrics::values);
  EXPECT_EQ(band.x, INSET_LEFT + LyraMetrics::values.topPadding);
  EXPECT_EQ(band.y, INSET_TOP + LyraMetrics::values.topPadding);
  EXPECT_EQ(band.width, LAUNCHER_BIBLE_W);
  EXPECT_EQ(band.height, 120);
}

TEST(MastheadLayout, NoInsetsWidenTheBandToThePaddedPanel) {
  const MastheadLayout::Box band = MastheadLayout::band(SCREEN_W, 0, 0, 0, 5, 120);
  EXPECT_EQ(band.x, 5);
  EXPECT_EQ(band.y, 5);
  EXPECT_EQ(band.width, 470);
  EXPECT_EQ(band.height, 120);
}

TEST(MastheadLayout, SharesTheLauncherBibleThumbnail) {
  const MastheadLayout::Box band = bandFor(LyraMetrics::values);
  const int bandThumb = CoverBandGeometry::thumbHeightFor(band.width, band.height);
  EXPECT_EQ(bandThumb, CoverBandGeometry::thumbHeightFor(LAUNCHER_BIBLE_W, LAUNCHER_BIBLE_H));
  EXPECT_EQ(bandThumb, 773);
}

TEST(MastheadLayout, PlateIsTheBandFootPlusOneRuleRow) {
  const MastheadLayout::Box band = bandFor(LyraMetrics::values);
  const int headerHeight = LyraMetrics::values.headerHeight;
  const MastheadLayout::Box plate = MastheadLayout::plate(band, headerHeight);
  EXPECT_EQ(plate.x, band.x);
  EXPECT_EQ(plate.width, band.width);
  EXPECT_EQ(plate.height, headerHeight + 1);
  EXPECT_EQ(plate.y, CoverBandGeometry::plateTop(band.y, band.height, headerHeight + 1));
  EXPECT_EQ(plate.y + plate.height, band.y + band.height);

  const MastheadLayout::Box header = MastheadLayout::plateHeader(plate);
  EXPECT_EQ(header.x, plate.x);
  EXPECT_EQ(header.y, plate.y + 1);
  EXPECT_EQ(header.width, plate.width);
  EXPECT_EQ(header.height, headerHeight);
}

TEST(MastheadLayout, ContentStartsAtTheBandFoot) {
  const MastheadLayout::Box band = bandFor(LyraMetrics::values);
  EXPECT_EQ(MastheadLayout::contentTop(band), band.y + band.height);
  EXPECT_EQ(MastheadLayout::contentTop(band), 134);
}

TEST(MastheadLayout, ChapterGridKeepsSevenByTenOnBothThemes) {
  for (const ThemeMetrics* metrics : {&LyraMetrics::values, &BaseMetrics::values}) {
    const int bodyHeight = gridBodyHeight(*metrics);
    const NumberGrid::Geometry grid = NumberGrid::geometryFor(SCREEN_W, bodyHeight);
    EXPECT_EQ(grid.cols, 7) << "body height " << bodyHeight;
    EXPECT_EQ(grid.rows, 10) << "body height " << bodyHeight;
    EXPECT_GE(NumberGrid::cellSizeFor(SCREEN_W, bodyHeight, grid), NumberGrid::MIN_CELL);
  }
}
