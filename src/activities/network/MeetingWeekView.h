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
WeekStrip buildWeekStrip(const CivilDate& monday, const CivilDate* localToday, uint8_t midweekDay,
                         uint8_t weekendDay);
