#include <gtest/gtest.h>

#include <algorithm>
#include <vector>

#include "activities/reader/ReaderMenuModel.h"

namespace {

using A = ReaderMenuAction;

struct Flags {
  bool isBible;
  bool hasFootnotes;
  bool hasBookmarks;
  bool hasHighlights;
  bool hasFrontlight;
  bool hasRotation;
  bool bibleReachable;
};

// The items EpubReaderMenuActivity::buildMenuItems built before the sheet
// (EpubReaderMenuActivity.cpp:41-82 at 67eaaf1e). The sheet must keep every one
// of them except Go to % in a Bible.
std::vector<A> legacyItems(const Flags& f) {
  std::vector<A> items{A::SELECT_CHAPTER};
  if (f.isBible) items.push_back(A::SEARCH_BIBLE);
  if (f.hasFootnotes) items.push_back(A::FOOTNOTES);
  if (f.hasBookmarks) items.push_back(A::BOOKMARKS);
  if (f.hasHighlights) items.push_back(A::HIGHLIGHTS);
  items.push_back(A::TOGGLE_BOOKMARK);
  if (f.hasHighlights) items.push_back(A::HIGHLIGHT_PASSAGE);
  items.push_back(A::TEXT_SETTINGS);
  items.push_back(A::NIGHT_MODE);
  if (f.hasFrontlight) items.push_back(A::FRONTLIGHT);
  if (f.hasRotation) items.push_back(A::ROTATE_SCREEN);
  items.push_back(A::AUTO_PAGE_TURN);
  items.push_back(A::GO_TO_PERCENT);
  items.push_back(A::SCREENSHOT);
  items.push_back(A::GO_HOME);
  items.push_back(A::DELETE_CACHE);
  return items;
}

ReaderMenuModel::Inputs inputsFor(const Flags& f, const int tagsHere) {
  ReaderMenuModel::Inputs in;
  in.isBible = f.isBible;
  in.hasFootnotes = f.hasFootnotes;
  in.hasBookmarks = f.hasBookmarks;
  in.hasHighlights = f.hasHighlights;
  in.hasFrontlight = f.hasFrontlight;
  in.hasRotation = f.hasRotation;
  in.bibleReachable = f.bibleReachable;
  in.tagsHereCount = tagsHere;
  return in;
}

std::vector<A> quickOf(const ReaderMenuModel::Model& m) { return {m.items, m.items + m.quickCount}; }
std::vector<A> rowsOf(const ReaderMenuModel::Model& m) { return {m.items + m.quickCount, m.items + m.count()}; }
std::vector<A> allOf(const ReaderMenuModel::Model& m) { return {m.items, m.items + m.count()}; }

constexpr Flags BIBLE_ALL{true, true, true, true, true, false, true};
constexpr Flags BOOK_ALL{false, true, true, true, true, false, true};

}  // namespace

TEST(ReaderMenuModel, BibleGetsFourQuickActionsInOrder) {
  const auto m = ReaderMenuModel::build(inputsFor(BIBLE_ALL, 0));
  EXPECT_EQ(quickOf(m), (std::vector<A>{A::SELECT_CHAPTER, A::SEARCH_BIBLE, A::TOGGLE_BOOKMARK, A::HIGHLIGHT_PASSAGE}));
}

TEST(ReaderMenuModel, NonBibleGetsThreeQuickActionsWithoutSearch) {
  const auto m = ReaderMenuModel::build(inputsFor(BOOK_ALL, 0));
  EXPECT_EQ(quickOf(m), (std::vector<A>{A::SELECT_CHAPTER, A::TOGGLE_BOOKMARK, A::HIGHLIGHT_PASSAGE}));
}

TEST(ReaderMenuModel, TagQuickActionNeedsHighlights) {
  Flags f = BOOK_ALL;
  f.hasHighlights = false;
  const auto m = ReaderMenuModel::build(inputsFor(f, 0));
  EXPECT_EQ(quickOf(m), (std::vector<A>{A::SELECT_CHAPTER, A::TOGGLE_BOOKMARK}));
}

TEST(ReaderMenuModel, BibleHidesGoToPercentAndBooksKeepIt) {
  const auto bible = rowsOf(ReaderMenuModel::build(inputsFor(BIBLE_ALL, 0)));
  const auto book = rowsOf(ReaderMenuModel::build(inputsFor(BOOK_ALL, 0)));
  EXPECT_EQ(std::count(bible.begin(), bible.end(), A::GO_TO_PERCENT), 0);
  EXPECT_EQ(std::count(book.begin(), book.end(), A::GO_TO_PERCENT), 1);
}

