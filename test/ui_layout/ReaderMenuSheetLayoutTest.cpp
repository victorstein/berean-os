#include <gtest/gtest.h>

#include "activities/reader/ReaderMenuSheetLayout.h"

namespace {

using ReaderMenuSheetLayout::Box;

// Lyra on the X4 Pro (touch): full-screen safe area, 44 px header band, 8 px
// vertical spacing, 50 px one-line touch rows, 29 px body line, 44 px minimum
// touch target, 2 px popup frame, 32 px tile icons.
ReaderMenuSheetLayout::Inputs x4pro(const int quickCount, const int rowCount) {
  ReaderMenuSheetLayout::Inputs in;
  in.safeX = 0;
  in.safeY = 0;
  in.safeW = 480;
  in.safeH = 800;
  in.titleHeight = 44;
  in.gap = 8;
  in.iconSize = 32;
  in.labelLineHeight = 29;
  in.minTouchSize = 44;
  in.rowHeight = 50;
  in.ruleWidth = 2;
  in.quickCount = quickCount;
  in.rowCount = rowCount;
  return in;
}

bool inside(const Box& inner, const Box& outer) {
  return inner.x >= outer.x && inner.y >= outer.y && inner.right() <= outer.right() && inner.bottom() <= outer.bottom();
}

bool overlaps(const Box& a, const Box& b) {
  return a.x < b.right() && b.x < a.right() && a.y < b.bottom() && b.y < a.bottom();
}

}  // namespace

TEST(ReaderMenuSheetLayout, StackedTileHeight) {
  EXPECT_EQ(ReaderMenuSheetLayout::tileHeightFor(x4pro(4, 11)), 32 + 4 + 29 + 16);
}

// d1: no path scrolls. The worst-case 15-item Bible menu (4 tiles + 11 rows)
// fits over the page with six rows per column.
TEST(ReaderMenuSheetLayout, FifteenItemBibleMenuFitsWithoutScrolling) {
  const auto l = ReaderMenuSheetLayout::compute(x4pro(4, 11));
  EXPECT_TRUE(l.fitsAlone);
  EXPECT_TRUE(l.fitsOverPage);
  EXPECT_EQ(l.rowsPerColumn, 6);
  EXPECT_EQ(l.plate.y, 800 - (2 + 44 + 8 + 81 + 8 + 6 * 50 + 8));
  EXPECT_EQ(l.columns[0].h, 6 * 50);
  EXPECT_EQ(l.columns[1].h, 6 * 50);
}

TEST(ReaderMenuSheetLayout, FourteenItemBookMenuFits) {
  const auto l = ReaderMenuSheetLayout::compute(x4pro(3, 11));
  EXPECT_TRUE(l.fitsOverPage);
  EXPECT_EQ(l.rowsPerColumn, 6);
  EXPECT_EQ(l.tileCount, 3);
}

TEST(ReaderMenuSheetLayout, RotationCapableBuildWithTwelveRowsFits) {
  EXPECT_TRUE(ReaderMenuSheetLayout::compute(x4pro(4, 12)).fitsOverPage);
}

TEST(ReaderMenuSheetLayout, SheetIsBottomAnchoredAndThePageEndsAtItsTop) {
  const auto l = ReaderMenuSheetLayout::compute(x4pro(4, 11));
  EXPECT_EQ(l.plate.bottom(), 800);
  EXPECT_EQ(l.page.y, 0);
  EXPECT_EQ(l.page.bottom(), l.plate.y);
  EXPECT_EQ(l.page.w, 480);
  EXPECT_EQ(l.rule.y, l.plate.y);
  EXPECT_EQ(l.rule.h, 2);
}

