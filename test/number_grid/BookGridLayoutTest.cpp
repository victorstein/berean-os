#include <gtest/gtest.h>

#include "activities/reader/BookGridLayout.h"

namespace {

constexpr int PORTRAIT_W = 480;
constexpr int PORTRAIT_H = 650;
// "1 Crón." in the 12pt UI body font, rounded up.
constexpr int SPANISH_WIDEST_LABEL = 64;
constexpr int NWT_SECTION_STARTS[] = {0, 39};

}  // namespace

TEST(BookGridColumns, SpanishAbbreviationsGetFiveColumnsInPortrait) {
  EXPECT_EQ(BookGrid::columnsFor(PORTRAIT_W, SPANISH_WIDEST_LABEL), 5);
}

TEST(BookGridColumns, WiderLabelsGetFewerColumnsButNeverBelowTheFloor) {
  EXPECT_LT(BookGrid::columnsFor(PORTRAIT_W, 120), 5);
  EXPECT_EQ(BookGrid::columnsFor(PORTRAIT_W, 1000), BookGrid::MIN_COLS);
}

TEST(BookGridColumns, TinyLabelsAreCappedAtTheCeiling) {
  EXPECT_EQ(BookGrid::columnsFor(PORTRAIT_W, 4), BookGrid::MAX_COLS);
}

TEST(BookGridColumns, ExactFitUsesTheTrailingGap) {
  // stride = 64 + 16 + 8 = 88; (432 + 8) / 88 = 5 exactly. Dropping the "+ gap"
  // numerator would round this down to 4.
  EXPECT_EQ(BookGrid::columnsFor(432, SPANISH_WIDEST_LABEL), 5);
}

TEST(BookGridPages, TheNwtSplitsIntoOnePagePerTestament) {
  const auto layout = BookGrid::layoutFor(66, NWT_SECTION_STARTS, 2, PORTRAIT_W, PORTRAIT_H, SPANISH_WIDEST_LABEL);

  ASSERT_EQ(layout.pageCount, 2);
  EXPECT_EQ(layout.pages[0].first, 0);
  EXPECT_EQ(layout.pages[0].count, 39);
  EXPECT_EQ(layout.pages[0].section, 0);
  EXPECT_EQ(layout.pages[1].first, 39);
  EXPECT_EQ(layout.pages[1].count, 27);
  EXPECT_EQ(layout.pages[1].section, 1);
  EXPECT_EQ(layout.cols, 5);
  EXPECT_EQ(layout.rows, 8);
}

TEST(BookGridPages, EveryPageStaysWithinTheCellCap) {
  const auto layout = BookGrid::layoutFor(66, NWT_SECTION_STARTS, 2, PORTRAIT_W, PORTRAIT_H, SPANISH_WIDEST_LABEL);

  EXPECT_LE(layout.cols * layout.rows, NumberGrid::MAX_CELLS);
  for (int p = 0; p < layout.pageCount; p++) EXPECT_LE(layout.pages[p].count, layout.cols * layout.rows);
}

TEST(BookGridPages, CellCapLimitsRowsWhenManyColumnsFit) {
  // Tiny labels earn all 6 columns, so rowsNeeded (11) and rowsFit (31) both
  // clear MAX_CELLS / cols (8) -- only the cell cap can be the binding limit.
  const auto layout = BookGrid::layoutFor(66, nullptr, 0, PORTRAIT_W, 2000, 4);

  EXPECT_EQ(layout.cols, BookGrid::MAX_COLS);
  EXPECT_EQ(layout.rows, 8);
  EXPECT_LE(layout.cols * layout.rows, NumberGrid::MAX_CELLS);
}

TEST(BookGridPages, NoSectionsPagesContinuously) {
  const auto layout = BookGrid::layoutFor(66, nullptr, 0, PORTRAIT_W, PORTRAIT_H, SPANISH_WIDEST_LABEL);

  ASSERT_GE(layout.pageCount, 2);
  EXPECT_EQ(layout.pages[0].section, -1);
  int covered = 0;
  for (int p = 0; p < layout.pageCount; p++) {
    EXPECT_EQ(layout.pages[p].first, covered);
    covered += layout.pages[p].count;
  }
  EXPECT_EQ(covered, 66);
}

TEST(BookGridPages, ASectionLargerThanAPageSplitsWithinItself) {
  // A short rect forces few rows, so the 39-book section cannot fit one page.
  const auto layout = BookGrid::layoutFor(66, NWT_SECTION_STARTS, 2, PORTRAIT_W, 200, SPANISH_WIDEST_LABEL);

  ASSERT_GT(layout.pageCount, 2);
  EXPECT_EQ(layout.pages[0].section, 0);
  EXPECT_EQ(layout.pages[1].section, 0);
  EXPECT_EQ(layout.pages[layout.pageCount - 1].section, 1);
  int covered = 0;
  for (int p = 0; p < layout.pageCount; p++) covered += layout.pages[p].count;
  EXPECT_EQ(covered, 66);
}

TEST(BookGridPages, InvalidSectionStartsFallBackToContinuous) {
  constexpr int outOfRange[] = {0, 90};
  constexpr int atBookCount[] = {0, 66};
  constexpr int unordered[] = {39, 0};
  constexpr int duplicateStart[] = {0, 0};
  constexpr int notFromZero[] = {5, 39};

  for (const int* starts : {outOfRange, atBookCount, unordered, duplicateStart, notFromZero}) {
    const auto layout = BookGrid::layoutFor(66, starts, 2, PORTRAIT_W, PORTRAIT_H, SPANISH_WIDEST_LABEL);
    EXPECT_EQ(layout.pages[0].section, -1);
  }
}

