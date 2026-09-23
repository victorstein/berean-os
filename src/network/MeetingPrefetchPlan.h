#pragma once

#include <string>

#include "network/MeetingWeekTable.h"
#include "network/WolWeekScan.h"

// Which meeting week, if any, an opportunistic resolve should look up while the
// radio happens to be up. Pure: no Arduino, no I/O, so the host suite exercises
// it directly. MeetingWeekPrefetch runs the answer on the device.

// A date before this is a clock that lost its time, not a real week: an RTC that
// reset reads 2000, and asking wol.jw.org for 1999/52 on every connection would
// cost a fetch that can never be useful.
inline constexpr uint16_t EARLIEST_PLAUSIBLE_MEETING_YEAR = 2026;

struct MeetingPrefetchConditions {
  bool enabled = false;
  // The clock has been set from the network at least once.
  bool clockSynced = false;
  IsoWeek currentWeek;
  // meetingWeekKey() of the week already attempted since boot, "" when none.
  // One attempt per week per boot, so a lookup that keeps failing costs its
  // bound once rather than on every connection.
  std::string attemptedThisBoot;
};

// The ISO week after `week`, rolling into week 1 of the next year after the
// last week of this one -- 52 or 53, depending on the year. A zero year and
// week when `week` is not one the calendar has.
IsoWeek isoWeekAfter(const IsoWeek& week);

// The current week if the cache cannot name its publications, otherwise next
// week if the cache cannot name that one, otherwise nothing. One week per call,
// so a single opportunity never costs more than one page fetch.
bool meetingWeekToPrefetch(const MeetingPrefetchConditions& conditions, const MeetingWeekTable& cache, IsoWeek& out);
