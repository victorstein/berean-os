#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "network/WolWeekScan.h"

// What the meetings screen draws above its cards, computed without Arduino or
// I/O so the host suite checks it directly.

struct WeekStripCell {
  uint8_t day = 0;
  bool today = false;
  bool meeting = false;
};

// Monday first.
using WeekStrip = std::array<WeekStripCell, 7>;

// `localToday` is marked only when it falls inside this week; null marks nothing.
// The meeting days are ISO weekdays (1 = Monday .. 7 = Sunday); 0, the "not set"
// setting, and anything past 7 mark nothing.
WeekStrip buildWeekStrip(const CivilDate& monday, const CivilDate* localToday, uint8_t midweekDay, uint8_t weekendDay);

// "Week of 21–27 September" from the two translated range formats, which take
// (day, day, month) and (day, month, day, month), and the twelve-word month
// list. False, leaving `out` empty, when a month name is missing or the text
// does not fit.
bool formatWeekRange(const CivilDate& monday, const char* sameMonthFormat, const char* twoMonthFormat,
                     std::string_view monthsLong, char* out, size_t outSize);

// The nth word of a space-separated translated list, copied out and terminated:
// a view into the list cannot go to %s or a text draw. False, leaving `out`
// empty, when there is no such word or it does not fit.
bool copyWordAt(std::string_view words, int index, char* out, size_t outSize);
