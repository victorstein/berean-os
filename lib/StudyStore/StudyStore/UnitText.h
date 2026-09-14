#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "StudyStore/UnitAnchors.h"

// The ONE producer of "a unit's visible text". Both the fingerprint written at
// migration time and the fingerprint checked at paint time must come from here,
// or they will disagree: the repo's other candidate, PassageSelectActivity's
// selectionLabel, joins laid-out word boxes with a plain space and loses the
// U+202F the Spanish NWT puts before verse text. A disagreement makes every
// fingerprint mismatch, and the degradation rule then refuses to paint anything.
//
// Counting is VisibleOffsetCounter's, unmodified, so the extracted text's
// codepoint count equals the distance between consecutive anchor offsets. That
// invariant is what ties a stored offset to real text; UnitTextTest asserts it.
namespace study {

// Visible text of the unit starting at `anchor`, up to the next anchor or the
// end of the body. Empty when the document has no units.
std::string extractUnitText(const char* xhtml, size_t length, const DocumentUnits& units, const UnitAnchor& anchor);

// Visible text between two document offsets, [from, to). Used to fingerprint a
// span that does not begin at a unit boundary.
std::string extractRangeText(const char* xhtml, size_t length, uint32_t from, uint32_t to);

// CRC32 over the whole document's visible codepoints, for unit-index
// invalidation. Same traversal, so it cannot drift from the offsets.
uint32_t documentVisibleCrc(const char* xhtml, size_t length);

}  // namespace study
