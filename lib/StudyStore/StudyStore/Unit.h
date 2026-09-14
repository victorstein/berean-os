#pragma once

#include <cstdint>
#include <optional>
#include <string>

// Where a tagged passage lives, independent of byte positions in any one file.
//
// A Verse unit carries `book` because the Bible's pubkey is language-free, and
// the only other thing naming the book is the document filename -- which is
// language-SPECIFIC (the Spanish NWT's Matthew is 1001061105-split*.xhtml, the
// English one's is not). Without `book`, "Salmos 119:145 and Psalm 119:145 are
// the same verse" is not expressible. The number is the position in
// biblebooknav.xhtml, which lists the 66 books in canonical order in every
// language (BibleNavScanner.h).
//
// A document supports exactly one kind, chosen by UnitAnchors: Verse where the
// document carries chapter<N>_verse<M> markers, Paragraph where it carries
// data-pid, DocumentOffset otherwise. Measured on the NWT: 1,189 documents are
// Verse, 153 Paragraph, 2,595 DocumentOffset.
namespace study {

enum class UnitKind : uint8_t { DocumentOffset = 0, Paragraph = 1, Verse = 2 };

struct Unit {
  UnitKind kind = UnitKind::DocumentOffset;
  uint8_t book = 0;     // canonical Bible book 1-66 for Verse; 0 otherwise
  uint16_t major = 0;   // chapter for Verse; 0 otherwise
  uint16_t minor = 0;   // verse for Verse; data-pid for Paragraph; 0 otherwise
  uint32_t offset = 0;  // codepoints into the unit, or into the document

  bool operator==(const Unit&) const = default;
};

// Verse units carry a real sequence; Paragraph units do not, because data-pid
// is assigned by JW's content system and runs out of document order in 46% of
// the documents that carry it. Callers that need paragraph order must consult
// the anchor list's offsets instead.
bool orderableByAddress(const Unit& a, const Unit& b);

bool operator<(const Unit& a, const Unit& b);

// "v:19:119:145:3" (Psalms 119:145 +3), "p:0:0:40:0", "d:0:0:0:1255" --
// kind:book:major:minor:offset, stable across format versions and cheap to
// eyeball in a migration report.
std::string unitToCompact(const Unit& u);
std::optional<Unit> unitFromCompact(const std::string& s);

}  // namespace study
