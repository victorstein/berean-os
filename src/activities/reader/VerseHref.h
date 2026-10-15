#pragma once

#include <Epub/VerseAnchors.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <optional>
#include <vector>

// The verse a Bible href points at, from its `#chapter<N>_verse<M>` fragment. Verse markers are
// empty spans, which never enter a section's anchor map, so a verse is reached by its marker's offset
// in a VerseAnchors scan rather than by the href's anchor. Arduino-free for test/verse_anchors.
namespace VerseHref {

struct Verse {
  uint16_t chapter;
  uint16_t verse;
};

// The same grammar VerseAnchors reads marker ids with, so an href and a scan agree on what names a
// verse. nullopt for no fragment, another fragment, trailing junk, or a number past uint16_t.
inline std::optional<Verse> parse(const char* href) {
  const char* hash = std::strchr(href, '#');
  if (!hash) return std::nullopt;
  unsigned chapter = 0;
  unsigned verse = 0;
  char tail = '\0';
  if (std::sscanf(hash + 1, "chapter%u_verse%u%c", &chapter, &verse, &tail) != 2) return std::nullopt;
  if (chapter > UINT16_MAX || verse > UINT16_MAX) return std::nullopt;
  return Verse{static_cast<uint16_t>(chapter), static_cast<uint16_t>(verse)};
}

// By value, so the result outlives a temporary `anchors` such as Scanner::take()'s.
inline std::optional<uint32_t> offsetOf(const std::vector<VerseAnchors::VerseAnchor>& anchors, const Verse verse) {
  for (const auto& anchor : anchors) {
    if (anchor.chapter == verse.chapter && anchor.verse == verse.verse) return anchor.offset;
  }
  return std::nullopt;
}

}  // namespace VerseHref
