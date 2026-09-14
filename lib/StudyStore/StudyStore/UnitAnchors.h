#pragma once

#include <cstddef>
#include <optional>
#include <vector>

#include "StudyStore/Unit.h"

// One document's addressable units, and the single kind that document supports.
//
// Precedence is Verse > Paragraph > DocumentOffset, and it is NOT a fallback
// chain over disjoint publications: measured on the NWT, all 1,189 verse-marked
// documents also carry data-pid. Without a precedence rule the same passage
// would address two ways depending on scan order.
namespace study {

struct UnitAnchor {
  uint32_t offset;  // visible codepoints into the document
  uint16_t major;
  uint16_t minor;
};

struct DocumentUnits {
  UnitKind kind = UnitKind::DocumentOffset;
  // Canonical Bible book 1-66, stamped by the caller from biblebooknav.xhtml's
  // ordering. scanUnits cannot know it -- the book is a property of where the
  // document sits in the spine, not of its markup.
  uint8_t book = 0;
  std::vector<UnitAnchor> anchors;  // ascending by offset; empty for DocumentOffset
};

// Runs the verse scanner first and the paragraph scanner only if it found
// nothing.
DocumentUnits scanUnits(const char* xhtml, size_t length);

// Chunk-fed form, so a caller can stream a spine item through SpineHtmlStream
// instead of holding it whole -- Psalm 119 is ~69 KB of XHTML.
//
// Feeds BOTH scanners in one pass and applies precedence in take(). Two
// sequential passes would double the inflate cost for every Bible document,
// since all 1,189 verse-marked NWT documents also carry data-pid.
class UnitScanner {
 public:
  UnitScanner();
  ~UnitScanner();
  UnitScanner(const UnitScanner&) = delete;
  UnitScanner& operator=(const UnitScanner&) = delete;

  bool valid() const;
  bool feed(const char* chunk, size_t length, bool isFinal);
  DocumentUnits take();

 private:
  void* verse_ = nullptr;      // VerseAnchors::Scanner
  void* paragraph_ = nullptr;  // ParagraphAnchors::Scanner
};

// The unit containing `documentOffset`, with `Unit::offset` relative to that
// unit's start. Falls back to a DocumentOffset unit carrying the raw offset
// when the document has no units, or when the position precedes the first one.
Unit resolve(const DocumentUnits& units, uint32_t documentOffset);

// The document offset a unit starts at, for painting a resolved passage back
// onto the page. Returns nullopt when the unit is not in this document.
std::optional<uint32_t> documentOffsetOf(const DocumentUnits& units, const Unit& unit);

}  // namespace study
