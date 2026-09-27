#include "MeetingWeekView.h"

#include <Catalog/CatalogLabel.h>

#include <cstdio>

namespace {

constexpr size_t MAX_MONTH_NAME_BYTES = 16;
constexpr size_t MAX_RANGE_BYTES = 96;

}  // namespace

WeekStrip buildWeekStrip(const CivilDate& monday, const CivilDate* localToday, const uint8_t midweekDay,
                         const uint8_t weekendDay) {
  WeekStrip strip{};
  for (size_t i = 0; i < strip.size(); ++i) {
    const CivilDate date = addDays(monday, static_cast<int>(i));
    const auto weekday = static_cast<uint8_t>(i + 1);
    strip[i].day = date.day;
    strip[i].today = localToday != nullptr && date.year == localToday->year && date.month == localToday->month &&
                     date.day == localToday->day;
    strip[i].meeting = weekday == midweekDay || weekday == weekendDay;
  }
  return strip;
}

bool copyWordAt(const std::string_view words, const int index, char* out, const size_t outSize) {
  if (out == nullptr || outSize == 0) return false;
  out[0] = '\0';
  const std::string_view word = catalog::wordAt(words, index);
  if (word.empty()) return false;
  return catalog::copyOut(word, out, outSize);
}

bool formatWeekRange(const CivilDate& monday, const char* sameMonthFormat, const char* twoMonthFormat,
                     const std::string_view monthsLong, char* out, const size_t outSize) {
  if (out == nullptr || outSize == 0) return false;
  out[0] = '\0';
  if (sameMonthFormat == nullptr || twoMonthFormat == nullptr) return false;

  const CivilDate sunday = addDays(monday, 6);
  if (sunday.month == 0) return false;

  char firstMonth[MAX_MONTH_NAME_BYTES];
  char lastMonth[MAX_MONTH_NAME_BYTES];
  if (!copyWordAt(monthsLong, monday.month - 1, firstMonth, sizeof(firstMonth)) ||
      !copyWordAt(monthsLong, sunday.month - 1, lastMonth, sizeof(lastMonth))) {
    return false;
  }

  char line[MAX_RANGE_BYTES];
  const int written = monday.month == sunday.month
                          ? snprintf(line, sizeof(line), sameMonthFormat, static_cast<unsigned>(monday.day),
                                     static_cast<unsigned>(sunday.day), lastMonth)
                          : snprintf(line, sizeof(line), twoMonthFormat, static_cast<unsigned>(monday.day), firstMonth,
                                     static_cast<unsigned>(sunday.day), lastMonth);
  if (written <= 0 || static_cast<size_t>(written) >= sizeof(line)) return false;
  return catalog::copyOut(std::string_view(line, static_cast<size_t>(written)), out, outSize);
}
