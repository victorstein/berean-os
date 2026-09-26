// Host coverage for HighlightFile's save budget check.
//
// HighlightFile.cpp itself is not host-built yet. test/stubs now has an
// Arduino.h and an in-memory Storage fake that it could be built against, as
// test/storage_io/ does for TagPaletteFile.
//
// The load-side decision this suite used to cover moved to
// test/temp_adoption/ when the rule became shared with PersistableStore.

#include <gtest/gtest.h>

#include "util/HighlightFileAction.h"

TEST(HighlightSaveAction, WritesWhenAtOrUnderBudget) {
  EXPECT_EQ(highlightSaveAction(0, 45000), HighlightSaveAction::Write);
  EXPECT_EQ(highlightSaveAction(44999, 45000), HighlightSaveAction::Write);
  EXPECT_EQ(highlightSaveAction(45000, 45000), HighlightSaveAction::Write) << "the budget itself must still fit";
}

TEST(HighlightSaveAction, RefusesOneByteOverBudget) {
  EXPECT_EQ(highlightSaveAction(45001, 45000), HighlightSaveAction::RefuseTooLarge);
}

TEST(HighlightSaveAction, RefusesTheDocumentedWorstCase) {
  // HighlightDocTest.WorstCaseDocumentStaysUnderTheSaveBudget records the
  // real worst case as > 50000 bytes; this is the guard that must catch it.
  EXPECT_EQ(highlightSaveAction(90733, 45000), HighlightSaveAction::RefuseTooLarge);
}
