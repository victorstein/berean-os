#pragma once

#include <StudyStore/TagPalette.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

// Counting and two-line wrap for the Highlights tag chip row. Free of FreeInkUI, Arduino and
// GfxRenderer so the host suite can exercise it (test/tag_rows).
namespace TagChips {

inline bool passageMatches(const std::vector<study::TagId>& tags, const std::optional<study::TagId> filter) {
  if (!filter) return true;
  return std::find(tags.begin(), tags.end(), *filter) != tags.end();
}

struct Counts {
  size_t all = 0;
  size_t unlabelled = 0;
  // Aligned with the activeIds passed to count().
  std::vector<uint16_t> perTag;
};

// Agrees with passageMatches only because PassageDoc stores each passage's tags without duplicates
// and UNLABELLED only alone (normaliseTags); EveryCountEqualsTheRowsItsFilterShows pins that.
template <typename Passages>
Counts count(const Passages& passages, const std::vector<study::TagId>& activeIds) {
  Counts out;
  out.perTag.assign(activeIds.size(), 0);

  // Sorted (raw id, slot) so each carried id is a binary search, not a scan of the palette.
  std::vector<std::pair<uint16_t, uint16_t>> slots;
  slots.reserve(activeIds.size());
  for (size_t i = 0; i < activeIds.size(); ++i) {
    slots.emplace_back(study::toRaw(activeIds[i]), static_cast<uint16_t>(i));
  }
  std::sort(slots.begin(), slots.end());

  for (const auto& passage : passages) {
    ++out.all;
    for (const study::TagId id : passage.tags) {
      if (id == study::UNLABELLED) {
        ++out.unlabelled;
        continue;
      }
      const uint16_t raw = study::toRaw(id);
      const auto it = std::lower_bound(slots.begin(), slots.end(), std::make_pair(raw, uint16_t{0}));
      if (it == slots.end() || it->first != raw) continue;
      if (out.perTag[it->second] < UINT16_MAX) ++out.perTag[it->second];
    }
  }
  return out;
}

}  // namespace TagChips
