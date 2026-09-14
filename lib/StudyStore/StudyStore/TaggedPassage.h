#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "StudyStore/TagPalette.h"
#include "StudyStore/Unit.h"
#include "StudyStore/UnitFingerprint.h"

namespace study {

// One tagged span. `start` and `end` are Units, so the passage survives the
// publication being re-downloaded; `document` and `documentSpine` are
// resolution HINTS for finding it again quickly, never the identity -- for the
// Bible they are language-specific and the Unit is not.
struct TaggedPassage {
  Unit start;
  Unit end;
  Fingerprint fingerprint;      // over the start unit's visible codepoints
  Fingerprint endFingerprint;   // over the end unit's, when the span crosses one
  std::string document;         // filename inside the archive, a hint
  uint16_t documentSpine = 0;   // spine index, a weaker hint
  std::string snippet;          // bounded passage text, for the tag list
  std::string reference;        // "Salmos 119:145", display + migration cross-check
  std::vector<TagId> tags;      // global ids
  bool pendingUpgrade = false;  // migrated without the EPUB; upgrade on next open
};

}  // namespace study
