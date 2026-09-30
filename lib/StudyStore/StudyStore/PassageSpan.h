#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>

#include "StudyStore/UnitAnchors.h"

// Which document range a passage's stored text covers. Pure, so test/unit_text
// runs it on the host.
namespace study {

struct PassageSpan {
  uint32_t from = 0;
  uint32_t to = 0;
  bool verseDocument = false;    // snapped to whole verses; the filter's verse rules apply
  bool extendToWordEnd = false;  // not snapped: capture runs on to the end of the last word
};

namespace span_detail {

inline size_t anchorAtOrBefore(const DocumentUnits& units, const uint32_t offset) {
  size_t found = SIZE_MAX;
  for (size_t i = 0; i < units.anchors.size(); ++i) {
    if (units.anchors[i].offset > offset) break;
    found = i;
  }
  return found;
}

}  // namespace span_detail

// In a Verse document -- decided by the document, not by what `start` resolved
// to, so a selection starting in a superscription and a row migrated as raw
// offsets both snap -- the whole verse(s) the selection touches. Elsewhere the
// selection itself, to be extended to the end of its last word. nullopt when
// `start` or `end` is not in `units`.
//
// `end` is half-open, one past the last selected word's first codepoint, so the
// last selected codepoint is end - 1, clamped to the start: a row whose end
// equals its start covers the start's own verse.
inline std::optional<PassageSpan> snapSpan(const DocumentUnits& units, const Unit& start, const Unit& end) {
  const auto startOffset = documentOffsetOf(units, start);
  const auto endOffset = documentOffsetOf(units, end);
  if (!startOffset || !endOffset) return std::nullopt;

  if (units.kind != UnitKind::Verse || units.anchors.empty()) {
    return PassageSpan{*startOffset, std::max(*endOffset, *startOffset + 1), false, true};
  }

  const uint32_t lastSelected = *endOffset > *startOffset ? *endOffset - 1 : *startOffset;
  const size_t first = span_detail::anchorAtOrBefore(units, *startOffset);
  const size_t last = span_detail::anchorAtOrBefore(units, lastSelected);

  PassageSpan span;
  span.verseDocument = true;
  span.from = first == SIZE_MAX ? *startOffset : units.anchors[first].offset;
  span.to = last == SIZE_MAX ? units.anchors.front().offset : unitEndOffset(units, last);
  if (span.to <= span.from) return std::nullopt;
  return span;
}

}  // namespace study
