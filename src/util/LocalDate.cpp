#include "util/LocalDate.h"

#include <HalClock.h>

#include "CrossPointSettings.h"

bool readLocalDate(CivilDate& out, bool& shifted) {
  shifted = false;
  HalClock::Date date{};
  if (!halClock.getDate(date)) return false;

  CivilDate utc;
  utc.year = date.year;
  utc.month = date.month;
  utc.day = date.day;
  uint8_t hour = 0;
  uint8_t minute = 0;
  const bool haveTime = halClock.getTime(hour, minute);
  if (!localDateOrUtc(utc, haveTime, hour, minute, SETTINGS.clockUtcOffsetQ, out)) return false;
  shifted = haveTime;
  return true;
}
