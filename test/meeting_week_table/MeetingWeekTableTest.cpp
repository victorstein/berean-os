#include <gtest/gtest.h>

#include <string>

#include "network/MeetingWeekTable.h"

namespace {

IsoWeek week(const uint16_t year, const uint8_t number) {
  IsoWeek w;
  w.year = year;
  w.week = number;
  return w;
}

}  // namespace

TEST(MeetingWeekKey, PadsTheWeekSoOrderIsChronological) {
  EXPECT_EQ(meetingWeekKey(week(2026, 38)), "2026-38");
  EXPECT_EQ(meetingWeekKey(week(2026, 1)), "2026-01");
  // The padding is the whole point: week 9 must sort before week 10, which it
  // only does when both are two digits.
  EXPECT_LT(meetingWeekKey(week(2026, 9)), meetingWeekKey(week(2026, 10)));
  EXPECT_LT(meetingWeekKey(week(2025, 52)), meetingWeekKey(week(2026, 1)));
}

TEST(MeetingWeekKey, RejectsAWeekTheClockCouldNotHaveProduced) {
  EXPECT_TRUE(meetingWeekKey(week(0, 38)).empty());
  EXPECT_TRUE(meetingWeekKey(week(2026, 0)).empty());
  EXPECT_TRUE(meetingWeekKey(week(2026, 54)).empty());
}

TEST(MeetingWeekTable, FindsWhatWasStored) {
  MeetingWeekTable table;
  table.set("2026-38", "202607", "202609");

  const MeetingWeekEntry* found = table.find("2026-38");
  ASSERT_NE(found, nullptr);
  EXPECT_EQ(found->watchtower, "202607");
  EXPECT_EQ(found->workbook, "202609");
  EXPECT_EQ(table.find("2026-37"), nullptr);
}

TEST(MeetingWeekTable, KeepsAWeekWithNoWorkbook) {
  // The Memorial week carries a Watchtower and no workbook, every year.
  MeetingWeekTable table;
  table.set("2026-14", "202602", "");

  const MeetingWeekEntry* found = table.find("2026-14");
  ASSERT_NE(found, nullptr);
  EXPECT_EQ(found->watchtower, "202602");
  EXPECT_TRUE(found->workbook.empty());
}

TEST(MeetingWeekTable, ReplacesRatherThanDuplicatingAWeek) {
  MeetingWeekTable table;
  table.set("2026-38", "202607", "202609");
  table.set("2026-38", "202608", "202610");

  EXPECT_EQ(table.entries().size(), 1u);
  EXPECT_EQ(table.find("2026-38")->watchtower, "202608");
}

TEST(MeetingWeekTable, IgnoresAnEmptyKey) {
  MeetingWeekTable table;
  table.set("", "202607", "202609");
  EXPECT_TRUE(table.entries().empty());
}

TEST(MeetingWeekTable, NewestIsTheLatestWeekNotTheLastInserted) {
  MeetingWeekTable table;
  table.set("2026-05", "a", "b");
  table.set("2026-38", "c", "d");
  table.set("2026-12", "e", "f");

  ASSERT_NE(table.newest(), nullptr);
  EXPECT_EQ(table.newest()->key, "2026-38");
}

TEST(MeetingWeekTable, NewestIsNullWhenEmpty) {
  const MeetingWeekTable table;
  EXPECT_EQ(table.newest(), nullptr);
}

TEST(MeetingWeekTable, PruneKeepsTheMostRecentWeeks) {
  MeetingWeekTable table;
  for (uint8_t i = 1; i <= MeetingWeekTable::MAX_WEEKS + 5; ++i) {
    table.set(meetingWeekKey(week(2026, i)), "w", "m");
  }
  table.prune();

  EXPECT_EQ(table.entries().size(), MeetingWeekTable::MAX_WEEKS);
  EXPECT_NE(table.find(meetingWeekKey(week(2026, MeetingWeekTable::MAX_WEEKS + 5))), nullptr);
  EXPECT_EQ(table.find(meetingWeekKey(week(2026, 1))), nullptr);
}

TEST(MeetingWeekTable, PruneAcrossAYearBoundaryKeepsTheNewerYear) {
  MeetingWeekTable table;
  for (uint8_t i = 40; i <= 52; ++i) table.set(meetingWeekKey(week(2025, i)), "w", "m");
  table.set(meetingWeekKey(week(2026, 1)), "new", "new");
  table.prune();

  EXPECT_EQ(table.entries().size(), MeetingWeekTable::MAX_WEEKS);
  EXPECT_NE(table.find("2026-01"), nullptr);
  EXPECT_EQ(table.find("2025-40"), nullptr);
}

TEST(MeetingWeekTable, PruneLeavesASmallTableAlone) {
  MeetingWeekTable table;
  table.set("2026-38", "w", "m");
  table.prune();
  EXPECT_EQ(table.entries().size(), 1u);
}

TEST(MeetingWeekTable, ClearEmptiesIt) {
  MeetingWeekTable table;
  table.set("2026-38", "w", "m");
  table.clear();
  EXPECT_TRUE(table.entries().empty());
  EXPECT_EQ(table.newest(), nullptr);
}
