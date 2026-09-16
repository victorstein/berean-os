// Host coverage for BookmarkFile::save's decision rule.
//
// BookmarkFile.cpp itself cannot be built on the host: it includes
// <PersistableStore.h>, which includes <Arduino.h> unconditionally. See the
// comment atop util/HighlightFileAction.h for the full reasoning. This suite
// drives the pure rule; the Storage call sequence around it is device-verified.

#include <SaveBudget.h>
#include <gtest/gtest.h>

#include "util/BookmarkSaveAction.h"

namespace {
constexpr size_t BUDGET = 45000;
constexpr size_t CAP = persist::SD_READ_TRUNCATION_CAP;  // 50000
}  // namespace

TEST(BookmarkSaveAction, UnderBudgetAlwaysWrites) {
  EXPECT_EQ(bookmarkSaveAction(0, 0, BUDGET, CAP), BookmarkSaveAction::Write);
  EXPECT_EQ(bookmarkSaveAction(13200, 0, BUDGET, CAP), BookmarkSaveAction::Write);
  EXPECT_EQ(bookmarkSaveAction(13200, 47400, BUDGET, CAP), BookmarkSaveAction::Write)
      << "what is already on disk cannot make an under-budget document unwritable";
}

TEST(BookmarkSaveAction, OverBudgetAndGrowingRefuses) {
  EXPECT_EQ(bookmarkSaveAction(45001, 0, BUDGET, CAP), BookmarkSaveAction::RefuseTooLarge);
  EXPECT_EQ(bookmarkSaveAction(47400, 47200, BUDGET, CAP), BookmarkSaveAction::RefuseTooLarge);
  EXPECT_EQ(bookmarkSaveAction(47400, 47400, BUDGET, CAP), BookmarkSaveAction::RefuseTooLarge)
      << "equal is not shrinking";
}

TEST(BookmarkSaveAction, OverBudgetAndShrinkingWrites) {
  // The pass-0 blocker, pinned: a 230-record legacy file minus one entry.
  EXPECT_EQ(bookmarkSaveAction(47200, 47400, BUDGET, CAP), BookmarkSaveAction::Write)
      << "an inherited over-budget file must be deletable back under the budget";
  EXPECT_EQ(bookmarkSaveAction(45001, 45002, BUDGET, CAP), BookmarkSaveAction::Write);
}

TEST(BookmarkSaveAction, OverTheReadCapRefusesEvenWhenShrinking) {
  EXPECT_EQ(bookmarkSaveAction(50001, 60000, BUDGET, CAP), BookmarkSaveAction::RefuseTooLarge)
      << "a file that reads back truncated must never be written, shrinking or not";
}

TEST(BookmarkSaveAction, NoFileYetRefusesAnythingOverBudget) {
  EXPECT_EQ(bookmarkSaveAction(45001, 0, BUDGET, CAP), BookmarkSaveAction::RefuseTooLarge)
      << "bytesOnDisk == 0 means nothing to shrink from";
}

TEST(BookmarkSaveAction, Boundaries) {
  EXPECT_EQ(bookmarkSaveAction(BUDGET, 0, BUDGET, CAP), BookmarkSaveAction::Write)
      << "the budget itself must still fit";
  // Exactly at the read cap writes ONLY on the shrink arm, which needs a file
  // larger than the cap -- and such a file reads back truncated, loads Failed
  // and latches saving off, so save() is never reached with one. Pinned as a
  // boundary, not as a reachable state.
  EXPECT_EQ(bookmarkSaveAction(CAP, 0, BUDGET, CAP), BookmarkSaveAction::RefuseTooLarge);
  EXPECT_EQ(bookmarkSaveAction(CAP, CAP + 1, BUDGET, CAP), BookmarkSaveAction::Write);
  EXPECT_EQ(bookmarkSaveAction(CAP + 1, CAP + 2, BUDGET, CAP), BookmarkSaveAction::RefuseTooLarge);
}
