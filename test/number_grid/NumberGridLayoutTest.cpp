#include <gtest/gtest.h>

#include "activities/reader/NumberGridLayout.h"

// Reaching Psalm 119:145 has to cost pages, not screens, and every cell on
// every page has to be tappable and render its own selection. Both properties
// are arithmetic: how many cells a rect earns (over the cap, cells would be
// silently dropped from the interaction table) and how absolute indexes map to
// the page-relative index keyGrid compares against.

namespace {

// The content rects the reader leaves for the grid. 743 is the Lyra chapter
// body on a touch board (800 less topPadding 5, headerHeight 44 and the 8 px
// spacer; UITheme zeroes buttonHintsHeight on touch); 695 was the body before
// the compact metrics. Computed, not measured on the device.
constexpr int PORTRAIT_W = 480;
constexpr int PORTRAIT_H = 743;
constexpr int PRE_COMPACT_H = 695;
constexpr int LANDSCAPE_W = 800;
constexpr int LANDSCAPE_H = 375;

constexpr int PAGE = 70;
constexpr int PSALM_119_VERSES = 176;

}  // namespace

TEST(NumberGridGeometry, PortraitBodyIsSevenByTen) {
  const auto geometry = NumberGrid::geometryFor(PORTRAIT_W, PORTRAIT_H);

  EXPECT_EQ(geometry.cols, 7);
  EXPECT_EQ(geometry.rows, 10);
  EXPECT_EQ(geometry.cellsPerPage(), PAGE);
  EXPECT_LE(geometry.cellsPerPage(), NumberGrid::MAX_CELLS);
}

TEST(NumberGridGeometry, PreCompactBodyIsSevenByTenWithoutTheClamp) {
  const int unclampedRows = (PRE_COMPACT_H + NumberGrid::GAP) / (NumberGrid::MIN_CELL + NumberGrid::GAP);
  const auto geometry = NumberGrid::geometryFor(PORTRAIT_W, PRE_COMPACT_H);

  EXPECT_EQ(unclampedRows, 10);
  EXPECT_EQ(geometry.cols, 7);
  EXPECT_EQ(geometry.rows, 10);
}

TEST(NumberGridGeometry, LandscapeRectStaysWithinTheCellCap) {
  const auto geometry = NumberGrid::geometryFor(LANDSCAPE_W, LANDSCAPE_H);

  EXPECT_EQ(geometry.cols, NumberGrid::MAX_COLS);
  EXPECT_LE(geometry.cellsPerPage(), NumberGrid::MAX_CELLS);
}

TEST(NumberGridGeometry, RowClampFiresInsteadOfOverflowing) {
  // The rows the height alone earns, before the clamp.
  const int unclampedRows = (PORTRAIT_H + NumberGrid::GAP) / (NumberGrid::MIN_CELL + NumberGrid::GAP);
  const auto geometry = NumberGrid::geometryFor(PORTRAIT_W, PORTRAIT_H);

  EXPECT_EQ(unclampedRows, 11);
  EXPECT_GT(unclampedRows * geometry.cols, NumberGrid::MAX_CELLS);
  EXPECT_LT(geometry.rows, unclampedRows);
  EXPECT_EQ(geometry.rows, NumberGrid::MAX_CELLS / geometry.cols);
}

TEST(NumberGridGeometry, TallPortraitBodiesStayAtTenRowsAboveTheTapFloor) {
  for (int height = 632; height <= 2000; height++) {
    const auto geometry = NumberGrid::geometryFor(PORTRAIT_W, height);
    ASSERT_EQ(geometry.cols, 7) << height;
    ASSERT_EQ(geometry.rows, 10) << height;
    EXPECT_GE(NumberGrid::cellSizeFor(PORTRAIT_W, height, geometry), NumberGrid::MIN_CELL) << height;
  }
}

TEST(NumberGridGeometry, NarrowRectKeepsTheMinimumColumns) {
  const auto geometry = NumberGrid::geometryFor(120, 200);

  EXPECT_EQ(geometry.cols, NumberGrid::MIN_COLS);
  EXPECT_GE(geometry.rows, 1);
  EXPECT_LE(geometry.cellsPerPage(), NumberGrid::MAX_CELLS);
}

TEST(NumberGridGeometry, DegenerateRectStillYieldsOneRow) {
  const auto geometry = NumberGrid::geometryFor(0, 0);

  EXPECT_EQ(geometry.cols, NumberGrid::MIN_COLS);
  EXPECT_EQ(geometry.rows, 1);
  EXPECT_TRUE(geometry.valid());
}

TEST(NumberGridGeometry, EveryOrientationHonoursTheCellCap) {
  for (int width = 8; width <= 800; width += 8) {
    for (int height = 8; height <= 800; height += 8) {
      const auto geometry = NumberGrid::geometryFor(width, height);
      EXPECT_LE(geometry.cellsPerPage(), NumberGrid::MAX_CELLS) << width << "x" << height;
      EXPECT_GE(geometry.cols, NumberGrid::MIN_COLS) << width << "x" << height;
      EXPECT_LE(geometry.cols, NumberGrid::MAX_COLS) << width << "x" << height;
      EXPECT_GE(geometry.rows, 1) << width << "x" << height;
    }
  }
}

