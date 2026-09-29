#include <gtest/gtest.h>

#include <initializer_list>

#include "Input/StableLevel.h"

namespace {

using input::StableLevel;

bool feed(StableLevel& level, const std::initializer_list<bool> samples) {
  for (const bool sample : samples) level.update(sample);
  return level.stable();
}

TEST(StableLevel, BeginSeedsTheStableLevel) {
  StableLevel level;
  level.begin(true);
  EXPECT_TRUE(level.stable());
}

TEST(StableLevel, OneDifferingSampleIsRejected) {
  StableLevel level;
  level.begin(false);
  EXPECT_FALSE(level.update(true));
  EXPECT_FALSE(level.update(false));
  EXPECT_FALSE(level.stable());
}

TEST(StableLevel, TwoAgreeingSamplesChangeIt) {
  StableLevel level;
  level.begin(false);
  EXPECT_FALSE(level.update(true));
  EXPECT_TRUE(level.update(true));
}

// Bench, #185: on plug-in GPIO21 floats for ~280 ms, which the pull-down reads
// LOW, before VBUS drives it HIGH.
TEST(StableLevel, PlugSequenceSettlesOnlyAfterTwoHighs) {
  StableLevel level;
  level.begin(false);
  EXPECT_FALSE(feed(level, {false, false, false, true}));
  EXPECT_TRUE(level.update(true));
}

// On unplug VBUS decays for ~280 ms with the pin still HIGH.
TEST(StableLevel, UnplugSequenceSettlesOnlyAfterTwoLows) {
  StableLevel level;
  level.begin(true);
  EXPECT_TRUE(feed(level, {true, true, false}));
  EXPECT_FALSE(level.update(false));
}

TEST(StableLevel, AlternatingSamplesNeverChangeIt) {
  StableLevel level;
  level.begin(false);
  EXPECT_FALSE(feed(level, {true, false, true, false, true, false}));
}

}  // namespace