TEST(ReaderMenuModel, BibleRowOrderWithTagsHere) {
  const auto m = ReaderMenuModel::build(inputsFor(BIBLE_ALL, 2));
  EXPECT_EQ(rowsOf(m),
            (std::vector<A>{A::BOOKMARKS, A::TAGS_HERE, A::HIGHLIGHTS, A::FOOTNOTES, A::TEXT_SETTINGS, A::NIGHT_MODE,
                            A::FRONTLIGHT, A::AUTO_PAGE_TURN, A::SCREENSHOT, A::GO_HOME, A::DELETE_CACHE}));
}

TEST(ReaderMenuModel, NonBibleRowOrder) {
  const auto m = ReaderMenuModel::build(inputsFor(BOOK_ALL, 2));
  EXPECT_EQ(rowsOf(m),
            (std::vector<A>{A::BOOKMARKS, A::HIGHLIGHTS, A::FOOTNOTES, A::TEXT_SETTINGS, A::NIGHT_MODE, A::FRONTLIGHT,
                            A::AUTO_PAGE_TURN, A::GO_TO_PERCENT, A::SCREENSHOT, A::GO_HOME, A::DELETE_CACHE}));
}

TEST(ReaderMenuModel, TagsHereOnlyInABibleChapterWithPassages) {
  auto has = [](const ReaderMenuModel::Model& m) {
    const auto rows = rowsOf(m);
    return std::count(rows.begin(), rows.end(), A::TAGS_HERE) == 1;
  };
  EXPECT_FALSE(has(ReaderMenuModel::build(inputsFor(BIBLE_ALL, 0))));
  EXPECT_TRUE(has(ReaderMenuModel::build(inputsFor(BIBLE_ALL, 1))));
  EXPECT_FALSE(has(ReaderMenuModel::build(inputsFor(BOOK_ALL, 3))));
  Flags noHighlights = BIBLE_ALL;
  noHighlights.hasHighlights = false;
  EXPECT_FALSE(has(ReaderMenuModel::build(inputsFor(noHighlights, 3))));
}

TEST(ReaderMenuModel, TagsHereLeadsWhenThereAreNoBookmarks) {
  Flags f = BIBLE_ALL;
  f.hasBookmarks = false;
  const auto m = ReaderMenuModel::build(inputsFor(f, 1));
  ASSERT_GT(m.rowCount, 0);
  EXPECT_EQ(m.row(0), A::TAGS_HERE);
}

TEST(ReaderMenuModel, WorstCaseCountsOnTheX4Pro) {
  const auto bible = ReaderMenuModel::build(inputsFor(BIBLE_ALL, 1));
  EXPECT_EQ(bible.quickCount, 4);
  EXPECT_EQ(bible.rowCount, 11);
  const auto book = ReaderMenuModel::build(inputsFor(BOOK_ALL, 0));
  EXPECT_EQ(book.quickCount, 3);
  EXPECT_EQ(book.rowCount, 11);
}

// The acceptance criterion "keeps all their items", for every flag combination:
// the sheet has exactly today's items, minus Go to % in a Bible, plus Tags here,
// minus both tag entries in a non-Bible with no Bible to open (issue #235).
TEST(ReaderMenuModel, EveryLegacyItemSurvivesForEveryFlagCombination) {
  for (int mask = 0; mask < 128; ++mask) {
    const Flags f{(mask & 1) != 0,  (mask & 2) != 0,  (mask & 4) != 0, (mask & 8) != 0,
                  (mask & 16) != 0, (mask & 32) != 0, (mask & 64) != 0};
    for (const int tagsHere : {0, 3}) {
      std::vector<A> expected = legacyItems(f);
      if (f.isBible) expected.erase(std::remove(expected.begin(), expected.end(), A::GO_TO_PERCENT), expected.end());
      if (f.isBible && f.hasHighlights && tagsHere > 0) expected.push_back(A::TAGS_HERE);
      if (!f.isBible && !f.bibleReachable) {
        expected.erase(std::remove(expected.begin(), expected.end(), A::HIGHLIGHTS), expected.end());
        expected.erase(std::remove(expected.begin(), expected.end(), A::HIGHLIGHT_PASSAGE), expected.end());
      }
      const auto m = ReaderMenuModel::build(inputsFor(f, tagsHere));
      std::vector<A> actual = allOf(m);
      std::sort(expected.begin(), expected.end());
      std::sort(actual.begin(), actual.end());
      EXPECT_EQ(actual, expected) << "mask=" << mask << " tagsHere=" << tagsHere;
      EXPECT_FALSE(m.overflowed) << "mask=" << mask;
      EXPECT_LE(m.rowCount, ReaderMenuModel::MAX_ROWS);
    }
  }
}

