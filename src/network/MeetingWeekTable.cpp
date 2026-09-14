#include "network/MeetingWeekTable.h"

#include <algorithm>
#include <cstdio>

std::string meetingWeekKey(const IsoWeek& week) {
  if (week.year == 0 || week.week == 0 || week.week > 53) return {};
  char buf[16];
  snprintf(buf, sizeof(buf), "%04u-%02u", static_cast<unsigned>(week.year), static_cast<unsigned>(week.week));
  return buf;
}

void MeetingWeekTable::set(const std::string& key, std::string watchtower, std::string workbook) {
  if (key.empty()) return;

  const auto existing =
      std::find_if(entries_.begin(), entries_.end(), [&](const MeetingWeekEntry& e) { return e.key == key; });
  if (existing != entries_.end()) {
    existing->watchtower = std::move(watchtower);
    existing->workbook = std::move(workbook);
    return;
  }

  entries_.push_back({key, std::move(watchtower), std::move(workbook)});
}

const MeetingWeekEntry* MeetingWeekTable::find(const std::string& key) const {
  const auto found =
      std::find_if(entries_.begin(), entries_.end(), [&](const MeetingWeekEntry& e) { return e.key == key; });
  return found == entries_.end() ? nullptr : &*found;
}

const MeetingWeekEntry* MeetingWeekTable::newest() const {
  const auto found = std::max_element(entries_.begin(), entries_.end(),
                                      [](const MeetingWeekEntry& a, const MeetingWeekEntry& b) { return a.key < b.key; });
  return found == entries_.end() ? nullptr : &*found;
}

void MeetingWeekTable::prune() {
  if (entries_.size() <= MAX_WEEKS) return;
  std::sort(entries_.begin(), entries_.end(),
            [](const MeetingWeekEntry& a, const MeetingWeekEntry& b) { return a.key > b.key; });
  entries_.resize(MAX_WEEKS);
}
