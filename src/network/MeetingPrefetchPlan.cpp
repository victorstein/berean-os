#include "network/MeetingPrefetchPlan.h"

namespace {

// 28 December always falls in the last ISO week of its year.
uint8_t isoWeeksInYear(const uint16_t year) {
  IsoWeek lastWeek;
  if (!isoWeekFromUtcDate(year, 12, 28, lastWeek)) return 0;
  return lastWeek.week;
}

// An entry naming no issue is what a resolve that found nothing records; it
// answers nothing, so it must not stand in the way of asking again.
bool cacheNamesPublications(const MeetingWeekTable& cache, const IsoWeek& week) {
  const MeetingWeekEntry* entry = cache.find(meetingWeekKey(week));
  return entry != nullptr && (!entry->watchtower.empty() || !entry->workbook.empty());
}

}  // namespace

IsoWeek isoWeekAfter(const IsoWeek& week) {
  const uint8_t weeksInYear = isoWeeksInYear(week.year);
  if (weeksInYear == 0 || week.week == 0 || week.week > weeksInYear) return {};

  IsoWeek next;
  if (week.week == weeksInYear) {
    next.year = static_cast<uint16_t>(week.year + 1);
    next.week = 1;
  } else {
    next.year = week.year;
    next.week = static_cast<uint8_t>(week.week + 1);
  }
  return next;
}

bool meetingWeekToPrefetch(const MeetingPrefetchConditions& conditions, const MeetingWeekTable& cache, IsoWeek& out) {
  const IsoWeek& currentWeek = conditions.currentWeek;
  if (!conditions.enabled || !conditions.clockSynced) return false;
  if (currentWeek.year < EARLIEST_PLAUSIBLE_MEETING_YEAR || meetingWeekKey(currentWeek).empty()) return false;

  IsoWeek candidate = currentWeek;
  if (cacheNamesPublications(cache, currentWeek)) {
    candidate = isoWeekAfter(currentWeek);
    if (meetingWeekKey(candidate).empty() || cacheNamesPublications(cache, candidate)) return false;
  }

  if (meetingWeekKey(candidate) == conditions.attemptedThisBoot) return false;
  out = candidate;
  return true;
}
