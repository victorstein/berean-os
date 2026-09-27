#pragma once

#include <cstdint>

// Proleptic Gregorian calendar arithmetic. Pure -- no Arduino, no <ctime> -- so
// the network code, the sleep screen and the host suites share one copy.

struct CivilDate {
  uint16_t year = 0;
  uint8_t month = 0;
  uint8_t day = 0;
};

// Days between 1970-01-01 and y-m-d, proleptic Gregorian (Howard Hinnant's
// days_from_civil). Avoids timegm, which newlib gates behind _GNU_SOURCE.
inline int64_t daysFromCivil(int y, const unsigned m, const unsigned d) {
  y -= m <= 2;
  const int64_t era = (y >= 0 ? y : y - 399) / 400;
  const auto yoe = static_cast<unsigned>(y - era * 400);
  const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + static_cast<int64_t>(doe) - 719468;
}

// Inverse of daysFromCivil (Hinnant's civil_from_days).
inline CivilDate civilFromDays(int64_t z) {
  z += 719468;
  const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
  const auto doe = static_cast<unsigned>(z - era * 146097);
  const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  const int64_t year = static_cast<int64_t>(yoe) + era * 400;
  const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const unsigned mp = (5 * doy + 2) / 153;
  const unsigned day = doy - (153 * mp + 2) / 5 + 1;
  const unsigned month = mp < 10 ? mp + 3 : mp - 9;
  CivilDate out;
  out.year = static_cast<uint16_t>(year + (month <= 2 ? 1 : 0));
  out.month = static_cast<uint8_t>(month);
  out.day = static_cast<uint8_t>(day);
  return out;
}

// A round trip through the day count rejects February 30th and the like, which a
// range check alone lets through.
inline bool isValidCivilDate(const CivilDate& date) {
  if (date.month < 1 || date.month > 12 || date.day < 1 || date.day > 31) return false;
  const CivilDate roundTrip = civilFromDays(daysFromCivil(date.year, date.month, date.day));
  return roundTrip.year == date.year && roundTrip.month == date.month && roundTrip.day == date.day;
}

// ISO weekday, 1 = Monday .. 7 = Sunday. 0 for a date that does not exist.
inline uint8_t isoWeekday(const CivilDate& date) {
  if (!isValidCivilDate(date)) return 0;
  const int64_t days = daysFromCivil(date.year, date.month, date.day);
  // 1970-01-01, day 0, was a Thursday: ISO weekday 4.
  const int64_t sinceMonday = ((days + 3) % 7 + 7) % 7;
  return static_cast<uint8_t>(sinceMonday + 1);
}

// The date `days` after (negative: before) `date`. An empty CivilDate when `date`
// does not exist.
inline CivilDate addDays(const CivilDate& date, const int days) {
  if (!isValidCivilDate(date)) return {};
  return civilFromDays(daysFromCivil(date.year, date.month, date.day) + days);
}

// The local calendar date for a UTC date and time, given the clock setting's
// quarter-hour offset biased by 48 (48 = UTC+0, 0 = UTC-12, 104 = UTC+14).
inline bool localDateFromUtc(const CivilDate& utc, const uint8_t hour, const uint8_t minute,
                             uint8_t offsetQuarterHoursBiased, CivilDate& out) {
  if (hour > 23 || minute > 59 || !isValidCivilDate(utc)) return false;
  // Same clamp as HalClock::formatTime, so a corrupted setting stays inside UTC-12..UTC+14.
  if (offsetQuarterHoursBiased > 104) offsetQuarterHoursBiased = 104;
  const int localMinutes = hour * 60 + minute + (static_cast<int>(offsetQuarterHoursBiased) - 48) * 15;
  const int dayShift = localMinutes < 0 ? -1 : (localMinutes >= 24 * 60 ? 1 : 0);
  out = addDays(utc, dayShift);
  return true;
}

// The viewer's date: the UTC date shifted by the local time of day, or the UTC
// date unchanged when the time could not be read.
inline bool localDateOrUtc(const CivilDate& utc, const bool haveTime, const uint8_t hour, const uint8_t minute,
                           const uint8_t offsetQuarterHoursBiased, CivilDate& out) {
  if (haveTime) return localDateFromUtc(utc, hour, minute, offsetQuarterHoursBiased, out);
  if (!isValidCivilDate(utc)) return false;
  out = utc;
  return true;
}
