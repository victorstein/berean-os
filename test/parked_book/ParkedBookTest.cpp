#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <string>

#include "activities/reader/ParkedBookOf.h"

// A parked book is handed back only while it is still the same file with the same cache, and a
// take that misses must let go of what was parked: the slot must never keep a stale Epub alive.

namespace {

struct FakeBook {
  bool cacheStillMatches = true;
  int reopenCalls = 0;
  bool lastSkipLoadingCss = false;

  bool reopenCache(const bool skipLoadingCss) {
    ++reopenCalls;
    lastSkipLoadingCss = skipLoadingCss;
    return cacheStillMatches;
  }
};

struct FakeNav {
  int marker = 0;
};

using Slot = ParkedBookOf<FakeBook, FakeNav>;

constexpr char PATH[] = "/Biblia.epub";
constexpr uint64_t SIZE = 14945282;

}  // namespace

TEST(ParkedBook, AnEmptySlotMisses) {
  Slot slot;
  Slot::Miss miss = Slot::Miss::None;
  EXPECT_EQ(slot.take(PATH, SIZE, false, miss).book, nullptr);
  EXPECT_EQ(miss, Slot::Miss::Empty);
}

TEST(ParkedBook, TheSameUnchangedBookIsHandedBackWithItsNavCache) {
  Slot slot;
  auto book = std::make_shared<FakeBook>();
  auto nav = std::make_shared<FakeNav>();
  slot.park(PATH, SIZE, {book, nav});

  Slot::Miss miss = Slot::Miss::Empty;
  const Slot::Held taken = slot.take(PATH, SIZE, true, miss);
  EXPECT_EQ(taken.book, book);
  EXPECT_EQ(taken.nav, nav);
  EXPECT_EQ(miss, Slot::Miss::None);
  EXPECT_EQ(book->reopenCalls, 1);
  EXPECT_TRUE(book->lastSkipLoadingCss);
  EXPECT_TRUE(slot.empty());
}

TEST(ParkedBook, AnotherPathMissesAndReleasesTheParkedBook) {
  Slot slot;
  auto book = std::make_shared<FakeBook>();
  auto nav = std::make_shared<FakeNav>();
  slot.park(PATH, SIZE, {book, nav});

  Slot::Miss miss = Slot::Miss::None;
  EXPECT_EQ(slot.take("/Atalaya.epub", SIZE, false, miss).book, nullptr);
  EXPECT_EQ(miss, Slot::Miss::OtherPath);
  EXPECT_EQ(book.use_count(), 1);
  EXPECT_EQ(nav.use_count(), 1);
  EXPECT_EQ(book->reopenCalls, 0);
  EXPECT_TRUE(slot.empty());
}

TEST(ParkedBook, AChangedFileSizeMissesAndReleases) {
  Slot slot;
  auto book = std::make_shared<FakeBook>();
  slot.park(PATH, SIZE, {book, nullptr});

  Slot::Miss miss = Slot::Miss::None;
  EXPECT_EQ(slot.take(PATH, SIZE + 1, false, miss).book, nullptr);
  EXPECT_EQ(miss, Slot::Miss::FileChanged);
  EXPECT_EQ(book.use_count(), 1);
  EXPECT_EQ(book->reopenCalls, 0);
}

TEST(ParkedBook, AFileThatCouldNotBeSizedMisses) {
  Slot slot;
  slot.park(PATH, SIZE, {std::make_shared<FakeBook>(), nullptr});

  Slot::Miss miss = Slot::Miss::None;
  EXPECT_EQ(slot.take(PATH, 0, false, miss).book, nullptr);
  EXPECT_EQ(miss, Slot::Miss::FileChanged);
}

TEST(ParkedBook, ACacheThatNoLongerMatchesMissesAndReleases) {
  Slot slot;
  auto book = std::make_shared<FakeBook>();
  book->cacheStillMatches = false;
  auto nav = std::make_shared<FakeNav>();
  slot.park(PATH, SIZE, {book, nav});

  Slot::Miss miss = Slot::Miss::None;
  EXPECT_EQ(slot.take(PATH, SIZE, false, miss).book, nullptr);
  EXPECT_EQ(miss, Slot::Miss::CacheChanged);
  EXPECT_EQ(book->reopenCalls, 1);
  EXPECT_FALSE(book->lastSkipLoadingCss);
  EXPECT_EQ(book.use_count(), 1);
  EXPECT_EQ(nav.use_count(), 1);
}

TEST(ParkedBook, ParkingReplacesAndReleasesThePreviousBook) {
  Slot slot;
  auto first = std::make_shared<FakeBook>();
  auto second = std::make_shared<FakeBook>();
  slot.park("/Atalaya.epub", SIZE, {first, nullptr});
  slot.park(PATH, SIZE, {second, nullptr});
  EXPECT_EQ(first.use_count(), 1);

  Slot::Miss miss = Slot::Miss::None;
  EXPECT_EQ(slot.take(PATH, SIZE, false, miss).book, second);
}

TEST(ParkedBook, ClearReleasesTheParkedBook) {
  Slot slot;
  auto book = std::make_shared<FakeBook>();
  slot.park(PATH, SIZE, {book, nullptr});
  slot.clear();
  EXPECT_TRUE(slot.empty());
  EXPECT_EQ(book.use_count(), 1);
}

TEST(ParkedBook, ATakeThatMissedLeavesNothingForTheNextTake) {
  Slot slot;
  slot.park(PATH, SIZE, {std::make_shared<FakeBook>(), nullptr});

  Slot::Miss miss = Slot::Miss::None;
  EXPECT_EQ(slot.take(PATH, SIZE + 1, false, miss).book, nullptr);
  EXPECT_EQ(slot.take(PATH, SIZE, false, miss).book, nullptr);
  EXPECT_EQ(miss, Slot::Miss::Empty);
}