TEST(ReaderMenuModel, RowsPastCapacityAreDroppedAndReported) {
  ReaderMenuModel::Model m;
  for (int i = 0; i < ReaderMenuModel::MAX_ROWS + 1; ++i) m.addRow(A::SCREENSHOT);
  EXPECT_EQ(m.rowCount, ReaderMenuModel::MAX_ROWS);
  EXPECT_TRUE(m.overflowed);
}

TEST(ReaderMenuModel, QuickActionAfterRowsIsRefused) {
  ReaderMenuModel::Model m;
  m.addRow(A::SCREENSHOT);
  m.addQuick(A::SELECT_CHAPTER);
  EXPECT_EQ(m.quickCount, 0);
  EXPECT_EQ(m.row(0), A::SCREENSHOT);
  EXPECT_TRUE(m.overflowed);
}

TEST(ReaderMenuModel, TagTargetRule) {
  using T = ReaderMenuModel::TagTarget;
  using ReaderMenuModel::tagTarget;
  // (hasHighlights, isBible, bibleReachable)
  EXPECT_EQ(tagTarget(false, false, false), T::Hidden);
  EXPECT_EQ(tagTarget(false, false, true), T::Hidden);
  EXPECT_EQ(tagTarget(false, true, false), T::Hidden);
  EXPECT_EQ(tagTarget(false, true, true), T::Hidden);
  EXPECT_EQ(tagTarget(true, false, false), T::Hidden);
  EXPECT_EQ(tagTarget(true, false, true), T::BibleTags);
  EXPECT_EQ(tagTarget(true, true, false), T::ThisBook);
  EXPECT_EQ(tagTarget(true, true, true), T::ThisBook);
}

TEST(ReaderMenuModel, NonBibleWithABibleKeepsBothTagEntries) {
  const auto m = ReaderMenuModel::build(inputsFor(BOOK_ALL, 0));
  EXPECT_EQ(quickOf(m), (std::vector<A>{A::SELECT_CHAPTER, A::TOGGLE_BOOKMARK, A::HIGHLIGHT_PASSAGE}));
  const auto rows = rowsOf(m);
  EXPECT_EQ(std::count(rows.begin(), rows.end(), A::HIGHLIGHTS), 1);
}

TEST(ReaderMenuModel, NonBibleWithoutABibleHidesBothTagEntries) {
  Flags f = BOOK_ALL;
  f.bibleReachable = false;
  const auto m = ReaderMenuModel::build(inputsFor(f, 0));
  EXPECT_EQ(quickOf(m), (std::vector<A>{A::SELECT_CHAPTER, A::TOGGLE_BOOKMARK}));
  EXPECT_EQ(rowsOf(m),
            (std::vector<A>{A::BOOKMARKS, A::FOOTNOTES, A::TEXT_SETTINGS, A::NIGHT_MODE, A::FRONTLIGHT,
                            A::AUTO_PAGE_TURN, A::GO_TO_PERCENT, A::SCREENSHOT, A::GO_HOME, A::DELETE_CACHE}));
}

TEST(ReaderMenuModel, BibleIgnoresBibleReachable) {
  Flags without = BIBLE_ALL;
  without.bibleReachable = false;
  EXPECT_EQ(allOf(ReaderMenuModel::build(inputsFor(BIBLE_ALL, 2))), allOf(ReaderMenuModel::build(inputsFor(without, 2))));
}

TEST(ReaderMenuModel, BibleReachableDefaultsToHidden) {
  ReaderMenuModel::Inputs in;
  in.hasHighlights = true;
  const auto m = ReaderMenuModel::build(in);
  const auto all = allOf(m);
  EXPECT_EQ(std::count(all.begin(), all.end(), A::HIGHLIGHTS), 0);
  EXPECT_EQ(std::count(all.begin(), all.end(), A::HIGHLIGHT_PASSAGE), 0);
}
