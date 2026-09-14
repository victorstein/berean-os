#pragma once

#include <string>

#include "network/MeetingWeekTable.h"

// /.berean/meeting-weeks.json -- ISO week -> the issues that week references.
//
// Which issues belong to a week is only knowable from wol.jw.org: the codes
// appear nowhere on the device, and recovering them means fetching and scraping
// the week's meetings page, which blocks for up to a minute. Remembering the
// answer is what lets the meetings screen name this week's publications
// offline, and lets it paint something while a new week resolves behind it.
//
// This is a cache, not study data. Losing it costs one rescan and never a
// user's work, which is why it may be pruned and why a version it does not
// recognise is discarded rather than migrated.
namespace MeetingWeekCache {

// Missing or unreadable leaves `out` empty and returns false; the caller
// carries on with a cold cache rather than treating it as an error.
bool load(MeetingWeekTable& out);

// Prunes before writing, so the file cannot grow past MAX_WEEKS.
bool save(MeetingWeekTable& table);

// load + set + prune + save. What the download sequence calls once it has
// resolved a week.
bool record(const IsoWeek& week, const std::string& watchtower, const std::string& workbook);

inline constexpr const char* PATH = "/.berean/meeting-weeks.json";

}  // namespace MeetingWeekCache
