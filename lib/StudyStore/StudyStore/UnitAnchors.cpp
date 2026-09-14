#include "StudyStore/UnitAnchors.h"

#include <cstdint>
#include <new>

#include "Epub/ParagraphAnchors.h"
#include "Epub/VerseAnchors.h"

namespace study {
namespace {

VerseAnchors::Scanner* verseOf(void* p) { return static_cast<VerseAnchors::Scanner*>(p); }
ParagraphAnchors::Scanner* paragraphOf(void* p) { return static_cast<ParagraphAnchors::Scanner*>(p); }

}  // namespace

UnitScanner::UnitScanner() {
  auto* verse = new (std::nothrow) VerseAnchors::Scanner();
  auto* paragraph = new (std::nothrow) ParagraphAnchors::Scanner();
  if (!verse || !paragraph || !verse->valid() || !paragraph->valid()) {
    delete verse;
    delete paragraph;
    return;
  }
  verse_ = verse;
  paragraph_ = paragraph;
}

UnitScanner::~UnitScanner() {
  delete verseOf(verse_);
  delete paragraphOf(paragraph_);
}

bool UnitScanner::valid() const { return verse_ != nullptr && paragraph_ != nullptr; }

bool UnitScanner::feed(const char* chunk, const size_t length, const bool isFinal) {
  if (!valid()) return false;
  // Both must see every chunk, and neither failing stops the other: a document
  // that trips one parser may still be readable by the other.
  const bool verseOk = verseOf(verse_)->feed(chunk, length, isFinal);
  const bool paragraphOk = paragraphOf(paragraph_)->feed(chunk, length, isFinal);
  return verseOk || paragraphOk;
}

DocumentUnits UnitScanner::take() {
  DocumentUnits out;
  if (!valid()) return out;

  const auto verses = verseOf(verse_)->take();
  if (!verses.empty()) {
    out.kind = UnitKind::Verse;
    out.anchors.reserve(verses.size());
    for (const auto& v : verses) out.anchors.push_back({v.offset, v.chapter, v.verse});
    return out;
  }

  const auto paragraphs = paragraphOf(paragraph_)->take();
  if (!paragraphs.empty()) {
    out.kind = UnitKind::Paragraph;
    out.anchors.reserve(paragraphs.size());
    for (const auto& p : paragraphs) out.anchors.push_back({p.offset, 0, p.pid});
  }
  return out;
}

DocumentUnits scanUnits(const char* xhtml, const size_t length) {
  DocumentUnits out;

  const auto verses = VerseAnchors::scan(xhtml, length);
  if (!verses.empty()) {
    out.kind = UnitKind::Verse;
    out.anchors.reserve(verses.size());
    for (const auto& v : verses) out.anchors.push_back({v.offset, v.chapter, v.verse});
    return out;
  }

  const auto paragraphs = ParagraphAnchors::scan(xhtml, length);
  if (!paragraphs.empty()) {
    out.kind = UnitKind::Paragraph;
    out.anchors.reserve(paragraphs.size());
    for (const auto& p : paragraphs) out.anchors.push_back({p.offset, 0, p.pid});
    return out;
  }

  return out;
}

Unit resolve(const DocumentUnits& units, const uint32_t documentOffset) {
  const UnitAnchor* best = nullptr;
  for (const auto& a : units.anchors) {
    if (a.offset > documentOffset) break;
    best = &a;
  }
  if (!best) return Unit{UnitKind::DocumentOffset, 0, 0, 0, documentOffset};
  // `book` is meaningful only for a Verse unit -- Unit.h says "0 otherwise",
  // and stamping it on a Paragraph would also change how the passage is filed.
  const uint8_t book = units.kind == UnitKind::Verse ? units.book : 0;
  return Unit{units.kind, book, best->major, best->minor, documentOffset - best->offset};
}

size_t anchorIndexOf(const DocumentUnits& units, const Unit& unit) {
  for (size_t i = 0; i < units.anchors.size(); ++i) {
    if (units.anchors[i].major == unit.major && units.anchors[i].minor == unit.minor) return i;
  }
  return SIZE_MAX;
}

uint32_t unitEndOffset(const DocumentUnits& units, const size_t anchorIndex) {
  if (anchorIndex + 1 < units.anchors.size()) return units.anchors[anchorIndex + 1].offset;
  return UINT32_MAX;
}

std::optional<uint32_t> documentOffsetOf(const DocumentUnits& units, const Unit& unit) {
  if (unit.kind == UnitKind::DocumentOffset) return unit.offset;
  if (unit.kind != units.kind) return std::nullopt;
  if (unit.kind == UnitKind::Verse && unit.book != units.book) return std::nullopt;
  const size_t index = anchorIndexOf(units, unit);
  if (index == SIZE_MAX) return std::nullopt;
  return units.anchors[index].offset + unit.offset;
}

}  // namespace study
