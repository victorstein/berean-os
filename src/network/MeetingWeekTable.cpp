#include "network/MeetingWeekTable.h"

#include <algorithm>
#include <cstdio>

std::string meetingWeekKey(const IsoWeek& week) {
  if (week.year == 0 || week.week == 0 || week.week > 53) return {};
  char buf[16];
  snprintf(buf, sizeof(buf), "%04u-%02u", static_cast<unsigned>(week.year), static_cast<unsigned>(week.week));
  return buf;
}

bool isoWeekFromKey(const std::string& key, IsoWeek& out) {
  if (key.size() != 7 || key[4] != '-') return false;
  unsigned year = 0;
  unsigned number = 0;
  for (size_t i = 0; i < key.size(); ++i) {
    if (i == 4) continue;
    const char c = key[i];
    if (c < '0' || c > '9') return false;
    if (i < 4) {
      year = year * 10 + static_cast<unsigned>(c - '0');
    } else {
      number = number * 10 + static_cast<unsigned>(c - '0');
    }
  }
  IsoWeek candidate;
  candidate.year = static_cast<uint16_t>(year);
  candidate.week = static_cast<uint8_t>(number);
  CivilDate monday;
  if (!mondayOfIsoWeek(candidate, monday)) return false;
  out = candidate;
  return true;
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
  const auto found =
      std::max_element(entries_.begin(), entries_.end(),
                       [](const MeetingWeekEntry& a, const MeetingWeekEntry& b) { return a.key < b.key; });
  return found == entries_.end() ? nullptr : &*found;
}

void MeetingWeekTable::prune() {
  if (entries_.size() <= MAX_WEEKS) return;
  std::sort(entries_.begin(), entries_.end(),
            [](const MeetingWeekEntry& a, const MeetingWeekEntry& b) { return a.key > b.key; });
  entries_.resize(MAX_WEEKS);
}
