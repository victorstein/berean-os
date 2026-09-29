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

// CRC32 over the whole document's visible codepoints. Same traversal, so it
// cannot drift from the offsets.
uint32_t documentVisibleCrc(const char* xhtml, size_t length);

// What a passage's stored text leaves out, on top of what every passage drops:
// footnotes (<aside>), footnote markers (<a epub:type="noteref">), and one space
// where a block ends. Counting is never affected, so every offset and fingerprint
// stays VisibleOffsetCounter's.
struct CaptureFilter {
  // A Bible chapter's own furniture: verse and chapter numbers (<sup>,
  // <span class="w_ch">) with the space after them, and unanchored headings such
  // as the acrostic letters (<p class="ss"> or "sd").
  bool verseDocument = false;
  // Keeps capturing past `to` to the end of the word it stops in, in document
  // order, so a filtered-out marker cannot shift where the cut lands.
  bool extendToWordEnd = false;
};

// Visible text of [from, to) as a passage stores it.
std::string extractPassageText(const char* xhtml, size_t length, uint32_t from, uint32_t to,
                               const CaptureFilter& filter);

// Chunk-fed form, so the firmware can stream a spine item through
// SpineHtmlStream rather than hold it whole.
class UnitTextScanner {
 public:
  UnitTextScanner();
  ~UnitTextScanner();
  UnitTextScanner(const UnitTextScanner&) = delete;
  UnitTextScanner& operator=(const UnitTextScanner&) = delete;

  bool valid() const { return parser_ != nullptr; }
  // Capture visible codepoints in [from, to). Must be called before feeding.
  void setRange(uint32_t from, uint32_t to);
  // Captures as extractPassageText does. Must be called before feeding.
  void setFilter(const CaptureFilter& filter);
  bool feed(const char* chunk, size_t length, bool isFinal);
  std::string take();

 private:
  void* parser_ = nullptr;  // XML_Parser
  void* state_ = nullptr;   // State
  bool failed_ = false;
};

}  // namespace study
