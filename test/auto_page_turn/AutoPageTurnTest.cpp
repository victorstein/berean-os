#include <gtest/gtest.h>

#include <iterator>
#include <limits>

#include "activities/reader/AutoPageTurn.h"

TEST(AutoPageTurn, StartsInactive) {
  const AutoPageTurn turn;
  EXPECT_FALSE(turn.active());
}

TEST(AutoPageTurn, OptionZeroIsOff) {
  AutoPageTurn turn;
  ASSERT_TRUE(turn.start(3));
  EXPECT_FALSE(turn.start(0));
  EXPECT_FALSE(turn.active());
}

TEST(AutoPageTurn, OutOfRangeOptionIsOff) {
  AutoPageTurn turn;
  ASSERT_TRUE(turn.start(3));
  EXPECT_FALSE(turn.start(static_cast<uint8_t>(std::size(AutoPageTurn::RATES))));
  EXPECT_FALSE(turn.active());
}

TEST(AutoPageTurn, EachOptionTurnsAtItsRate) {
  constexpr unsigned long EXPECTED_PAGES_PER_MINUTE[] = {0, 1, 3, 6, 12};
  for (uint8_t option = 1; option < std::size(AutoPageTurn::RATES); ++option) {
    AutoPageTurn turn;
    ASSERT_TRUE(turn.start(option));
    EXPECT_TRUE(turn.active());
    EXPECT_EQ(turn.pagesPerMinute(), EXPECTED_PAGES_PER_MINUTE[option]) << "option " << int(option);
  }
}

TEST(AutoPageTurn, EachOptionIsDueExactlyAtItsInterval) {
  constexpr unsigned long EXPECTED_INTERVAL_MS[] = {0, 60000, 20000, 10000, 5000};
  constexpr unsigned long LAST_TURN = 1000;
  for (uint8_t option = 1; option < std::size(AutoPageTurn::RATES); ++option) {
    AutoPageTurn turn;
    ASSERT_TRUE(turn.start(option));
    const unsigned long interval = EXPECTED_INTERVAL_MS[option];
    EXPECT_FALSE(turn.due(LAST_TURN + interval - 1, LAST_TURN)) << "option " << int(option);
    EXPECT_TRUE(turn.due(LAST_TURN + interval, LAST_TURN)) << "option " << int(option);
  }
}

TEST(AutoPageTurn, DueExactlyAtTheInterval) {
  AutoPageTurn turn;
  ASSERT_TRUE(turn.start(4));  // 12 pages a minute: one every 5000 ms
  EXPECT_FALSE(turn.due(1000 + 4999, 1000));
  EXPECT_TRUE(turn.due(1000 + 5000, 1000));
}

TEST(AutoPageTurn, DueAcrossTheClockWrap) {
  // Derived from the host's own unsigned long, which is 64-bit here but 32-bit
  // on the device: a literal 0xFFFFFFFF would never wrap on the host.
  constexpr unsigned long MAX = std::numeric_limits<unsigned long>::max();
  constexpr unsigned long INTERVAL = 5000;
  AutoPageTurn turn;
  ASSERT_TRUE(turn.start(4));
  const unsigned long lastTurn = MAX - 99;
  EXPECT_FALSE(turn.due(INTERVAL - 101, lastTurn));
  EXPECT_TRUE(turn.due(INTERVAL - 100, lastTurn));
}

TEST(AutoPageTurn, StopDeactivates) {
  AutoPageTurn turn;
  ASSERT_TRUE(turn.start(2));
  turn.stop();
  EXPECT_FALSE(turn.active());
}
