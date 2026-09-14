#pragma once

#include <optional>
#include <string>
#include <vector>

#include "StudyStore/TaggedPassage.h"
#include "StudyStore/UnitAnchors.h"

// Decides what one legacy highlight becomes. Pure: no storage, no EPUB, no
// Arduino, so the whole migration is testable on the host.
namespace study {

// A HighlightEntry as it exists in /.crosspoint/highlights/, flattened so this
// header does not depend on the old model.
struct LegacyHighlight {
  uint16_t spineIndex = 0;
  uint32_t start = 0;
  uint32_t end = 0;
  std::string snippet;    // the old `text`
  std::string reference;  // the old `ref`
  std::vector<std::string> tagNames;
};

enum class MigrationOutcome : uint8_t {
  Resolved,                   // addressed to a Verse or Paragraph unit
  ResolvedReferenceMismatch,  // addressed, but disagrees with the stored ref
  ResolvedDocumentOffset,     // the document has no units; kept the raw offset
  PendingUpgrade,             // the EPUB was unavailable; upgrade on next open
  DroppedNoTags,              // carried no tags; nothing to preserve
};

struct MigrationInputs {
  std::string pubKey;
  std::string document;
  DocumentUnits units;
  bool sourceAvailable = true;
  TagPalette* palette = nullptr;  // when set, tag names become global ids
  // Visible text of a unit, for the fingerprint -- a context pointer and a plain
  // function pointer, not std::function: CLAUDE.md prohibits std::function in
  // library code (~2-4 KB per signature plus a heap-allocated closure) and the
  // pair costs nothing here.
  void* unitTextCtx = nullptr;
  std::string (*unitText)(void* ctx, const Unit&) = nullptr;
};

struct MigrationResult {
  std::optional<TaggedPassage> passage;
  MigrationOutcome outcome = MigrationOutcome::DroppedNoTags;
  bool referenceAgrees = true;
  std::string resolvedReference;  // "119:145", what the address actually says
};

MigrationResult planMigration(const MigrationInputs& in, const LegacyHighlight& legacy);

// Registers every name in the palette, so a tag the user defined but never
// applied survives the migration. The user has two of these.
void adoptTagNames(TagPalette& palette, const std::vector<std::string>& names);

// "Salmos 119:145" -> "119:145"; "" when there is no chapter:verse tail.
std::string referenceTail(const std::string& reference);

}  // namespace study
