#include <gtest/gtest.h>

#include "activities/reader/BibleEntryPosition.h"

// Select chapter opens where the reader already is, so every one of the 1,189
// chapters has to map to its own book and row, and every other spine item has
// to fall back rather than guess. The fixture is the spine layout measured on
// the English and Spanish NWT EPUBs, which is identical in both: spine numbers
// only, no publisher text.

namespace {

constexpr int BOOKS = 66;

// bookTargetSpine as loadBooks() resolves it: a book's chapter-nav page, or for
// a single-chapter book its only chapter.
constexpr int16_t BOOK_TARGET[BOOKS] = {
    29,   81,   123,  152,  190,  226,  252,  275,  281,  314,  340,  364,  391,  422,  460,  472,  487,
    499,  543,  695,  728,  742,  752,  820,  874,  881,  931,  945,  961,  966,  977,  979,  985,  994,
    999,  1004, 1009, 1013, 1029, 1035, 1065, 1083, 1109, 1132, 1162, 1180, 1198, 1213, 1221, 1229, 1235,
    1241, 1248, 1253, 1261, 1267, 1272, 1274, 1289, 1296, 1303, 1308, 1315, 1317, 1319, 1321};

constexpr int CHAPTERS[BOOKS] = {50, 40,  27, 36, 34, 24, 21, 4,  31, 24, 22, 25, 29, 36, 10, 13, 10,
                                 42, 150, 31, 12, 8,  66, 52, 5,  48, 12, 14, 3,  9,  1,  4,  7,  3,
                                 3,  3,   2,  14, 4,  28, 16, 24, 21, 28, 16, 16, 13, 6,  6,  4,  4,
                                 5,  3,   6,  4,  3,  1,  13, 5,  5,  3,  5,  1,  1,  1,  22};

// Obadiah, Philemon, 2 John, 3 John, Jude.
constexpr int SINGLE_CHAPTER_BOOKS[] = {30, 56, 62, 63, 64};

constexpr int GENESIS = 0;
constexpr int PSALMS = 18;
constexpr int REVELATION = 65;
constexpr int LAST_SPINE = 3940;

struct DirectFlags {
  bool isDirect[BOOKS] = {};
  DirectFlags() {
    for (const int book : SINGLE_CHAPTER_BOOKS) isDirect[book] = true;
  }
};

const DirectFlags& directFlags() {
  static const DirectFlags flags;
  return flags;
}

// chapterSpine as loadChapters() resolves it: every measured multi-chapter book
// has its chapters contiguous, straight after its nav page.
struct ChapterList {
  int16_t spine[150] = {};
  int count = 0;
  explicit ChapterList(const int book) : count(CHAPTERS[book]) {
    for (int k = 0; k < count; k++) spine[k] = static_cast<int16_t>(BOOK_TARGET[book] + 1 + k);
  }
};

BibleEntry::Entry classify(const int spine) {
  return BibleEntry::classify(BOOK_TARGET, directFlags().isDirect, BOOKS, spine);
}

int rowInBook(const int spine, const int book) {
  const ChapterList chapters(book);
  return BibleEntry::chapterRowFor(chapters.spine, chapters.count, spine);
}

void expectChapter(const int spine, const int book, const int row) {
  const BibleEntry::Entry entry = classify(spine);
  EXPECT_EQ(entry.kind, BibleEntry::Kind::LoadChapters) << "spine " << spine;
  EXPECT_EQ(entry.book, book) << "spine " << spine;
  EXPECT_EQ(rowInBook(spine, book), row) << "spine " << spine;
}

void expectFallback(const int spine) {
  const BibleEntry::Entry entry = classify(spine);
  EXPECT_EQ(entry.kind, BibleEntry::Kind::None) << "spine " << spine;
  EXPECT_EQ(entry.book, -1) << "spine " << spine;
}

}  // namespace

TEST(BibleEntryChapter, Genesis32OpensOnRow31) { expectChapter(61, GENESIS, 31); }

TEST(BibleEntryChapter, FirstAndLastChapterOfABook) {
  expectChapter(30, GENESIS, 0);
  expectChapter(79, GENESIS, 49);
}

TEST(BibleEntryChapter, Psalm119AndThe150thChapterEdge) {
  expectChapter(662, PSALMS, 118);
  expectChapter(693, PSALMS, 149);
}

TEST(BibleEntryChapter, Revelation22) { expectChapter(1343, REVELATION, 21); }

