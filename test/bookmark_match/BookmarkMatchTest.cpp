#include <gtest/gtest.h>

#include "util/BookmarkMatch.h"

namespace {

BookmarkEntry bookmarkAt(const float percentage, const uint16_t spine = 0, const uint16_t pageCount = 0,
                         const uint16_t page = 0) {
  BookmarkEntry entry;
  entry.percentage = percentage;
  entry.computedSpineIndex = spine;
  entry.computedChapterPageCount = pageCount;
  entry.computedChapterProgress = page;
  return entry;
}

constexpr int SPINE = 3;
constexpr int PAGE = 7;
constexpr int PAGE_COUNT = 20;
constexpr ProgressRange RANGE{0.40f, 0.50f};

}  // namespace

TEST(BookmarkMatch, ExactComputedPositionMatchesEvenOutsideTheRange) {
  EXPECT_TRUE(bookmarkMatchesProgress(bookmarkAt(0.90f, SPINE, PAGE_COUNT, PAGE), SPINE, PAGE, PAGE_COUNT, RANGE));
}

TEST(BookmarkMatch, PercentageInsideTheRangeMatches) {
  EXPECT_TRUE(bookmarkMatchesProgress(bookmarkAt(0.45f), SPINE, PAGE, PAGE_COUNT, RANGE));
}

TEST(BookmarkMatch, PercentageOutsideTheRangeDoesNotMatch) {
  EXPECT_FALSE(bookmarkMatchesProgress(bookmarkAt(0.30f), SPINE, PAGE, PAGE_COUNT, RANGE));
  EXPECT_FALSE(bookmarkMatchesProgress(bookmarkAt(0.60f), SPINE, PAGE, PAGE_COUNT, RANGE));
}

TEST(BookmarkMatch, EpsilonWidensBothEdges) {
  constexpr float HALF = BOOKMARK_PROGRESS_EPSILON / 2;
  constexpr float DOUBLE = BOOKMARK_PROGRESS_EPSILON * 2;
  EXPECT_TRUE(bookmarkMatchesProgress(bookmarkAt(RANGE.start - HALF), SPINE, PAGE, PAGE_COUNT, RANGE));
  EXPECT_TRUE(bookmarkMatchesProgress(bookmarkAt(RANGE.end + HALF), SPINE, PAGE, PAGE_COUNT, RANGE));
  EXPECT_FALSE(bookmarkMatchesProgress(bookmarkAt(RANGE.start - DOUBLE), SPINE, PAGE, PAGE_COUNT, RANGE));
  EXPECT_FALSE(bookmarkMatchesProgress(bookmarkAt(RANGE.end + DOUBLE), SPINE, PAGE, PAGE_COUNT, RANGE));
}

TEST(BookmarkMatch, PercentageIsClampedToTheBook) {
  EXPECT_TRUE(bookmarkMatchesProgress(bookmarkAt(1.5f), SPINE, PAGE, PAGE_COUNT, {0.95f, 1.0f}));
  EXPECT_TRUE(bookmarkMatchesProgress(bookmarkAt(-0.5f), SPINE, PAGE, PAGE_COUNT, {0.0f, 0.05f}));
}

TEST(BookmarkMatch, PartialComputedMatchFallsBackToTheRange) {
  EXPECT_FALSE(bookmarkMatchesProgress(bookmarkAt(0.90f, SPINE, PAGE_COUNT + 1, PAGE), SPINE, PAGE, PAGE_COUNT, RANGE))
      << "a re-paginated chapter must not match by page number";
  EXPECT_TRUE(bookmarkMatchesProgress(bookmarkAt(0.45f, SPINE, PAGE_COUNT + 1, PAGE), SPINE, PAGE, PAGE_COUNT, RANGE))
      << "same spine, different page count: the range still decides";
  EXPECT_TRUE(bookmarkMatchesProgress(bookmarkAt(0.45f, SPINE + 1, PAGE_COUNT, PAGE), SPINE, PAGE, PAGE_COUNT, RANGE));
}
