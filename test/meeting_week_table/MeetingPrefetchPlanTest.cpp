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

MeetingPrefetchConditions conditionsFor(const IsoWeek& current) {
  MeetingPrefetchConditions conditions;
  conditions.enabled = true;
  conditions.clockSynced = true;
  conditions.currentWeek = current;
  return conditions;
}

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
  MeetingPrefetchConditions conditions = conditionsFor(week(2026, 38));
  conditions.enabled = false;
  EXPECT_FALSE(meetingWeekToPrefetch(conditions, cache, out));
}

TEST(MeetingWeekToPrefetch, DoesNothingWithoutAUsableCurrentWeek) {
  MeetingWeekTable cache;
  IsoWeek out;
  EXPECT_FALSE(meetingWeekToPrefetch(conditionsFor(week(0, 0)), cache, out));
}

TEST(MeetingWeekToPrefetch, ResolvesTheCurrentWeekFirstWhenItIsMissing) {
  MeetingWeekTable cache;
  cache.set("2026-39", "202607", "202609");
  IsoWeek out;
  ASSERT_TRUE(meetingWeekToPrefetch(conditionsFor(week(2026, 38)), cache, out));
  EXPECT_EQ(key(out), "2026-38");
}

TEST(MeetingWeekToPrefetch, ResolvesNextWeekOnceTheCurrentOneIsHeld) {
  MeetingWeekTable cache;
  cache.set("2026-38", "202607", "202609");
  IsoWeek out;
  ASSERT_TRUE(meetingWeekToPrefetch(conditionsFor(week(2026, 38)), cache, out));
  EXPECT_EQ(key(out), "2026-39");
}

TEST(MeetingWeekToPrefetch, ResolvesNextWeekAcrossTheYearBoundary) {
  MeetingWeekTable cache;
  cache.set("2026-53", "202610", "202611");
  IsoWeek out;
  ASSERT_TRUE(meetingWeekToPrefetch(conditionsFor(week(2026, 53)), cache, out));
  EXPECT_EQ(key(out), "2027-01");
}

TEST(MeetingWeekToPrefetch, DoesNothingWhenBothWeeksAreHeld) {
  MeetingWeekTable cache;
  cache.set("2026-38", "202607", "202609");
  cache.set("2026-39", "202607", "202609");
  IsoWeek out;
  EXPECT_FALSE(meetingWeekToPrefetch(conditionsFor(week(2026, 38)), cache, out));
}

TEST(MeetingWeekToPrefetch, CountsAMemorialWeekWithOnlyAWatchtowerAsHeld) {
  MeetingWeekTable cache;
  cache.set("2026-38", "202607", "202609");
  cache.set("2026-39", "202607", "");
  IsoWeek out;
  EXPECT_FALSE(meetingWeekToPrefetch(conditionsFor(week(2026, 38)), cache, out));
}

TEST(MeetingWeekToPrefetch, TreatsAnEntryNamingNoIssueAsMissing) {
  // A resolve that found nothing on the page still records the week; that
  // answer must not stop a later prefetch from asking again.
  MeetingWeekTable cache;
  cache.set("2026-38", "202607", "202609");
  cache.set("2026-39", "", "");
  IsoWeek out;
  ASSERT_TRUE(meetingWeekToPrefetch(conditionsFor(week(2026, 38)), cache, out));
  EXPECT_EQ(key(out), "2026-39");
}

TEST(MeetingWeekToPrefetch, DoesNothingBeforeTheClockHasBeenSynced) {
  MeetingWeekTable cache;
  MeetingPrefetchConditions conditions = conditionsFor(week(2026, 38));
  conditions.clockSynced = false;
  IsoWeek out;
  EXPECT_FALSE(meetingWeekToPrefetch(conditions, cache, out));
}

TEST(MeetingWeekToPrefetch, IgnoresAClockThatLostItsTime) {
  // An RTC that reset reads 2000-01-01, which is ISO week 1999/52. A synced
  // flag set long ago does not make that date true.
  MeetingWeekTable cache;
  IsoWeek out;
  EXPECT_FALSE(meetingWeekToPrefetch(conditionsFor(week(1999, 52)), cache, out));
  EXPECT_FALSE(meetingWeekToPrefetch(conditionsFor(week(2025, 52)), cache, out));
}

TEST(MeetingWeekToPrefetch, DoesNotRetryAWeekAlreadyAttemptedThisBoot) {
  MeetingWeekTable cache;
  MeetingPrefetchConditions conditions = conditionsFor(week(2026, 38));
  conditions.attemptedThisBoot = "2026-38";
  IsoWeek out;
  // The failed current week is not retried, and next week is not asked for in
  // its place: a lookup that failed once this boot will most likely fail again.
  EXPECT_FALSE(meetingWeekToPrefetch(conditions, cache, out));
}

TEST(MeetingWeekToPrefetch, MovesOnToNextWeekOnceTheAttemptedWeekIsHeld) {
  MeetingWeekTable cache;
  cache.set("2026-38", "202607", "202609");
  MeetingPrefetchConditions conditions = conditionsFor(week(2026, 38));
  conditions.attemptedThisBoot = "2026-38";
  IsoWeek out;
  ASSERT_TRUE(meetingWeekToPrefetch(conditions, cache, out));
  EXPECT_EQ(key(out), "2026-39");

  conditions.attemptedThisBoot = "2026-39";
  EXPECT_FALSE(meetingWeekToPrefetch(conditions, cache, out));
}