TEST(BookGridPages, ZeroContentHeightStillYieldsOneRow) {
  const auto layout = BookGrid::layoutFor(66, nullptr, 0, PORTRAIT_W, 0, SPANISH_WIDEST_LABEL);
  EXPECT_EQ(layout.rows, 1);
}

TEST(BookGridPages, OneRowRectReportsTruncation) {
  // rows == 1 caps each page at 5 books; 12 pages cover 60 of the 66, so
  // coveredBooks must fall short and pageCount hits MAX_PAGES.
  const auto layout = BookGrid::layoutFor(66, nullptr, 0, PORTRAIT_W, 60, SPANISH_WIDEST_LABEL);

  ASSERT_EQ(layout.rows, 1);
  EXPECT_EQ(layout.pageCount, BookGrid::MAX_PAGES);
  EXPECT_LT(layout.coveredBooks, 66);
}

TEST(BookGridPages, PageOfFindsTheOwningPage) {
  const auto layout = BookGrid::layoutFor(66, NWT_SECTION_STARTS, 2, PORTRAIT_W, PORTRAIT_H, SPANISH_WIDEST_LABEL);

  EXPECT_EQ(BookGrid::pageOf(layout, 0), 0);
  EXPECT_EQ(BookGrid::pageOf(layout, 38), 0);
  EXPECT_EQ(BookGrid::pageOf(layout, 39), 1);
  EXPECT_EQ(BookGrid::pageOf(layout, 65), 1);
  EXPECT_EQ(BookGrid::pageOf(layout, 200), 1);
  EXPECT_EQ(BookGrid::pageOf(layout, -3), 0);
}

TEST(BookGridPages, LayoutFitsTheStackBudget) {
  static_assert(sizeof(BookGrid::Layout) < 128, "Layout is returned by value on the render stack");
  SUCCEED();
}

TEST(BookGridPages, NoBooksMeansNoPages) {
  const auto layout = BookGrid::layoutFor(0, nullptr, 0, PORTRAIT_W, PORTRAIT_H, SPANISH_WIDEST_LABEL);
  EXPECT_EQ(layout.pageCount, 0);
}

namespace {

// 5 columns: Hebrew page 0..38 (last row 35..38), Greek page 39..65 (last row 64..65).
BookGrid::Layout nwtLayout() {
  return BookGrid::layoutFor(66, NWT_SECTION_STARTS, 2, PORTRAIT_W, PORTRAIT_H, SPANISH_WIDEST_LABEL);
}

constexpr int DOWN = 1;
constexpr int UP = -1;

}  // namespace

TEST(BookGridStepRow, StepsByAColumnCountWithinAPage) {
  const auto layout = nwtLayout();
  ASSERT_EQ(layout.cols, 5);

  EXPECT_EQ(BookGrid::stepRow(layout, 2, DOWN), 7);
  EXPECT_EQ(BookGrid::stepRow(layout, 7, UP), 2);
  EXPECT_EQ(BookGrid::stepRow(layout, 41, DOWN), 46);
}

TEST(BookGridStepRow, DownIntoAShortLastRowClampsToThePagesLastBook) {
  // 34 is column 4 of row 6; row 7 stops at column 3, and stepping by the
  // column count would leave the Hebrew page for Matthew.
  EXPECT_EQ(BookGrid::stepRow(nwtLayout(), 34, DOWN), 38);
}

TEST(BookGridStepRow, DownOffAPageKeepsTheColumnOnTheNextPage) {
  const auto layout = nwtLayout();

  EXPECT_EQ(BookGrid::stepRow(layout, 35, DOWN), 39);
  EXPECT_EQ(BookGrid::stepRow(layout, 37, DOWN), 41);
  EXPECT_EQ(BookGrid::stepRow(layout, 38, DOWN), 42);
}

TEST(BookGridStepRow, UpOffAPageKeepsTheColumnOnThePreviousPagesLastRow) {
  const auto layout = nwtLayout();

  EXPECT_EQ(BookGrid::stepRow(layout, 39, UP), 35);
  EXPECT_EQ(BookGrid::stepRow(layout, 42, UP), 38);
  // Row 7 of the Hebrew page has no column 4.
  EXPECT_EQ(BookGrid::stepRow(layout, 43, UP), 38);
}

TEST(BookGridStepRow, StopsAtEitherEndWithoutWrapping) {
  const auto layout = nwtLayout();

  EXPECT_EQ(BookGrid::stepRow(layout, 3, UP), 0);
  EXPECT_EQ(BookGrid::stepRow(layout, 62, DOWN), 65);
  EXPECT_EQ(BookGrid::stepRow(layout, 64, DOWN), 65);
  EXPECT_EQ(BookGrid::stepRow(layout, 65, DOWN), 65);
}

TEST(BookGridStepRow, NeverLeavesTheCoveredBooks) {
  const auto layout = BookGrid::layoutFor(66, nullptr, 0, PORTRAIT_W, 60, SPANISH_WIDEST_LABEL);
  ASSERT_LT(layout.coveredBooks, 66);

  const int lastCovered = layout.coveredBooks - 1;
  EXPECT_EQ(BookGrid::stepRow(layout, lastCovered, DOWN), lastCovered);
  EXPECT_EQ(BookGrid::stepRow(layout, 65, UP), lastCovered - layout.cols);
}

TEST(BookGridStepRow, AnEmptyLayoutStaysOnTheFirstBook) {
  EXPECT_EQ(BookGrid::stepRow(BookGrid::Layout{}, 5, DOWN), 0);
}
