#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "StudyStore/TagPalette.h"
#include "StudyStore/Unit.h"
#include "StudyStore/UnitFingerprint.h"

namespace study {

// A directed cross-reference to another passage. The target is its start Unit,
// not an index, so the link outlives the target passage being deleted and
// re-added, and for a Verse address survives an edition change. targetSpine is
// only a hint -- and the only way to find a Paragraph or DocumentOffset target,
// neither of which is unique outside its own document.
struct PassageLink {
  Unit target;
  uint16_t targetSpine = 0;
  std::string label;  // the target's reference (or snippet) when linked

  bool operator==(const PassageLink&) const = default;
};

// One tagged span. `start` and `end` are Units, so the passage survives the
// publication being re-downloaded; `document` and `documentSpine` are
// resolution HINTS for finding it again quickly, never the identity -- for the
// Bible they are language-specific and the Unit is not.
struct TaggedPassage {
  Unit start;
  Unit end;
  Fingerprint fingerprint;         // over the start unit's visible codepoints
  Fingerprint endFingerprint;      // over the end unit's, when the span crosses one
  std::string document;            // filename inside the archive, a hint
  uint16_t documentSpine = 0;      // spine index, a weaker hint
  std::string snippet;             // bounded passage text, for the tag list
  std::string reference;           // "Salmos 119:145", display + migration cross-check
  std::vector<TagId> tags;         // global ids
  std::vector<PassageLink> links;  // outgoing only
  bool pendingUpgrade = false;     // migrated without the EPUB; upgrade on next open
};

}  // namespace study
