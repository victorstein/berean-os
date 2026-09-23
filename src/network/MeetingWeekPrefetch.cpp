#include "network/MeetingWeekPrefetch.h"

#include <Arduino.h>
#include <HalClock.h>
#include <Logging.h>

#include "network/HttpDownloader.h"
#include "network/MeetingPrefetchPlan.h"
#include "network/MeetingWeekCache.h"

namespace {
constexpr const char* MODULE = "MEETPF";
}

namespace MeetingWeekPrefetch {

bool due(const bool enabled, IsoWeek& week) {
  if (!enabled) return false;

  HalClock::Date today{};
  IsoWeek currentWeek;
  if (!halClock.getDate(today) || !isoWeekFromUtcDate(today.year, today.month, today.day, currentWeek)) {
    LOG_DBG(MODULE, "No usable date; not prefetching");
    return false;
  }

  MeetingWeekTable cache;
  MeetingWeekCache::load(cache);
  return meetingWeekToPrefetch(enabled, currentWeek, cache, week);
}

bool resolve(const IsoWeek& week, const SkipCheck skipRequested, void* ctx) {
  const std::string url = meetingsPageUrl(week);
  const unsigned long startedMs = millis();
  bool timedOut = false;
  bool skipped = false;
  size_t bytes = 0;
  WolWeekScanner scanner;

  const bool fetched = HttpDownloader::fetchUrl(
      url,
      [&scanner, &bytes](const uint8_t* data, const size_t len) {
        bytes += len;
        scanner.feed(reinterpret_cast<const char*>(data), len);
        return true;
      },
      [&]() {
        if (skipRequested && skipRequested(ctx)) skipped = true;
        if (millis() - startedMs >= BUDGET_MS) timedOut = true;
        return skipped || timedOut;
      });

  const unsigned long elapsedMs = millis() - startedMs;
  const unsigned year = week.year;
  const unsigned number = week.week;
  if (skipped || timedOut) {
    LOG_INF(MODULE, "Week %u/%02u %s after %lu ms, %zu bytes; cache left as it was", year, number,
            skipped ? "skipped" : "timed out", elapsedMs, bytes);
    return false;
  }
  if (!fetched) {
    LOG_ERR(MODULE, "Week page %s failed after %lu ms; cache left as it was", url.c_str(), elapsedMs);
    return false;
  }
  // A week wol.jw.org has not published yet answers with a page naming nothing.
  // Recording that would claim the week has no publications.
  if (scanner.count() == 0) {
    LOG_ERR(MODULE, "Week page %s named no publications (%zu bytes); not recorded", url.c_str(), bytes);
    return false;
  }

  if (!MeetingWeekCache::record(week, scanner.issue(MeetingPub::Watchtower), scanner.issue(MeetingPub::Workbook))) {
    LOG_ERR(MODULE, "Week %u/%02u resolved but the cache was not written", year, number);
    return false;
  }
  LOG_INF(MODULE, "Week %u/%02u resolved in %lu ms: w=%s mwb=%s", year, number, elapsedMs,
          scanner.issue(MeetingPub::Watchtower), scanner.issue(MeetingPub::Workbook));
  return true;
}

}  // namespace MeetingWeekPrefetch
