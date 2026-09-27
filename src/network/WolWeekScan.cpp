#include "WolWeekScan.h"

#include <cstdio>
#include <cstring>
#include <ctime>

namespace {

constexpr const char* WATCHTOWER_PREFIX = "the-watchtower-";
constexpr const char* WATCHTOWER_MIDDLE = "/study-edition/";
constexpr const char* WORKBOOK_PREFIX = "life-and-ministry-meeting-workbook-";
constexpr const char* WORKBOOK_MIDDLE = "/";

constexpr const char* MONTH_NAMES[12] = {"january", "february", "march",     "april",   "may",      "june",
                                         "july",    "august",   "september", "october", "november", "december"};

bool isLowerAlpha(char c) { return c >= 'a' && c <= 'z'; }
bool isDigit(char c) { return c >= '0' && c <= '9'; }

// Days between 1970-01-01 and y-m-d, proleptic Gregorian (Howard Hinnant's
// days_from_civil). Avoids timegm, which newlib gates behind _GNU_SOURCE.
int64_t daysFromCivil(int y, unsigned m, unsigned d) {
  y -= m <= 2;
  const int64_t era = (y >= 0 ? y : y - 399) / 400;
  const auto yoe = static_cast<unsigned>(y - era * 400);
  const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + static_cast<int64_t>(doe) - 719468;
}

// Inverse of daysFromCivil (Hinnant's civil_from_days).
CivilDate civilFromDays(int64_t z) {
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
bool isValidCivilDate(const CivilDate& date) {
  if (date.month < 1 || date.month > 12 || date.day < 1 || date.day > 31) return false;
  const CivilDate roundTrip = civilFromDays(daysFromCivil(date.year, date.month, date.day));
  return roundTrip.year == date.year && roundTrip.month == date.month && roundTrip.day == date.day;
}

}  // namespace

uint8_t monthNumberFromName(const char* name, const size_t len) {
  if (!name) return 0;
  for (uint8_t i = 0; i < 12; ++i) {
    if (strlen(MONTH_NAMES[i]) == len && memcmp(name, MONTH_NAMES[i], len) == 0) return static_cast<uint8_t>(i + 1);
  }
  return 0;
}

bool isoWeekFromUtcDate(const uint16_t year, const uint8_t month, const uint8_t day, IsoWeek& out) {
  if (year < 1970 || month < 1 || month > 12 || day < 1 || day > 31) return false;

  const auto seconds = static_cast<time_t>(daysFromCivil(year, month, day) * 86400);
  struct tm utc = {};
  if (!gmtime_r(&seconds, &utc)) return false;

  char formatted[8];
  // A strftime without %G/%V support copies them through literally, which would
  // otherwise be sent as a week number; the digit check rejects that.
  if (strftime(formatted, sizeof(formatted), "%G%V", &utc) != 6) return false;
  for (int i = 0; i < 6; ++i) {
    if (!isDigit(formatted[i])) return false;
  }

  out.year = static_cast<uint16_t>((formatted[0] - '0') * 1000 + (formatted[1] - '0') * 100 +
                                   (formatted[2] - '0') * 10 + (formatted[3] - '0'));
  out.week = static_cast<uint8_t>((formatted[4] - '0') * 10 + (formatted[5] - '0'));
  return out.week >= 1 && out.week <= 53;
}

uint8_t isoWeekday(const CivilDate& date) {
  if (!isValidCivilDate(date)) return 0;
  const int64_t days = daysFromCivil(date.year, date.month, date.day);
  // 1970-01-01, day 0, was a Thursday: ISO weekday 4.
  const int64_t sinceMonday = ((days + 3) % 7 + 7) % 7;
  return static_cast<uint8_t>(sinceMonday + 1);
}

CivilDate addDays(const CivilDate& date, const int days) {
  if (!isValidCivilDate(date)) return {};
  return civilFromDays(daysFromCivil(date.year, date.month, date.day) + days);
}

bool mondayOfIsoWeek(const IsoWeek& week, CivilDate& out) {
  if (week.week < 1 || week.week > 53) return false;
  CivilDate january4;
  january4.year = week.year;
  january4.month = 1;
  january4.day = 4;
  // ISO week 1 is the week that holds January 4th.
  const CivilDate firstMonday = addDays(january4, 1 - static_cast<int>(isoWeekday(january4)));
  const CivilDate candidate = addDays(firstMonday, 7 * (week.week - 1));
  // Validated by round trip: a week 53 the year does not have lands in next year's week 1.
  IsoWeek back;
  if (!isoWeekFromUtcDate(candidate.year, candidate.month, candidate.day, back)) return false;
  if (back.year != week.year || back.week != week.week) return false;
  out = candidate;
  return true;
}

bool localDateFromUtc(const CivilDate& utc, const uint8_t hour, const uint8_t minute, uint8_t offsetQuarterHoursBiased,
                      CivilDate& out) {
  if (hour > 23 || minute > 59 || !isValidCivilDate(utc)) return false;
  // Same clamp as HalClock::formatTime, so a corrupted setting stays inside UTC-12..UTC+14.
  if (offsetQuarterHoursBiased > 104) offsetQuarterHoursBiased = 104;
  const int localMinutes = hour * 60 + minute + (static_cast<int>(offsetQuarterHoursBiased) - 48) * 15;
  const int dayShift = localMinutes < 0 ? -1 : (localMinutes >= 24 * 60 ? 1 : 0);
  out = addDays(utc, dayShift);
  return true;
}

std::string meetingsPageUrl(const IsoWeek& week) {
  // The English page keeps the scanner on ASCII month names and needs no
  // per-language rsconf table; the week -> issue mapping is language-independent.
  char buf[64];
  snprintf(buf, sizeof(buf), "https://wol.jw.org/en/wol/meetings/r1/lp-e/%u/%02u", static_cast<unsigned>(week.year),
           static_cast<unsigned>(week.week));
  return buf;
}

std::string pubMediaUrlForSymbol(const char* symbol, const char* issue, const char* languageKey, const bool epubOnly) {
  const char* formatFilter = epubOnly ? "&fileformat=EPUB" : "";
  char buf[200];
  if (issue == nullptr || issue[0] == '\0') {
    snprintf(buf, sizeof(buf),
             "https://b.jw-cdn.org/apis/pub-media/GETPUBMEDIALINKS?output=json&pub=%s&langwritten=%s%s", symbol,
             languageKey, formatFilter);
  } else {
    snprintf(buf, sizeof(buf),
             "https://b.jw-cdn.org/apis/pub-media/GETPUBMEDIALINKS?output=json&pub=%s&langwritten=%s%s&issue=%s",
             symbol, languageKey, formatFilter, issue);
  }
  return buf;
}

std::string pubMediaUrl(const MeetingPub pub, const char* issue, const char* languageKey) {
  return pubMediaUrlForSymbol(pub == MeetingPub::Watchtower ? "w" : "mwb", issue, languageKey);
}

std::string filenameFromUrl(const std::string& url) {
  const size_t slash = url.rfind('/');
  if (slash == std::string::npos) return url;
  return url.substr(slash + 1);
}

void WolWeekScanner::Matcher::reset() {
  stage = Stage::Prefix;
  prefixPos = 0;
  middlePos = 0;
  yearLen = 0;
  monthLen = 0;
  year[0] = '\0';
  month[0] = '\0';
  issue[0] = '\0';
}

void WolWeekScanner::Matcher::restartPrefix(const char c) {
  stage = Stage::Prefix;
  prefixPos = c == prefix[0] ? 1 : 0;
}

void WolWeekScanner::Matcher::commit() {
  const uint8_t monthNumber = monthNumberFromName(month, monthLen);
  if (monthNumber == 0) {
    reset();
    return;
  }
  snprintf(issue, sizeof(issue), "%s%02u", year, static_cast<unsigned>(monthNumber));
  stage = Stage::Done;
}

void WolWeekScanner::Matcher::consume(const char c) {
  switch (stage) {
    case Stage::Prefix:
      if (c == prefix[prefixPos]) {
        if (prefix[++prefixPos] == '\0') {
          stage = Stage::Year;
          yearLen = 0;
        }
      } else {
        restartPrefix(c);
      }
      break;

    case Stage::Year:
      if (!isDigit(c)) {
        restartPrefix(c);
      } else {
        year[yearLen++] = c;
        if (yearLen == 4) {
          year[4] = '\0';
          stage = Stage::Middle;
          middlePos = 0;
        }
      }
      break;

    case Stage::Middle:
      if (c == middle[middlePos]) {
        if (middle[++middlePos] == '\0') {
          stage = Stage::Month;
          monthLen = 0;
        }
      } else {
        restartPrefix(c);
      }
      break;

    case Stage::Month:
      if (isLowerAlpha(c)) {
        // Anything longer than "september" is not a month name; drop the match
        // rather than truncate it into a false positive.
        if (monthLen >= sizeof(month) - 1) {
          restartPrefix(c);
        } else {
          month[monthLen++] = c;
        }
      } else {
        month[monthLen] = '\0';
        commit();
      }
      break;

    case Stage::Done:
      break;
  }
}

WolWeekScanner::WolWeekScanner() {
  watchtower_.prefix = WATCHTOWER_PREFIX;
  watchtower_.middle = WATCHTOWER_MIDDLE;
  workbook_.prefix = WORKBOOK_PREFIX;
  workbook_.middle = WORKBOOK_MIDDLE;
  reset();
}

void WolWeekScanner::reset() {
  watchtower_.reset();
  workbook_.reset();
}

void WolWeekScanner::feed(const char* data, const size_t len) {
  for (size_t i = 0; i < len; ++i) {
    watchtower_.consume(data[i]);
    workbook_.consume(data[i]);
  }
}

WolWeekScanner::Matcher& WolWeekScanner::matcherFor(const MeetingPub pub) {
  return pub == MeetingPub::Watchtower ? watchtower_ : workbook_;
}

const WolWeekScanner::Matcher& WolWeekScanner::matcherFor(const MeetingPub pub) const {
  return pub == MeetingPub::Watchtower ? watchtower_ : workbook_;
}

bool WolWeekScanner::has(const MeetingPub pub) const { return matcherFor(pub).stage == Stage::Done; }

const char* WolWeekScanner::issue(const MeetingPub pub) const { return matcherFor(pub).issue; }

int WolWeekScanner::count() const {
  return (has(MeetingPub::Watchtower) ? 1 : 0) + (has(MeetingPub::Workbook) ? 1 : 0);
}
