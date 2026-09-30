#include <gtest/gtest.h>

#include "activities/reader/GridMarks.h"

// A chapter or verse cell carries a mark when the reader has a tagged passage or
// a bookmark in it. Fixtures are synthetic spine numbers and offsets only.

namespace {

constexpr uint8_t ISAIAH = 23;

study::Unit verseUnit(const uint8_t book, const uint16_t chapter, const uint16_t verse) {
  study::Unit unit;
  unit.kind = study::UnitKind::Verse;
  unit.book = book;
  unit.major = chapter;
  unit.minor = verse;
  return unit;
}

study::Unit paragraphUnit() {
  study::Unit unit;
  unit.kind = study::UnitKind::Paragraph;
  unit.minor = 40;
  return unit;
}

int countSet(const GridMarks::Bits& bits) {
  int count = 0;
  for (int i = 0; i < GridMarks::CAPACITY; i++) count += bits.test(i) ? 1 : 0;
  return count;
}

// Isaiah 40, verses 1..31, one anchor every 100 codepoints from offset 50.
std::vector<VerseAnchors::VerseAnchor> isaiah40() {
  std::vector<VerseAnchors::VerseAnchor> anchors;
  anchors.reserve(31);
  for (uint16_t verse = 1; verse <= 31; verse++) {
    anchors.push_back(VerseAnchors::VerseAnchor{static_cast<uint32_t>(50 + (verse - 1) * 100), 40, verse});
  }
  return anchors;
}

}  // namespace

TEST(GridMarksBits, SetThenTest) {
  GridMarks::Bits bits;
  bits.set(0);
  bits.set(39);
  bits.set(GridMarks::CAPACITY - 1);

  EXPECT_TRUE(bits.test(0));
  EXPECT_TRUE(bits.test(39));
  EXPECT_TRUE(bits.test(GridMarks::CAPACITY - 1));
  EXPECT_FALSE(bits.test(40));
  EXPECT_EQ(countSet(bits), 3);
}

TEST(GridMarksBits, ClearResetsEverything) {
  GridMarks::Bits bits;
  bits.set(5);
  bits.clear();

  EXPECT_EQ(countSet(bits), 0);
}

TEST(GridMarksBits, OutOfRangeIsIgnoredNotWritten) {
  GridMarks::Bits bits;
  bits.set(-1);
  bits.set(GridMarks::CAPACITY);

  EXPECT_EQ(countSet(bits), 0);
  EXPECT_FALSE(bits.test(-1));
  EXPECT_FALSE(bits.test(GridMarks::CAPACITY));
}

TEST(GridMarksSpan, SingleVerse) {
  GridMarks::VerseSpan span;
  ASSERT_TRUE(GridMarks::spanFor(verseUnit(ISAIAH, 40, 31), verseUnit(ISAIAH, 40, 31), ISAIAH, span));

  EXPECT_EQ(span.startChapter, 40);
  EXPECT_EQ(span.startVerse, 31);
  EXPECT_EQ(span.endChapter, 40);
  EXPECT_EQ(span.endVerse, 31);
}

TEST(GridMarksSpan, AcrossChapters) {
  GridMarks::VerseSpan span;
  ASSERT_TRUE(GridMarks::spanFor(verseUnit(ISAIAH, 5, 30), verseUnit(ISAIAH, 6, 2), ISAIAH, span));

  EXPECT_EQ(span.startChapter, 5);
  EXPECT_EQ(span.endChapter, 6);
  EXPECT_EQ(span.endVerse, 2);
}

TEST(GridMarksSpan, OtherBookOrNonVerseStartNamesNothing) {
  GridMarks::VerseSpan span;

  EXPECT_FALSE(GridMarks::spanFor(verseUnit(ISAIAH + 1, 1, 1), verseUnit(ISAIAH + 1, 1, 1), ISAIAH, span));
  EXPECT_FALSE(GridMarks::spanFor(paragraphUnit(), paragraphUnit(), ISAIAH, span));
}

TEST(GridMarksSpan, UnusableEndFallsBackToTheStart) {
  GridMarks::VerseSpan span;

  ASSERT_TRUE(GridMarks::spanFor(verseUnit(ISAIAH, 40, 3), paragraphUnit(), ISAIAH, span));
  EXPECT_EQ(span.endChapter, 40);
  EXPECT_EQ(span.endVerse, 3);

  ASSERT_TRUE(GridMarks::spanFor(verseUnit(ISAIAH, 40, 3), verseUnit(ISAIAH, 39, 8), ISAIAH, span));
  EXPECT_EQ(span.endChapter, 40);
  EXPECT_EQ(span.endVerse, 3);

  ASSERT_TRUE(GridMarks::spanFor(verseUnit(ISAIAH, 66, 24), verseUnit(ISAIAH + 1, 1, 1), ISAIAH, span));
  EXPECT_EQ(span.endChapter, 66);
  EXPECT_EQ(span.endVerse, 24);
}

TEST(GridMarksPassageChapters, SingleVerseMarksItsChapter) {
  GridMarks::Bits bits;
  GridMarks::VerseSpan span;
  ASSERT_TRUE(GridMarks::spanFor(verseUnit(ISAIAH, 40, 31), verseUnit(ISAIAH, 40, 31), ISAIAH, span));
  GridMarks::markPassageChapters(bits, span, 66);

  EXPECT_TRUE(bits.test(39));
  EXPECT_EQ(countSet(bits), 1);
}

