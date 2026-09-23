#include <gtest/gtest.h>

#include "network/MeetingPrefetchPlan.h"

namespace {

IsoWeek week(const uint16_t year, const uint8_t number) {
  IsoWeek w;
  w.year = year;
  w.week = number;
  return w;
}

std::string key(const IsoWeek& w) { return meetingWeekKey(w); }

}  // namespace

TEST(IsoWeekAfter, StepsWithinAYear) {
  EXPECT_EQ(key(isoWeekAfter(week(2026, 38))), "2026-39");
  EXPECT_EQ(key(isoWeekAfter(week(2026, 9))), "2026-10");
}

TEST(IsoWeekAfter, RollsOverAFiftyTwoWeekYear) {
  // 2025 has 52 ISO weeks: 2025-12-28 is a Sunday in week 52.
  EXPECT_EQ(key(isoWeekAfter(week(2025, 52))), "2026-01");
}

TEST(IsoWeekAfter, KeepsWeekFiftyThreeInALongYear) {
  // 2026 starts on a Thursday, so it is one of the 53-week years. Rolling
  // over at 52 would skip a real meeting week.
  EXPECT_EQ(key(isoWeekAfter(week(2026, 52))), "2026-53");
  EXPECT_EQ(key(isoWeekAfter(week(2026, 53))), "2027-01");
  // 2020 is the leap-year case: it starts on a Wednesday.
  EXPECT_EQ(key(isoWeekAfter(week(2020, 52))), "2020-53");
}

TEST(IsoWeekAfter, RejectsAWeekThatCannotExist) {
  EXPECT_TRUE(key(isoWeekAfter(week(0, 10))).empty());
  EXPECT_TRUE(key(isoWeekAfter(week(2026, 0))).empty());
  EXPECT_TRUE(key(isoWeekAfter(week(2025, 53))).empty());
}

TEST(MeetingWeekToPrefetch, DoesNothingWhenDisabled) {
  MeetingWeekTable cache;
  IsoWeek out;
  EXPECT_FALSE(meetingWeekToPrefetch(false, week(2026, 38), cache, out));
}

TEST(MeetingWeekToPrefetch, DoesNothingWithoutAUsableCurrentWeek) {
  MeetingWeekTable cache;
  IsoWeek out;
  EXPECT_FALSE(meetingWeekToPrefetch(true, week(0, 0), cache, out));
}

TEST(MeetingWeekToPrefetch, ResolvesTheCurrentWeekFirstWhenItIsMissing) {
  MeetingWeekTable cache;
  cache.set("2026-39", "202607", "202609");
  IsoWeek out;
  ASSERT_TRUE(meetingWeekToPrefetch(true, week(2026, 38), cache, out));
  EXPECT_EQ(key(out), "2026-38");
}

TEST(MeetingWeekToPrefetch, ResolvesNextWeekOnceTheCurrentOneIsHeld) {
  MeetingWeekTable cache;
  cache.set("2026-38", "202607", "202609");
  IsoWeek out;
  ASSERT_TRUE(meetingWeekToPrefetch(true, week(2026, 38), cache, out));
  EXPECT_EQ(key(out), "2026-39");
}

TEST(MeetingWeekToPrefetch, ResolvesNextWeekAcrossTheYearBoundary) {
  MeetingWeekTable cache;
  cache.set("2026-53", "202610", "202611");
  IsoWeek out;
  ASSERT_TRUE(meetingWeekToPrefetch(true, week(2026, 53), cache, out));
  EXPECT_EQ(key(out), "2027-01");
}

TEST(MeetingWeekToPrefetch, DoesNothingWhenBothWeeksAreHeld) {
  MeetingWeekTable cache;
  cache.set("2026-38", "202607", "202609");
  cache.set("2026-39", "202607", "202609");
  IsoWeek out;
  EXPECT_FALSE(meetingWeekToPrefetch(true, week(2026, 38), cache, out));
}

TEST(MeetingWeekToPrefetch, CountsAMemorialWeekWithOnlyAWatchtowerAsHeld) {
  MeetingWeekTable cache;
  cache.set("2026-38", "202607", "202609");
  cache.set("2026-39", "202607", "");
  IsoWeek out;
  EXPECT_FALSE(meetingWeekToPrefetch(true, week(2026, 38), cache, out));
}

TEST(MeetingWeekToPrefetch, TreatsAnEntryNamingNoIssueAsMissing) {
  // A resolve that found nothing on the page still records the week; that
  // answer must not stop a later prefetch from asking again.
  MeetingWeekTable cache;
  cache.set("2026-38", "202607", "202609");
  cache.set("2026-39", "", "");
  IsoWeek out;
  ASSERT_TRUE(meetingWeekToPrefetch(true, week(2026, 38), cache, out));
  EXPECT_EQ(key(out), "2026-39");
}
