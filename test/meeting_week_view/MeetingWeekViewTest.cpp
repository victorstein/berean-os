#include <gtest/gtest.h>

#include <array>
#include <cstring>

#include "activities/network/MeetingWeekView.h"

namespace {

CivilDate civil(const uint16_t year, const uint8_t month, const uint8_t day) {
  CivilDate date;
  date.year = year;
  date.month = month;
  date.day = day;
  return date;
}

void expectDays(const WeekStrip& strip, const std::array<int, 7>& days) {
  for (size_t i = 0; i < days.size(); ++i) EXPECT_EQ(strip[i].day, days[i]) << i;
}

}  // namespace

TEST(WeekStrip, NumbersTheDaysFromMonday) {
  const WeekStrip strip = buildWeekStrip(civil(2026, 9, 21), nullptr, 0, 0);
  expectDays(strip, {21, 22, 23, 24, 25, 26, 27});
  for (const WeekStripCell& cell : strip) {
    EXPECT_FALSE(cell.today);
    EXPECT_FALSE(cell.meeting);
  }
}

TEST(WeekStrip, CrossesAMonthAndAYear) {
  expectDays(buildWeekStrip(civil(2026, 9, 28), nullptr, 0, 0), {28, 29, 30, 1, 2, 3, 4});
  expectDays(buildWeekStrip(civil(2026, 12, 28), nullptr, 0, 0), {28, 29, 30, 31, 1, 2, 3});
}

TEST(WeekStrip, MarksTodayOnlyInsideTheWeek) {
  const CivilDate wednesday = civil(2026, 9, 23);
  const WeekStrip strip = buildWeekStrip(civil(2026, 9, 21), &wednesday, 0, 0);
  for (size_t i = 0; i < strip.size(); ++i) EXPECT_EQ(strip[i].today, i == 2) << i;

  const CivilDate nextMonday = civil(2026, 9, 28);
  for (const WeekStripCell& cell : buildWeekStrip(civil(2026, 9, 21), &nextMonday, 0, 0)) {
    EXPECT_FALSE(cell.today);
  }
}

TEST(WeekStrip, DotsTheMeetingDays) {
  const WeekStrip strip = buildWeekStrip(civil(2026, 9, 21), nullptr, 3, 7);
  for (size_t i = 0; i < strip.size(); ++i) EXPECT_EQ(strip[i].meeting, i == 2 || i == 6) << i;
}

TEST(WeekStrip, UnsetSameDayAndOutOfRangeSettings) {
  for (const WeekStripCell& cell : buildWeekStrip(civil(2026, 9, 21), nullptr, 0, 0)) EXPECT_FALSE(cell.meeting);

  const WeekStrip sameDay = buildWeekStrip(civil(2026, 9, 21), nullptr, 4, 4);
  int dots = 0;
  for (const WeekStripCell& cell : sameDay) dots += cell.meeting ? 1 : 0;
  EXPECT_EQ(dots, 1);
  EXPECT_TRUE(sameDay[3].meeting);

  for (const WeekStripCell& cell : buildWeekStrip(civil(2026, 9, 21), nullptr, 8, 200)) EXPECT_FALSE(cell.meeting);
}
