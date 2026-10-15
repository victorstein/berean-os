#include <gtest/gtest.h>

#include <vector>

#include "activities/preaching/PreachingCardNav.h"

using preaching::CardCursor;
using preaching::Layout;

namespace {

// A layout reduced to what navigation reads: the page count and where each Verse card begins.
Layout pagesWithVerses(const int pages, const std::vector<uint16_t>& verseFirstPage) {
  Layout layout;
  for (int page = 0; page < pages; ++page) layout.pageFirstSegment.push_back(0);
  layout.verseFirstPage = verseFirstPage;
  return layout;
}

bool sameCursor(const CardCursor& a, const CardCursor& b) { return a.page == b.page && a.selected == b.selected; }

}  // namespace

TEST(PreachingCardNav, RightStepsThroughAPageThenTurnsIt) {
  const Layout layout = pagesWithVerses(2, {0, 0, 1});
  CardCursor cursor = preaching::selectVerse(layout, 0);
  ASSERT_TRUE(preaching::nextCard(layout, cursor));
  EXPECT_TRUE(sameCursor(cursor, {0, 1}));
  ASSERT_TRUE(preaching::nextCard(layout, cursor));
  EXPECT_TRUE(sameCursor(cursor, {1, 2}));
  EXPECT_FALSE(preaching::nextCard(layout, cursor));
  EXPECT_TRUE(sameCursor(cursor, {1, 2}));
}

TEST(PreachingCardNav, RightReachesAPageWithoutVerses) {
  const Layout layout = pagesWithVerses(3, {0, 2});
  CardCursor cursor = preaching::selectVerse(layout, 0);
  ASSERT_TRUE(preaching::nextCard(layout, cursor));
  EXPECT_TRUE(sameCursor(cursor, {1, -1}));
  ASSERT_TRUE(preaching::nextCard(layout, cursor));
  EXPECT_TRUE(sameCursor(cursor, {2, 1}));
}

TEST(PreachingCardNav, ProseOnlyTopicStillPages) {
  const Layout layout = pagesWithVerses(3, {});
  CardCursor cursor = preaching::selectVerse(layout, 0);
  EXPECT_TRUE(sameCursor(cursor, {0, -1}));
  ASSERT_TRUE(preaching::nextCard(layout, cursor));
  ASSERT_TRUE(preaching::nextCard(layout, cursor));
  EXPECT_TRUE(sameCursor(cursor, {2, -1}));
  EXPECT_FALSE(preaching::nextCard(layout, cursor));
}

TEST(PreachingCardNav, LeftMirrorsRightAndSelectsThePreviousPagesLastVerse) {
  const Layout layout = pagesWithVerses(3, {0, 0, 2});
  CardCursor cursor = preaching::selectVerse(layout, 2);
  ASSERT_TRUE(preaching::previousCard(layout, cursor));
  EXPECT_TRUE(sameCursor(cursor, {1, -1}));
  ASSERT_TRUE(preaching::previousCard(layout, cursor));
  EXPECT_TRUE(sameCursor(cursor, {0, 1}));
  ASSERT_TRUE(preaching::previousCard(layout, cursor));
  EXPECT_TRUE(sameCursor(cursor, {0, 0}));
  EXPECT_FALSE(preaching::previousCard(layout, cursor));
}

TEST(PreachingCardNav, SwipesTurnPagesAndSelectTheirFirstVerse) {
  const Layout layout = pagesWithVerses(3, {0, 1, 1});
  CardCursor cursor = preaching::selectVerse(layout, 0);
  ASSERT_TRUE(preaching::turnPage(layout, cursor, +1));
  EXPECT_TRUE(sameCursor(cursor, {1, 1}));
  ASSERT_TRUE(preaching::turnPage(layout, cursor, +1));
  EXPECT_TRUE(sameCursor(cursor, {2, -1}));
  EXPECT_FALSE(preaching::turnPage(layout, cursor, +1));
  ASSERT_TRUE(preaching::turnPage(layout, cursor, -1));
  EXPECT_TRUE(sameCursor(cursor, {1, 1}));
}

TEST(PreachingCardNav, SelectingAVerseShowsItsPage) {
  const Layout layout = pagesWithVerses(3, {0, 1, 2});
  EXPECT_TRUE(sameCursor(preaching::selectVerse(layout, 2), {2, 2}));
  EXPECT_TRUE(sameCursor(preaching::selectVerse(layout, 9), {0, 0}));
  EXPECT_TRUE(sameCursor(preaching::selectVerse(layout, -1), {0, 0}));
}

TEST(PreachingCardNav, AFreshOpenStartsOnTheFirstPage) {
  const Layout layout = pagesWithVerses(3, {1, 2});
  EXPECT_TRUE(sameCursor(preaching::atPage(layout, 0), {0, -1}));
  EXPECT_TRUE(sameCursor(preaching::selectVerse(layout, 0), {1, 0}));
}
