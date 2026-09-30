#include <gtest/gtest.h>

#include "activities/reader/BibleReference.h"

TEST(BibleReference, AppendsAKnownChapter) { EXPECT_EQ(bibleReference("Isaiah", 40), "Isaiah 40"); }

TEST(BibleReference, UnknownChapterLeavesTheBookAlone) {
  EXPECT_EQ(bibleReference("Isaiah", 0), "Isaiah");
  EXPECT_EQ(bibleReference("Isaiah", -1), "Isaiah");
}

TEST(BibleReference, KeepsUtf8BookNames) { EXPECT_EQ(bibleReference("\xC3\x89xodo", 5), "\xC3\x89xodo 5"); }
