#include <gtest/gtest.h>

#include <cstdio>
#include <string>

#include "util/CivilDate.h"

namespace {

CivilDate civil(const uint16_t year, const uint8_t month, const uint8_t day) {
  CivilDate date;
  date.year = year;
  date.month = month;
  date.day = day;
  return date;
}

std::string text(const CivilDate& date) {
  char out[16];
  snprintf(out, sizeof(out), "%04u-%02u-%02u", static_cast<unsigned>(date.year), static_cast<unsigned>(date.month),
           static_cast<unsigned>(date.day));
  return out;
}

constexpr uint8_t UTC = 48;

}  // namespace

TEST(CivilDate, RejectsDatesThatDoNotExist) {
  EXPECT_FALSE(isValidCivilDate(civil(2026, 2, 30)));
  EXPECT_FALSE(isValidCivilDate(civil(2026, 2, 29)));
  EXPECT_FALSE(isValidCivilDate(civil(2026, 13, 1)));
  EXPECT_FALSE(isValidCivilDate(civil(2026, 9, 0)));
  EXPECT_TRUE(isValidCivilDate(civil(2028, 2, 29)));
}

TEST(CivilDate, TheDayCountStartsAtTheEpochAndRoundTrips) {
  EXPECT_EQ(daysFromCivil(1970, 1, 1), 0);
  EXPECT_EQ(text(civilFromDays(0)), "1970-01-01");
  EXPECT_EQ(text(civilFromDays(daysFromCivil(2026, 9, 27))), "2026-09-27");
}

TEST(CivilDate, IsoWeekdayRunsMondayToSundayFromAThursdayEpoch) {
  EXPECT_EQ(isoWeekday(civil(1970, 1, 1)), 4);
  EXPECT_EQ(isoWeekday(civil(2026, 9, 28)), 1);
  EXPECT_EQ(isoWeekday(civil(2026, 9, 27)), 7);
}

TEST(CivilDate, LocalDateShiftsBackForwardOrNotAtAll) {
  CivilDate local;
  ASSERT_TRUE(localDateFromUtc(civil(2026, 9, 28), 2, 0, 24, local));
  EXPECT_EQ(text(local), "2026-09-27");
  ASSERT_TRUE(localDateFromUtc(civil(2026, 9, 27), 10, 0, UTC, local));
  EXPECT_EQ(text(local), "2026-09-27");
  ASSERT_TRUE(localDateFromUtc(civil(2026, 9, 27), 11, 0, 104, local));
  EXPECT_EQ(text(local), "2026-09-28");
}

TEST(CivilDate, AddDaysCrossesAYearAndALeapDay) {
  EXPECT_EQ(text(addDays(civil(2026, 12, 31), 1)), "2027-01-01");
  EXPECT_EQ(text(addDays(civil(2028, 3, 1), -1)), "2028-02-29");
  EXPECT_EQ(addDays(civil(2026, 2, 30), 1).month, 0);
}

TEST(CivilDate, LocalDateClampsACorruptOffsetToUtcPlusFourteen) {
  CivilDate local;
  ASSERT_TRUE(localDateFromUtc(civil(2026, 9, 27), 9, 0, 200, local));
  EXPECT_EQ(text(local), "2026-09-27");
  ASSERT_TRUE(localDateFromUtc(civil(2026, 9, 27), 12, 0, 200, local));
  EXPECT_EQ(text(local), "2026-09-28");
}

TEST(CivilDate, LocalDateRefusesAnImpossibleReading) {
  CivilDate local;
  EXPECT_FALSE(localDateFromUtc(civil(2026, 2, 30), 10, 0, UTC, local));
  EXPECT_FALSE(localDateFromUtc(civil(2026, 9, 27), 24, 0, UTC, local));
  EXPECT_FALSE(localDateFromUtc(civil(2026, 9, 27), 10, 60, UTC, local));
}
