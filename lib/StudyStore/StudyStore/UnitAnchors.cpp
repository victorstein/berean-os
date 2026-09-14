#include "StudyStore/UnitAnchors.h"

#include "ParagraphAnchors.h"
#include "VerseAnchors.h"

namespace study {

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

std::optional<uint32_t> documentOffsetOf(const DocumentUnits& units, const Unit& unit) {
  if (unit.kind == UnitKind::DocumentOffset) return unit.offset;
  if (unit.kind != units.kind) return std::nullopt;
  if (unit.kind == UnitKind::Verse && unit.book != units.book) return std::nullopt;
  for (const auto& a : units.anchors) {
    if (a.major == unit.major && a.minor == unit.minor) return a.offset + unit.offset;
  }
  return std::nullopt;
}

}  // namespace study