TEST(BibleEntryChapter, EveryChapterOfEveryBookRoundTrips) {
  int chapters = 0;
  for (int book = 0; book < BOOKS; book++) {
    if (directFlags().isDirect[book]) {
      chapters++;
      continue;
    }
    for (int k = 0; k < CHAPTERS[book]; k++) {
      expectChapter(BOOK_TARGET[book] + 1 + k, book, k);
      chapters++;
    }
  }
  EXPECT_EQ(chapters, 1189);
}

TEST(BibleEntrySelectBook, EachSingleChapterBookSelectsItself) {
  for (const int book : SINGLE_CHAPTER_BOOKS) {
    const BibleEntry::Entry entry = classify(BOOK_TARGET[book]);
    EXPECT_EQ(entry.kind, BibleEntry::Kind::SelectBook) << "book " << book;
    EXPECT_EQ(entry.book, book);
  }
}

TEST(BibleEntrySelectBook, ABooksOwnNavPageSelectsIt) {
  const BibleEntry::Entry genesis = classify(29);
  EXPECT_EQ(genesis.kind, BibleEntry::Kind::SelectBook);
  EXPECT_EQ(genesis.book, GENESIS);

  const BibleEntry::Entry exodus = classify(81);
  EXPECT_EQ(exodus.kind, BibleEntry::Kind::SelectBook);
  EXPECT_EQ(exodus.book, 1);
}

TEST(BibleEntryFallback, FrontMatterBookNavAndGenesisOutline) {
  expectFallback(0);
  expectFallback(2);
  expectFallback(28);
}

// Jonah's outline sits between Obadiah's only chapter and Jonah's nav page.
TEST(BibleEntryFallback, PastASingleChapterBookNeedsNoLoad) { expectFallback(978); }

// Exodus's outline sits between Genesis 50 and Exodus's nav page: the range
// rule guesses Genesis, and Genesis's chapter list rejects it.
TEST(BibleEntryFallback, NextBooksOutlineIsNotAChapter) {
  const BibleEntry::Entry entry = classify(80);
  EXPECT_EQ(entry.kind, BibleEntry::Kind::LoadChapters);
  EXPECT_EQ(entry.book, GENESIS);
  EXPECT_EQ(rowInBook(80, GENESIS), -1);
}

TEST(BibleEntryFallback, AppendicesAfterRevelationAreNotChapters) {
  constexpr int APPENDIX_SPINES[] = {1344, LAST_SPINE};
  for (const int spine : APPENDIX_SPINES) {
    const BibleEntry::Entry entry = classify(spine);
    EXPECT_EQ(entry.kind, BibleEntry::Kind::LoadChapters) << "spine " << spine;
    EXPECT_EQ(entry.book, REVELATION);
    EXPECT_EQ(rowInBook(spine, REVELATION), -1);
  }
}

TEST(BibleEntryDegenerate, NoPositionOrNoBooks) {
  expectFallback(-1);
  EXPECT_EQ(BibleEntry::classify(BOOK_TARGET, directFlags().isDirect, 0, 61).kind, BibleEntry::Kind::None);
  EXPECT_EQ(BibleEntry::classify(nullptr, nullptr, 0, 61).kind, BibleEntry::Kind::None);
  EXPECT_EQ(BibleEntry::chapterRowFor(nullptr, 0, 61), -1);
  const ChapterList genesis(GENESIS);
  EXPECT_EQ(BibleEntry::chapterRowFor(genesis.spine, genesis.count, -1), -1);
}

TEST(BibleEntryBookFor, AnUnresolvedTargetNeverWins) {
  constexpr int16_t targets[] = {-1, 10, 20};
  EXPECT_EQ(BibleEntry::bookFor(targets, 3, 5), -1);
  EXPECT_EQ(BibleEntry::bookFor(targets, 3, 15), 1);
  EXPECT_EQ(BibleEntry::bookFor(targets, 3, 25), 2);
}

TEST(BibleEntryBookFor, UnsortedTargetsStillPickTheLargestAtOrBelow) {
  constexpr int16_t targets[] = {20, 10, -1};
  EXPECT_EQ(BibleEntry::bookFor(targets, 3, 15), 1);
  EXPECT_EQ(BibleEntry::bookFor(targets, 3, 25), 0);
}

TEST(BibleEntryBookFor, ATieGoesToTheLowerIndex) {
  constexpr int16_t targets[] = {10, 10};
  EXPECT_EQ(BibleEntry::bookFor(targets, 2, 12), 0);
}
