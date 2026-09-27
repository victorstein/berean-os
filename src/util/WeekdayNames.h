#pragma once

#include <I18nKeys.h>

#include "CrossPointSettings.h"

// The one source of weekday names. Monday first: index = isoWeekday() - 1 =
// meeting-day setting - MEETING_DAY_MONDAY. Short forms are derived from these,
// never translated separately.
inline constexpr StrId WEEKDAY_NAME_IDS[7] = {StrId::STR_MONDAY,   StrId::STR_TUESDAY, StrId::STR_WEDNESDAY,
                                              StrId::STR_THURSDAY, StrId::STR_FRIDAY,  StrId::STR_SATURDAY,
                                              StrId::STR_SUNDAY};

static_assert(CrossPointSettings::MEETING_DAY_SUNDAY - CrossPointSettings::MEETING_DAY_MONDAY == 6,
              "the meeting-day settings must run Monday..Sunday contiguously");
