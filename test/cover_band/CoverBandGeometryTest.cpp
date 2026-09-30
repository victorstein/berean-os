// Host coverage for where CoverBand crops a cover and what size of thumbnail it
// asks for. The blit itself needs the renderer and is checked on the device.

#include <gtest/gtest.h>

#include "components/CoverBandGeometry.h"

using CoverBandGeometry::BOOK_TITLE_BAND;
using CoverBandGeometry::crop;
using CoverBandGeometry::MAGAZINE_MASTHEAD_BAND;
using CoverBandGeometry::plateTop;
using CoverBandGeometry::thumbHeightFor;

namespace {
// X4 Pro launcher tiles under the Lyra theme; derivation in
// docs/superpowers/plans/2026-09-30-issue-202-plan.md, "Derived test inputs".
constexpr int BIBLE_TILE_W = 464;
constexpr int BIBLE_TILE_H = 321;
constexpr int BIBLE_PLATE_H = 63;
constexpr int MEETINGS_TILE_W = 227;
constexpr int MEETINGS_TILE_H = 160;
constexpr int MEETINGS_PLATE_H = 40;
// Thumbnails are height-bound, so a cover's height is the requested height;
// the widths are a 3:4 source's at that height.
constexpr int BIBLE_COVER_W = 579;
constexpr int BIBLE_COVER_H = 773;
constexpr int MEETINGS_COVER_W = 283;
constexpr int MEETINGS_COVER_H = 378;
}  // namespace

TEST(CoverBandCrop, CentresHorizontally) {
  EXPECT_EQ(crop(300, 500, 200, 400, 400, MAGAZINE_MASTHEAD_BAND).xOffset, 50);
}

TEST(CoverBandCrop, OddHorizontalSurplusDropsTheExtraColumnOnTheRight) {
  EXPECT_EQ(crop(301, 500, 200, 400, 400, MAGAZINE_MASTHEAD_BAND).xOffset, 50);
}

TEST(CoverBandCrop, MastheadBandStartsAtTheCoversFirstRow) {
  EXPECT_EQ(crop(300, 500, 200, 400, 340, MAGAZINE_MASTHEAD_BAND).yOffset, 0);
}

TEST(CoverBandCrop, TitleBandIsCentredInTheVisibleArtworkNotTheBand) {
  // Focus row 200, visible 300: 200 - 150. Centring in the band (400) would give 0.
  EXPECT_EQ(crop(300, 800, 200, 400, 300, BOOK_TITLE_BAND).yOffset, 50);
}

TEST(CoverBandCrop, ClampsToTheTopRow) { EXPECT_EQ(crop(300, 400, 200, 300, 300, BOOK_TITLE_BAND).yOffset, 0); }

TEST(CoverBandCrop, ClampsSoTheBandNeverRunsOffTheCoversFoot) {
  EXPECT_EQ(crop(300, 500, 200, 400, 100, 1.0f).yOffset, 100);
}

TEST(CoverBandCrop, DeclinesACoverNarrowerThanTheBand) {
  EXPECT_FALSE(crop(199, 500, 200, 400, 400, BOOK_TITLE_BAND).fits);
}

TEST(CoverBandCrop, DeclinesACoverShorterThanTheBand) {
  EXPECT_FALSE(crop(300, 399, 200, 400, 400, BOOK_TITLE_BAND).fits);
}

TEST(CoverBandCrop, ExactSizeFitsWithNoOffset) {
  const auto exact = crop(200, 400, 200, 400, 340, BOOK_TITLE_BAND);
  EXPECT_TRUE(exact.fits);
  EXPECT_EQ(exact.xOffset, 0);
  EXPECT_EQ(exact.yOffset, 0);
}

TEST(CoverBandThumb, WideBandIsBoundByItsWidth) { EXPECT_EQ(thumbHeightFor(BIBLE_TILE_W, BIBLE_TILE_H), 773); }

TEST(CoverBandThumb, TallBandIsBoundByItsHeight) { EXPECT_EQ(thumbHeightFor(100, 400), 400); }

TEST(CoverBandThumb, TruncatesInSinglePrecision) {
  // 0.6f is a little above 0.6, so 240 / 0.6f is just under 400. The cached
  // thumbnail is named by this height: "fixing" it to 400 regenerates covers.
  EXPECT_EQ(thumbHeightFor(240, 100), 399);
}

TEST(CoverBandPlate, SitsAtTheBandsFoot) { EXPECT_EQ(plateTop(59, BIBLE_TILE_H, BIBLE_PLATE_H), 317); }

TEST(CoverBandPlate, ZeroHeightPlateSitsOnTheBottomEdge) { EXPECT_EQ(plateTop(59, BIBLE_TILE_H, 0), 380); }

TEST(CoverBandLauncher, BibleTileRequestsAndCropsAsBefore) {
  EXPECT_EQ(thumbHeightFor(BIBLE_TILE_W, BIBLE_TILE_H), BIBLE_COVER_H);
  const auto bible =
      crop(BIBLE_COVER_W, BIBLE_COVER_H, BIBLE_TILE_W, BIBLE_TILE_H, BIBLE_TILE_H - BIBLE_PLATE_H, BOOK_TITLE_BAND);
  EXPECT_TRUE(bible.fits);
  EXPECT_EQ(bible.xOffset, 57);
  EXPECT_EQ(bible.yOffset, 64);
}

TEST(CoverBandLauncher, MeetingsTileRequestsAndCropsAsBefore) {
  EXPECT_EQ(thumbHeightFor(MEETINGS_TILE_W, MEETINGS_TILE_H), MEETINGS_COVER_H);
  const auto meetings = crop(MEETINGS_COVER_W, MEETINGS_COVER_H, MEETINGS_TILE_W, MEETINGS_TILE_H,
                             MEETINGS_TILE_H - MEETINGS_PLATE_H, MAGAZINE_MASTHEAD_BAND);
  EXPECT_TRUE(meetings.fits);
  EXPECT_EQ(meetings.xOffset, 28);
  EXPECT_EQ(meetings.yOffset, 0);
}
