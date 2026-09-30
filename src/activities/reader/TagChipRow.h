#pragma once

#include <StudyStore/TagPalette.h>

#include <algorithm>
#include <optional>
#include <vector>

// Counting and two-line wrap for the Highlights tag chip row. Free of FreeInkUI, Arduino and
// GfxRenderer so the host suite can exercise it (test/tag_rows).
namespace TagChips {

inline bool passageMatches(const std::vector<study::TagId>& tags, const std::optional<study::TagId> filter) {
  if (!filter) return true;
  return std::find(tags.begin(), tags.end(), *filter) != tags.end();
}

}  // namespace TagChips
