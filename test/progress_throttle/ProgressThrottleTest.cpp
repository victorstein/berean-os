#include <gtest/gtest.h>

#include "util/ProgressThrottle.h"

TEST(ProgressThrottle, TheFirstReportRepaints) {
  ProgressThrottle throttle;
  EXPECT_TRUE(throttle.shouldRepaint(0, 100, 1000));
}

TEST(ProgressThrottle, WaitsForFivePercentOrFiveSeconds) {
  ProgressThrottle throttle;
  ASSERT_TRUE(throttle.shouldRepaint(10, 100, 1000));
  EXPECT_FALSE(throttle.shouldRepaint(14, 100, 5999));
  EXPECT_TRUE(throttle.shouldRepaint(15, 100, 5999));
  EXPECT_FALSE(throttle.shouldRepaint(16, 100, 10998));
  EXPECT_TRUE(throttle.shouldRepaint(16, 100, 10999));
}

TEST(ProgressThrottle, TheLastChunkAlwaysRepaints) {
  ProgressThrottle throttle;
  ASSERT_TRUE(throttle.shouldRepaint(99, 100, 1000));
  EXPECT_TRUE(throttle.shouldRepaint(100, 100, 1001));
}

TEST(ProgressThrottle, ResetMakesTheNextReportRepaint) {
  ProgressThrottle throttle;
  ASSERT_TRUE(throttle.shouldRepaint(50, 100, 1000));
  ASSERT_FALSE(throttle.shouldRepaint(51, 100, 1001));
  throttle.reset();
  EXPECT_TRUE(throttle.shouldRepaint(0, 100, 1002));
}

TEST(ProgressThrottle, AnUnknownTotalIsZeroPercent) {
  EXPECT_EQ(ProgressThrottle::percentOf(12345, 0), 0);
  EXPECT_EQ(ProgressThrottle::percentOf(50, 200), 25);
}

TEST(ProgressThrottle, ElapsedTimeWrapsAtThirtyTwoBitsAsMillisDoes) {
  ProgressThrottle throttle;
  ASSERT_TRUE(throttle.shouldRepaint(0, 100, 0xFFFFF000u));
  EXPECT_FALSE(throttle.shouldRepaint(1, 100, 0x100u));
  EXPECT_TRUE(throttle.shouldRepaint(1, 100, 0x100u + 648u));
}
