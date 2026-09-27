#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>

#include "util/BookProgress.h"

TEST(ParseProgressBytes, ReadsTheSixByteLayout) {
  const uint8_t data[6] = {0x03, 0x00, 0x05, 0x00, 0x14, 0x00};
  SavedProgress progress;
  ASSERT_TRUE(parseProgressBytes(data, sizeof(data), progress));
  EXPECT_EQ(progress.spine, 3);
  EXPECT_EQ(progress.page, 5);
  EXPECT_EQ(progress.pageCount, 20);
}

TEST(ParseProgressBytes, ReadsTheTenByteLayoutAndIgnoresTheTextOffset) {
  const uint8_t data[10] = {0x02, 0x01, 0x07, 0x00, 0x0A, 0x00, 0xAA, 0xBB, 0xCC, 0xDD};
  SavedProgress progress;
  ASSERT_TRUE(parseProgressBytes(data, sizeof(data), progress));
  EXPECT_EQ(progress.spine, 258);
  EXPECT_EQ(progress.page, 7);
  EXPECT_EQ(progress.pageCount, 10);
}

TEST(ParseProgressBytes, LegacyFourByteFileHasNoPageCount) {
  const uint8_t data[4] = {0x01, 0x00, 0x04, 0x00};
  SavedProgress progress;
  ASSERT_TRUE(parseProgressBytes(data, sizeof(data), progress));
  EXPECT_EQ(progress.spine, 1);
  EXPECT_EQ(progress.page, 4);
  EXPECT_EQ(progress.pageCount, 0);
}

TEST(ParseProgressBytes, StaleLastPageSentinelCountsAsPageZero) {
  const uint8_t data[6] = {0x01, 0x00, 0xFF, 0xFF, 0x0A, 0x00};
  SavedProgress progress;
  ASSERT_TRUE(parseProgressBytes(data, sizeof(data), progress));
  EXPECT_EQ(progress.page, 0);
}

TEST(ParseProgressBytes, RefusesEveryOtherSize) {
  const uint8_t data[12] = {};
  SavedProgress progress;
  for (const size_t size : {size_t{0}, size_t{5}, size_t{7}, size_t{11}}) {
    EXPECT_FALSE(parseProgressBytes(data, size, progress)) << size;
  }
  EXPECT_FALSE(parseProgressBytes(nullptr, 6, progress));
}

TEST(ChapterFraction, IsPageOverPageCount) {
  SavedProgress progress;
  progress.page = 5;
  progress.pageCount = 20;
  EXPECT_FLOAT_EQ(chapterFraction(progress), 0.25f);
}

TEST(ChapterFraction, IsZeroWithoutAPageCount) {
  // The reader writes a page count of 0 after a footnote return, and a legacy
  // file has none.
  SavedProgress progress;
  progress.page = 5;
  progress.pageCount = 0;
  EXPECT_FLOAT_EQ(chapterFraction(progress), 0.0f);
}

TEST(ChapterFraction, NeverPassesTheEndOfTheChapter) {
  SavedProgress progress;
  progress.page = 30;
  progress.pageCount = 20;
  EXPECT_FLOAT_EQ(chapterFraction(progress), 1.0f);
}

TEST(RoundPercent, RoundsLikeTheReaderMenu) {
  EXPECT_EQ(roundPercent(0.004f), 0);
  EXPECT_EQ(roundPercent(0.426f), 43);
  EXPECT_EQ(roundPercent(1.0f), 100);
}

TEST(RoundPercent, ClampsBeforeConvertingToInt) {
  EXPECT_EQ(roundPercent(1.2f), 100);
  EXPECT_EQ(roundPercent(-0.1f), 0);
  // The magnitude a wrapped size_t in calculateProgress produces.
  EXPECT_EQ(roundPercent(1e19f), 100);
  EXPECT_EQ(roundPercent(-1e19f), 0);
  EXPECT_EQ(roundPercent(std::nanf("")), 0);
}
