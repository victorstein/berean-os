#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "StudyStore/PassageDoc.h"
#include "StudyStore/TagPalette.h"

namespace study {

// A passage's tags in display order (TagPalette::nameRank, then raw id), for a list row. A copy, so
// stored order -- which the sleep screen reads for its tag pill -- never moves.
struct DisplayOrderedTags {
  std::array<TagId, PassageDoc::MAX_TAGS_PER_PASSAGE> ids{};
  size_t count = 0;
};

// Ids past MAX_TAGS_PER_PASSAGE are ignored; PassageDoc never stores more.
inline DisplayOrderedTags inDisplayOrder(const TagPalette& palette, const std::vector<TagId>& tags) {
  struct Ranked {
    uint16_t rank;
    TagId id;
  };
  std::array<Ranked, PassageDoc::MAX_TAGS_PER_PASSAGE> ranked{};

  DisplayOrderedTags out;
  out.count = std::min(tags.size(), ranked.size());
  for (size_t i = 0; i < out.count; ++i) ranked[i] = {palette.nameRank(tags[i]), tags[i]};
  std::sort(ranked.begin(), ranked.begin() + out.count, [](const Ranked& a, const Ranked& b) {
    return a.rank != b.rank ? a.rank < b.rank : toRaw(a.id) < toRaw(b.id);
  });
  for (size_t i = 0; i < out.count; ++i) out.ids[i] = ranked[i].id;
  return out;
}

}  // namespace study