TEST(ReaderMenuSheetLayout, TilesAndColumnsStayInsideThePlateWithoutOverlap) {
  for (const int quick : {3, 4}) {
    const auto l = ReaderMenuSheetLayout::compute(x4pro(quick, 11));
    EXPECT_TRUE(inside(l.title, l.plate));
    EXPECT_TRUE(inside(l.close, l.plate));
    EXPECT_FALSE(overlaps(l.title, l.close));
    for (int i = 0; i < l.tileCount; ++i) {
      EXPECT_TRUE(inside(l.tiles[i], l.plate)) << "tile " << i;
      EXPECT_EQ(l.tiles[i].h, l.tileHeight);
      EXPECT_FALSE(overlaps(l.tiles[i], l.columns[0]));
      EXPECT_FALSE(overlaps(l.tiles[i], l.columns[1]));
      EXPECT_GE(l.tiles[i].y, l.title.bottom());
    }
    for (int i = 0; i + 1 < l.tileCount; ++i) {
      EXPECT_EQ(l.tiles[i + 1].x - l.tiles[i].right(), 8) << "even spacing";
      EXPECT_EQ(l.tiles[i + 1].w, l.tiles[i].w) << "equal widths";
    }
    EXPECT_TRUE(inside(l.columns[0], l.plate));
    EXPECT_TRUE(inside(l.columns[1], l.plate));
    EXPECT_FALSE(overlaps(l.columns[0], l.columns[1]));
  }
}

TEST(ReaderMenuSheetLayout, WithoutQuickActionsColumnsFollowTheTitle) {
  const auto l = ReaderMenuSheetLayout::compute(x4pro(0, 4));
  EXPECT_EQ(l.tileCount, 0);
  EXPECT_EQ(l.columns[0].y, l.title.bottom() + 8);
}

TEST(ReaderMenuSheetLayout, OtherScreenSizesLayOutFromTheirOwnSafeArea) {
  auto in = x4pro(4, 11);
  in.safeX = 10;
  in.safeY = 20;
  in.safeW = 580;
  in.safeH = 990;
  const auto l = ReaderMenuSheetLayout::compute(in);
  EXPECT_TRUE(l.fitsOverPage);
  EXPECT_EQ(l.plate.bottom(), 20 + 990);
  EXPECT_EQ(l.page.y, 20);
  EXPECT_EQ(l.page.x, 10);
  EXPECT_EQ(l.page.w, 580);
  EXPECT_EQ(l.columns[1].right(), 10 + 580 - 8);
  EXPECT_EQ(l.tiles[3].right(), 10 + 580 - 8 - (580 - 16 - 3 * 8) % 4);
}

TEST(ReaderMenuSheetLayout, TooManyRowsLoseThePageThenTheScreen) {
  const auto tall = ReaderMenuSheetLayout::compute(x4pro(4, 20));
  EXPECT_TRUE(tall.fitsAlone);
  EXPECT_FALSE(tall.fitsOverPage);
  const auto tooTall = ReaderMenuSheetLayout::compute(x4pro(4, 28));
  EXPECT_FALSE(tooTall.fitsAlone);
  EXPECT_FALSE(tooTall.fitsOverPage);
  EXPECT_EQ(tooTall.plate.y, 0);
}

TEST(ReaderMenuSheetLayout, SnapshotBytesAtX4ProGeometry) {
  const auto l = ReaderMenuSheetLayout::compute(x4pro(4, 11));
  EXPECT_EQ(l.page.h, 349);
  EXPECT_EQ(ReaderMenuSheetLayout::snapshotBytes(l.page.w, l.page.h), 21600u);
}

// readFramebufferRegion copies the panel-oriented bounding box, so the buffer
// must cover the rect in either orientation, plus a byte of alignment slack on
// the packed axis (review 1, MAJOR 1).
TEST(ReaderMenuSheetLayout, SnapshotBytesCoverBothOrientationsForEveryRowCount) {
  for (const int safeW : {480, 580}) {
    for (int rows = 1; rows <= 14; ++rows) {
      auto in = x4pro(4, rows);
      in.safeW = safeW;
      const auto l = ReaderMenuSheetLayout::compute(in);
      const size_t w = static_cast<size_t>(l.page.w);
      const size_t h = static_cast<size_t>(l.page.h);
      const size_t cap = ReaderMenuSheetLayout::snapshotBytes(l.page.w, l.page.h);
      EXPECT_GE(cap, ((h + 7) / 8 + 1) * w) << "rows=" << rows << " w=" << safeW;
      EXPECT_GE(cap, ((w + 7) / 8 + 1) * h) << "rows=" << rows << " w=" << safeW;
    }
  }
}
