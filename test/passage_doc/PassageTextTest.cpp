#include <gtest/gtest.h>

#include <cstdlib>
#include <string>
#include <type_traits>
#include <utility>

#include "StudyStore/PassageText.h"

namespace {

int allocations = 0;
void* countingAllocate(const size_t bytes) {
  ++allocations;
  return std::malloc(bytes);
}
void countingRelease(void* block) { std::free(block); }
const study::TextAllocator COUNTING{countingAllocate, countingRelease};

void* failingAllocate(size_t) { return nullptr; }
void failingRelease(void*) {}
const study::TextAllocator FAILING{failingAllocate, failingRelease};

TEST(PassageText, IsMoveOnly) {
  EXPECT_FALSE(std::is_copy_constructible_v<study::PassageText>);
  EXPECT_FALSE(std::is_copy_assignable_v<study::PassageText>);
  EXPECT_TRUE(std::is_nothrow_move_constructible_v<study::PassageText>)
      << "std::vector growth must move, never copy";
}

TEST(PassageText, AssignsAndReadsBackWithoutACap) {
  const std::string long_(5000, 'x');
  study::PassageText text;
  ASSERT_TRUE(text.assign(long_));
  EXPECT_EQ(text.size(), 5000u);
  EXPECT_EQ(text.view(), long_);
  EXPECT_STREQ(text.c_str(), long_.c_str());
}

TEST(PassageText, AllocatesFromItsAllocator) {
  allocations = 0;
  study::PassageText text(COUNTING);
  ASSERT_TRUE(text.assign("Jesús lloró."));
  EXPECT_EQ(allocations, 1);
  EXPECT_TRUE(text.allocator() == COUNTING);
}

TEST(PassageText, AFailedAssignKeepsTheOldText) {
  study::PassageText text;
  ASSERT_TRUE(text.assign("antes"));
  study::PassageText failing(FAILING);
  EXPECT_FALSE(failing.assign("nada"));
  EXPECT_TRUE(failing.empty());
  EXPECT_EQ(text.view(), "antes");
}

TEST(PassageText, CopyFromReportsAnAllocationFailure) {
  study::PassageText source;
  ASSERT_TRUE(source.assign("Ustedes, los que tratan"));
  study::PassageText target(FAILING);
  EXPECT_FALSE(target.copyFrom(source)) << "a copy that cannot allocate must say so, never come back empty";
}

TEST(PassageText, CopyFromUsesTheTargetsAllocator) {
  allocations = 0;
  study::PassageText source;
  ASSERT_TRUE(source.assign("Ustedes"));
  study::PassageText target(COUNTING);
  ASSERT_TRUE(target.copyFrom(source));
  EXPECT_EQ(allocations, 1);
  EXPECT_EQ(target, source);
}

TEST(PassageText, MovingLeavesTheSourceEmpty) {
  study::PassageText source;
  ASSERT_TRUE(source.assign("texto"));
  study::PassageText moved(std::move(source));
  EXPECT_EQ(moved.view(), "texto");
  EXPECT_TRUE(source.empty());  // NOLINT(bugprone-use-after-move): the moved-from state is the contract
}

TEST(PassageText, AssigningEmptyClears) {
  study::PassageText text;
  ASSERT_TRUE(text.assign("algo"));
  ASSERT_TRUE(text.assign(""));
  EXPECT_TRUE(text.empty());
  EXPECT_STREQ(text.c_str(), "");
}

}  // namespace
