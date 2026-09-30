#include <gtest/gtest.h>

#include <cstdint>

#include "activities/reader/BibleNavCache.h"

// A second Go to reads nothing from SD only if the first left a usable copy behind. A copy
// stored from a failed load would open an empty grid on every later Go to, and a chapter list
// handed to the wrong book would open the wrong chapter.

namespace {

struct FakeBooks {
  int bookCount = 0;
  int marker = 0;
};

using Cache = BibleNavCacheOf<FakeBooks>;

}  // namespace

TEST(BibleNavCache, HoldsNoBooksUntilCommitted) {
  Cache cache;
  EXPECT_FALSE(cache.hasBooks());
  EXPECT_EQ(cache.books(), nullptr);
}

TEST(BibleNavCache, CommittedBooksAreServed) {
  Cache cache;
  FakeBooks& books = cache.booksToFill();
  books.bookCount = 66;
  books.marker = 7;
  cache.commitBooks();
  ASSERT_TRUE(cache.hasBooks());
  EXPECT_EQ(cache.books()->bookCount, 66);
  EXPECT_EQ(cache.books()->marker, 7);
}

TEST(BibleNavCache, ACommitWithNoBooksStoresNothing) {
  Cache cache;
  cache.booksToFill().bookCount = 0;
  cache.commitBooks();
  EXPECT_FALSE(cache.hasBooks());
}

TEST(BibleNavCache, RefillingWithdrawsTheCopyUntilCommittedAgain) {
  Cache cache;
  cache.booksToFill().bookCount = 66;
  cache.commitBooks();
  cache.booksToFill();
  EXPECT_FALSE(cache.hasBooks());
}

TEST(BibleNavCache, ChaptersAreServedOnlyForTheBookStored) {
  Cache cache;
  const int16_t spines[] = {30, 31, 32};
  cache.storeChapters(0, spines, 3);

  int count = -1;
  const int16_t* cached = cache.chaptersFor(0, count);
  ASSERT_NE(cached, nullptr);
  EXPECT_EQ(count, 3);
  EXPECT_EQ(cached[2], 32);

  EXPECT_EQ(cache.chaptersFor(1, count), nullptr);
  EXPECT_EQ(count, 0);
}

TEST(BibleNavCache, AnotherBookReplacesTheStoredChapters) {
  Cache cache;
  const int16_t genesis[] = {30, 31};
  const int16_t revelation[] = {1322, 1323, 1324};
  cache.storeChapters(0, genesis, 2);
  cache.storeChapters(65, revelation, 3);

  int count = 0;
  EXPECT_EQ(cache.chaptersFor(0, count), nullptr);
  const int16_t* cached = cache.chaptersFor(65, count);
  ASSERT_NE(cached, nullptr);
  EXPECT_EQ(count, 3);
  EXPECT_EQ(cached[0], 1322);
}

TEST(BibleNavCache, AnUnusableChapterListIsIgnored) {
  Cache cache;
  const int16_t spines[BibleNavLimits::MAX_CHAPTERS + 1] = {};
  int count = 0;
  cache.storeChapters(0, spines, 0);
  EXPECT_EQ(cache.chaptersFor(0, count), nullptr);
  cache.storeChapters(0, spines, BibleNavLimits::MAX_CHAPTERS + 1);
  EXPECT_EQ(cache.chaptersFor(0, count), nullptr);
  cache.storeChapters(-1, spines, 3);
  EXPECT_EQ(cache.chaptersFor(-1, count), nullptr);
  cache.storeChapters(0, nullptr, 3);
  EXPECT_EQ(cache.chaptersFor(0, count), nullptr);
}

TEST(BibleNavCache, StoredChaptersAreACopy) {
  Cache cache;
  int16_t spines[] = {30, 31};
  cache.storeChapters(0, spines, 2);
  spines[0] = 999;
  int count = 0;
  EXPECT_EQ(cache.chaptersFor(0, count)[0], 30);
}
