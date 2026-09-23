#include "network/MeetingWeekPrefetch.h"

#include <Arduino.h>
#include <HalClock.h>
#include <Logging.h>

#include "network/HttpDownloader.h"
#include "network/MeetingPrefetchPlan.h"
#include "network/MeetingWeekCache.h"

namespace {

constexpr const char* MODULE = "MEETPF";

// RAM, not SD: a failed lookup should cost its bound once per boot, and a
// reboot is a fair point to try again.
std::string attemptedWeekThisBoot;

}  // namespace

namespace MeetingWeekPrefetch {

bool due(const bool enabled, const bool clockSynced, IsoWeek& week) {
  if (!enabled || !clockSynced) return false;

  HalClock::Date today{};
  MeetingPrefetchConditions conditions;
  conditions.enabled = enabled;
  conditions.clockSynced = clockSynced;
  conditions.attemptedThisBoot = attemptedWeekThisBoot;
  if (!halClock.getDate(today) || !isoWeekFromUtcDate(today.year, today.month, today.day, conditions.currentWeek)) {
    LOG_DBG(MODULE, "No usable date; not prefetching");
    return false;
  }

  MeetingWeekTable cache;
  MeetingWeekCache::load(cache);
  return meetingWeekToPrefetch(conditions, cache, week);
}

bool resolve(const IsoWeek& week, const Hooks& hooks, ResolvedWeek& out) {
  attemptedWeekThisBoot = meetingWeekKey(week);

  const std::string url = meetingsPageUrl(week);
  const unsigned long startedMs = millis();
  unsigned polls = 0;
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
        if (millis() - startedMs >= BUDGET_MS) timedOut = true;
        // SecureHttpClient polls once before it connects and then only from
        // waits it can leave (SecureHttpClient.h, sendRequestOnce), so the
        // second poll is the first one a skip is honoured at.
        if (++polls == 2 && hooks.onSkippable) hooks.onSkippable(hooks.ctx);
        if (polls >= 2 && hooks.skipRequested && hooks.skipRequested(hooks.ctx)) skipped = true;
        return skipped || timedOut;
      },
      NETWORK_TIMEOUT_MS);

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

  out.watchtower = scanner.issue(MeetingPub::Watchtower);
  out.workbook = scanner.issue(MeetingPub::Workbook);
  LOG_INF(MODULE, "Week %u/%02u resolved in %lu ms: w=%s mwb=%s", year, number, elapsedMs, out.watchtower.c_str(),
          out.workbook.c_str());
  return true;
}

bool record(const IsoWeek& week, const ResolvedWeek& resolved) {
  if (MeetingWeekCache::record(week, resolved.watchtower, resolved.workbook)) return true;
  LOG_ERR(MODULE, "Week %u/%02u resolved but the cache was not written", static_cast<unsigned>(week.year),
          static_cast<unsigned>(week.week));
  return false;
}

}  // namespace MeetingWeekPrefetch
