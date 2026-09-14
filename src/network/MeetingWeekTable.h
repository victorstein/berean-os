#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "network/WolWeekScan.h"

// Which issues a meeting week references, remembered so the device can name
// this week's publications without going back to wol.jw.org. Pure: no Arduino,
// no I/O, so the host suite exercises it directly. MeetingWeekCache owns the
// file this is serialised into.
//
// Either publication may be absent: the Memorial week carries a Watchtower link
// and no workbook, and that recurs annually.
struct MeetingWeekEntry {
  std::string key;
  std::string watchtower;
  std::string workbook;
};

// "2026-38" for ISO week 38 of 2026. Zero-padded so that lexicographic order is
// chronological order, which is what lets prune() and the newest() scan work on
// the string without parsing it back into numbers.
std::string meetingWeekKey(const IsoWeek& week);

class MeetingWeekTable {
 public:
  // A quarter of a year. Enough to answer "what was last week?" across a device
  // left in a drawer, without letting the file grow without bound.
  static constexpr size_t MAX_WEEKS = 13;

  // Replaces any entry already held for this key.
  void set(const std::string& key, std::string watchtower, std::string workbook);

  // Null when the week is not held. Borrowed; invalidated by the next set().
  const MeetingWeekEntry* find(const std::string& key) const;

  // The most recently dated entry, or null when empty. This is what a stale
  // screen paints while the current week resolves behind it.
  const MeetingWeekEntry* newest() const;

  // Drops all but the MAX_WEEKS most recent entries.
  void prune();

  const std::vector<MeetingWeekEntry>& entries() const { return entries_; }
  void clear() { entries_.clear(); }

 private:
  std::vector<MeetingWeekEntry> entries_;
};
