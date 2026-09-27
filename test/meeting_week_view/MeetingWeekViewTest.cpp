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

namespace {

constexpr const char* EN_SAME_MONTH = "Week of %u–%u %s";
constexpr const char* EN_TWO_MONTHS = "Week of %u %s – %u %s";
constexpr const char* ES_SAME_MONTH = "Semana del %u al %u de %s";
constexpr const char* ES_TWO_MONTHS = "Semana del %u de %s al %u de %s";
constexpr const char* EN_MONTHS =
    "January February March April May June July August September October November December";
constexpr const char* ES_MONTHS =
    "enero febrero marzo abril mayo junio julio agosto septiembre octubre noviembre diciembre";

}  // namespace

TEST(FormatWeekRange, SameMonthTakesOnlyThatMonthsName) {
  char out[96];
  ASSERT_TRUE(formatWeekRange(civil(2026, 9, 21), EN_SAME_MONTH, EN_TWO_MONTHS, EN_MONTHS, out, sizeof(out)));
  EXPECT_STREQ(out, "Week of 21–27 September");
  // The month is a view into the middle of the list; unterminated, %s would run on.
  EXPECT_EQ(strstr(out, "October"), nullptr);

  ASSERT_TRUE(formatWeekRange(civil(2026, 9, 21), ES_SAME_MONTH, ES_TWO_MONTHS, ES_MONTHS, out, sizeof(out)));
  EXPECT_STREQ(out, "Semana del 21 al 27 de septiembre");
}

TEST(FormatWeekRange, TwoMonthsAndTwoYears) {
  char out[96];
  ASSERT_TRUE(formatWeekRange(civil(2026, 9, 28), EN_SAME_MONTH, EN_TWO_MONTHS, EN_MONTHS, out, sizeof(out)));
  EXPECT_STREQ(out, "Week of 28 September – 4 October");
  ASSERT_TRUE(formatWeekRange(civil(2026, 9, 28), ES_SAME_MONTH, ES_TWO_MONTHS, ES_MONTHS, out, sizeof(out)));
  EXPECT_STREQ(out, "Semana del 28 de septiembre al 4 de octubre");
  ASSERT_TRUE(formatWeekRange(civil(2026, 12, 28), ES_SAME_MONTH, ES_TWO_MONTHS, ES_MONTHS, out, sizeof(out)));
  EXPECT_STREQ(out, "Semana del 28 de diciembre al 3 de enero");
}

TEST(FormatWeekRange, TooSmallBufferWritesNothing) {
  char out[8] = "junk";
  EXPECT_FALSE(formatWeekRange(civil(2026, 9, 21), EN_SAME_MONTH, EN_TWO_MONTHS, EN_MONTHS, out, sizeof(out)));
  EXPECT_STREQ(out, "");
}

TEST(FormatWeekRange, MissingMonthNameFails) {
  char out[96] = "junk";
  EXPECT_FALSE(formatWeekRange(civil(2026, 9, 21), EN_SAME_MONTH, EN_TWO_MONTHS, "January", out, sizeof(out)));
  EXPECT_STREQ(out, "");
}

TEST(CopyWordAt, CopiesOneTerminatedWord) {
  char out[8];
  ASSERT_TRUE(copyWordAt("L M X J V S D", 2, out, sizeof(out)));
  EXPECT_STREQ(out, "X");
  ASSERT_TRUE(copyWordAt("L M X J V S D", 6, out, sizeof(out)));
  EXPECT_STREQ(out, "D");
}

TEST(CopyWordAt, FailsPastTheEndOrWhenTooSmall) {
  char out[4] = "x";
  EXPECT_FALSE(copyWordAt("L M X J V S D", 7, out, sizeof(out)));
  EXPECT_STREQ(out, "");
  EXPECT_FALSE(copyWordAt(ES_MONTHS, 8, out, sizeof(out)));
  EXPECT_STREQ(out, "");
}

TEST(CopyInitial, CopiesTheFirstCodePoint) {
  char out[8];
  ASSERT_TRUE(copyInitial("Monday", out, sizeof(out)));
  EXPECT_STREQ(out, "M");
  ASSERT_TRUE(copyInitial("Ávila", out, sizeof(out)));
  EXPECT_STREQ(out, "Á");
}

TEST(CopyInitial, FailsEmptyWhenThereIsNothingToCopyOrNoRoom) {
  char out[8] = "x";
  EXPECT_FALSE(copyInitial("", out, sizeof(out)));
  EXPECT_STREQ(out, "");
  EXPECT_FALSE(copyInitial(nullptr, out, sizeof(out)));
  EXPECT_STREQ(out, "");
  char tiny[2] = "x";
  EXPECT_FALSE(copyInitial("Ávila", tiny, sizeof(tiny)));
  EXPECT_STREQ(tiny, "");
}
