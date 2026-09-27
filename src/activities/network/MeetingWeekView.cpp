#include "MeetingWeekView.h"

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