TEST(GridMarksPassageChapters, SpanMarksEveryChapterItCrosses) {
  GridMarks::Bits bits;
  GridMarks::VerseSpan span;
  ASSERT_TRUE(GridMarks::spanFor(verseUnit(ISAIAH, 5, 30), verseUnit(ISAIAH, 6, 2), ISAIAH, span));
  GridMarks::markPassageChapters(bits, span, 66);

  EXPECT_TRUE(bits.test(4));
  EXPECT_TRUE(bits.test(5));
  EXPECT_EQ(countSet(bits), 2);
}

TEST(GridMarksPassageChapters, ChaptersPastTheListAreDropped) {
  GridMarks::Bits bits;
  GridMarks::markPassageChapters(bits, GridMarks::VerseSpan{65, 1, 70, 1}, 66);

  EXPECT_TRUE(bits.test(64));
  EXPECT_TRUE(bits.test(65));
  EXPECT_EQ(countSet(bits), 2);
}

TEST(GridMarksPassageVerses, MarksExactlyTheVersesInRange) {
  const auto anchors = isaiah40();
  GridMarks::Bits bits;
  GridMarks::markPassageVerses(bits, GridMarks::VerseSpan{40, 3, 40, 5}, anchors.data(),
                               static_cast<int>(anchors.size()));

  EXPECT_TRUE(bits.test(2));
  EXPECT_TRUE(bits.test(3));
  EXPECT_TRUE(bits.test(4));
  EXPECT_EQ(countSet(bits), 3);
}

TEST(GridMarksPassageVerses, SpanEnteringFromThePreviousChapter) {
  const auto anchors = isaiah40();
  GridMarks::Bits bits;
  GridMarks::markPassageVerses(bits, GridMarks::VerseSpan{39, 20, 40, 2}, anchors.data(),
                               static_cast<int>(anchors.size()));

  EXPECT_TRUE(bits.test(0));
  EXPECT_TRUE(bits.test(1));
  EXPECT_EQ(countSet(bits), 2);
}

TEST(GridMarksPassageVerses, OtherChapterMarksNothing) {
  const auto anchors = isaiah40();
  GridMarks::Bits bits;
  GridMarks::markPassageVerses(bits, GridMarks::VerseSpan{41, 1, 41, 10}, anchors.data(),
                               static_cast<int>(anchors.size()));

  EXPECT_EQ(countSet(bits), 0);
}

TEST(GridMarksPassageVerses, SingleChapterBookUsesTheAnchorsOwnChapter) {
  std::vector<VerseAnchors::VerseAnchor> obadiah;
  obadiah.reserve(21);
  for (uint16_t verse = 1; verse <= 21; verse++) obadiah.push_back(VerseAnchors::VerseAnchor{verse * 10U, 1, verse});
  GridMarks::Bits bits;
  GridMarks::markPassageVerses(bits, GridMarks::VerseSpan{1, 21, 1, 21}, obadiah.data(),
                               static_cast<int>(obadiah.size()));

  EXPECT_TRUE(bits.test(20));
  EXPECT_EQ(countSet(bits), 1);
}

TEST(GridMarksBookmarkChapters, SpineMatchMarksTheRow) {
  int16_t chapterSpine[66];
  for (int row = 0; row < 66; row++) chapterSpine[row] = static_cast<int16_t>(1000 + row);
  const GridMarks::BookmarkPosition bookmarks[] = {{1039, true, 400}, {999, false, 0}, {1065, false, 0}};
  GridMarks::Bits bits;
  GridMarks::markBookmarkChapters(bits, bookmarks, 3, chapterSpine, 66);

  EXPECT_TRUE(bits.test(39));
  EXPECT_TRUE(bits.test(65));
  EXPECT_EQ(countSet(bits), 2);
}

TEST(GridMarksBookmarkChapters, NoBookmarksMarkNothing) {
  int16_t chapterSpine[2] = {1000, 1001};
  GridMarks::Bits bits;
  GridMarks::markBookmarkChapters(bits, nullptr, 0, chapterSpine, 2);

  EXPECT_EQ(countSet(bits), 0);
}

TEST(GridMarksVerseAtOffset, PicksTheLastAnchorAtOrBefore) {
  const auto anchors = isaiah40();
  const int count = static_cast<int>(anchors.size());

  EXPECT_EQ(GridMarks::verseCellAtOffset(anchors.data(), count, 10), 0);
  EXPECT_EQ(GridMarks::verseCellAtOffset(anchors.data(), count, 350), 3);
  EXPECT_EQ(GridMarks::verseCellAtOffset(anchors.data(), count, 449), 3);
  EXPECT_EQ(GridMarks::verseCellAtOffset(anchors.data(), count, 100000), 30);
  EXPECT_EQ(GridMarks::verseCellAtOffset(anchors.data(), 0, 350), -1);
}

TEST(GridMarksBookmarkVerses, OnlyOffsetBookmarksInThisSpineMark) {
  const auto anchors = isaiah40();
  const GridMarks::BookmarkPosition bookmarks[] = {
      {1039, true, 350},   // verse 4
      {1039, false, 0},    // pre-offset: its chapter only
      {1040, true, 1000},  // another chapter
  };
  GridMarks::Bits bits;
  GridMarks::markBookmarkVerses(bits, bookmarks, 3, 1039, anchors.data(), static_cast<int>(anchors.size()));

  EXPECT_TRUE(bits.test(3));
  EXPECT_EQ(countSet(bits), 1);
}
