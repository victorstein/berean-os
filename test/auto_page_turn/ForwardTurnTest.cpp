#include <gtest/gtest.h>

#include "activities/reader/PageTurn.h"

TEST(ForwardTurn, OnlyAForwardTurnFromTheLastBuiltPageLeavesTheDocument) {
  EXPECT_FALSE(forwardTurnLeavesDocument(0, 5, false));
  EXPECT_FALSE(forwardTurnLeavesDocument(3, 5, false));
  EXPECT_TRUE(forwardTurnLeavesDocument(4, 5, false));
  EXPECT_TRUE(forwardTurnLeavesDocument(0, 1, false));
}

// While the section is still being laid out the reader stays in it and waits for
// more pages, so "the last page so far" is not the chapter's end.
TEST(ForwardTurn, DoesNotLeaveWhileTheSectionIsStillBuilding) { EXPECT_FALSE(forwardTurnLeavesDocument(4, 5, true)); }
