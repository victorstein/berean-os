#pragma once

#include "network/MeetingWeekTable.h"
#include "network/WolWeekScan.h"

// Which meeting week, if any, an opportunistic resolve should look up while the
// radio happens to be up. Pure: no Arduino, no I/O, so the host suite exercises
// it directly. MeetingWeekPrefetch runs the answer on the device.

// The ISO week after `week`, rolling into week 1 of the next year after the
// last week of this one -- 52 or 53, depending on the year. A zero year and
// week when `week` is not one the calendar has.
IsoWeek isoWeekAfter(const IsoWeek& week);

// The current week if the cache cannot name its publications, otherwise next
// week if the cache cannot name that one, otherwise nothing. One week per call,
// so a single opportunity never costs more than one page fetch.
bool meetingWeekToPrefetch(bool enabled, const IsoWeek& currentWeek, const MeetingWeekTable& cache, IsoWeek& out);