TEST(NumberGridGeometry, CellSizeBoundsTheTouchTargetFloor) {
  const auto geometry = NumberGrid::geometryFor(PORTRAIT_W, PORTRAIT_H);
  const int cell = NumberGrid::cellSizeFor(PORTRAIT_W, PORTRAIT_H, geometry);

  // A minTouchSize above this would expand hit rects past their cell and
  // adjacent numbers would swallow each other's taps.
  const int cellW = (PORTRAIT_W - (geometry.cols - 1) * NumberGrid::GAP) / geometry.cols;
  const int cellH = (PORTRAIT_H - (geometry.rows - 1) * NumberGrid::GAP) / geometry.rows;
  EXPECT_EQ(cell, std::min(cellW, cellH));
  EXPECT_EQ(cell, 61);
  EXPECT_GE(cell, NumberGrid::MIN_CELL);
}

TEST(NumberGridPaging, ChapterListsFitTheirPages) {
  EXPECT_EQ(NumberGrid::pageCount(50, PAGE), 1);   // Genesis
  EXPECT_EQ(NumberGrid::pageCount(66, PAGE), 1);   // Isaiah
  EXPECT_EQ(NumberGrid::pageCount(150, PAGE), 3);  // Psalms
  EXPECT_EQ(NumberGrid::cellsOnPage(150, NumberGrid::pageFirstCell(2, PAGE), PAGE), 10);
}

TEST(NumberGridPaging, Psalm119TakesThreePages) { EXPECT_EQ(NumberGrid::pageCount(PSALM_119_VERSES, PAGE), 3); }

TEST(NumberGridPaging, LastPageIsPartlyPadded) {
  const int lastPageFirst = NumberGrid::pageFirstCell(2, PAGE);

  EXPECT_EQ(lastPageFirst, 140);
  EXPECT_EQ(NumberGrid::cellsOnPage(PSALM_119_VERSES, lastPageFirst, PAGE), 36);
  EXPECT_EQ(PAGE - NumberGrid::cellsOnPage(PSALM_119_VERSES, lastPageFirst, PAGE), 34);
}

TEST(NumberGridPaging, ExactlyOnePageHasNoEmptyTrailingPage) {
  EXPECT_EQ(NumberGrid::pageCount(PAGE, PAGE), 1);
  EXPECT_EQ(NumberGrid::cellsOnPage(PAGE, 0, PAGE), PAGE);
  EXPECT_EQ(NumberGrid::pageCount(PAGE + 1, PAGE), 2);
}

TEST(NumberGridPaging, HandlesOneAndZeroCounts) {
  EXPECT_EQ(NumberGrid::pageCount(1, PAGE), 1);
  EXPECT_EQ(NumberGrid::cellsOnPage(1, 0, PAGE), 1);
  EXPECT_EQ(NumberGrid::pageCount(0, PAGE), 0);
  EXPECT_EQ(NumberGrid::cellsOnPage(0, 0, PAGE), 0);
  EXPECT_EQ(NumberGrid::pageStartFor(0, 0, PAGE), 0);
}

TEST(NumberGridPaging, IndexRoundTripsOnEveryPage) {
  for (int index = 0; index < PSALM_119_VERSES; index++) {
    const int page = NumberGrid::pageOfIndex(index, PAGE);
    const int pageFirst = NumberGrid::pageFirstCell(page, PAGE);
    const int cell = NumberGrid::pageRelativeIndex(index, pageFirst, PAGE);

    ASSERT_GE(cell, 0) << index;
    ASSERT_LT(cell, PAGE) << index;
    EXPECT_EQ(pageFirst + cell, index);
    EXPECT_EQ(NumberGrid::pageStartFor(index, PSALM_119_VERSES, PAGE), pageFirst);
  }
}

TEST(NumberGridPaging, PageStartClampsAPageThatNoLongerExists) {
  // A geometry change shrinks the page; a selection kept from the larger
  // geometry must land on a page the new count still has.
  EXPECT_EQ(NumberGrid::pageStartFor(140, PSALM_119_VERSES, PAGE), 140);
  EXPECT_EQ(NumberGrid::pageStartFor(140, 40, PAGE), 0);
  EXPECT_EQ(NumberGrid::pageStartFor(-5, PSALM_119_VERSES, PAGE), 0);
}

TEST(NumberGridSelection, SelectedIndexIsPageRelative) {
  EXPECT_EQ(NumberGrid::pageRelativeIndex(140, NumberGrid::pageFirstCell(2, PAGE), PAGE), 0);
  EXPECT_EQ(NumberGrid::pageRelativeIndex(175, NumberGrid::pageFirstCell(2, PAGE), PAGE), 35);
  EXPECT_EQ(NumberGrid::pageRelativeIndex(0, 0, PAGE), 0);
}

TEST(NumberGridSelection, SelectionOffThePageRendersAsNoSelection) {
  const int pageFirst = NumberGrid::pageFirstCell(2, PAGE);

  EXPECT_EQ(NumberGrid::pageRelativeIndex(139, pageFirst, PAGE), -1);
  EXPECT_EQ(NumberGrid::pageRelativeIndex(210, pageFirst, PAGE), -1);
  EXPECT_EQ(NumberGrid::pageRelativeIndex(0, pageFirst, PAGE), -1);
}
