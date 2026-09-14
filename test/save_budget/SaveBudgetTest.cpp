#include <gtest/gtest.h>

#include <string>

#include "SaveBudget.h"

// SDCardManager::readFile caps reads at 50,000 bytes and returns the truncated
// string with no error. A document that saves larger than the budget therefore
// reads back mid-token, fails to parse, initialises the store empty, and the
// next save overwrites the real file with {}. The only defence is measuring
// before writing and refusing, so these bounds are load bearing rather than
// decorative.

TEST(SaveBudget, AcceptsDocumentsUnderTheBudget) {
  EXPECT_TRUE(persist::fitsBudget(1000, persist::DEFAULT_SAVE_BUDGET));
}

TEST(SaveBudget, AcceptsExactlyTheBudget) {
  EXPECT_TRUE(persist::fitsBudget(persist::DEFAULT_SAVE_BUDGET, persist::DEFAULT_SAVE_BUDGET));
}

TEST(SaveBudget, RefusesOneByteOver) {
  EXPECT_FALSE(persist::fitsBudget(persist::DEFAULT_SAVE_BUDGET + 1, persist::DEFAULT_SAVE_BUDGET));
}

// The budget must stay clear of the read cap, not merely equal it: the cap is
// where truncation begins, so a document sized exactly at it is already at risk.
TEST(SaveBudget, DefaultBudgetLeavesHeadroomUnderTheReadCap) {
  EXPECT_LT(persist::DEFAULT_SAVE_BUDGET, persist::SD_READ_TRUNCATION_CAP);
}

TEST(SaveBudget, HonoursAPerStoreBudget) {
  constexpr size_t tight = 128;
  EXPECT_TRUE(persist::fitsBudget(128, tight));
  EXPECT_FALSE(persist::fitsBudget(129, tight));
}
