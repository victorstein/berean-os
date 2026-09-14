#pragma once

#include <Epub.h>

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "StudyStore/PassageDoc.h"
#include "StudyStore/TagPalette.h"
#include "study/UnitIndexCache.h"

class GfxRenderer;

// The device's study data for the publication currently open: the global tag
// palette plus that publication's tagged passages.
//
// Passages are addressed BY INDEX, with the caller re-checking against
// passages().size() after any mutation. Sub-activities mutate the document while
// a caller holds a position -- TagPickerActivity is entered and exited mid-edit
// -- so a pointer or reference would dangle. The model this replaces made the
// same choice for the same reason.
//
// Saves are synchronous and roll back on failure. A debounced save would lose an
// edit on Power-off and leave the screen showing a tag that was never persisted;
// tag edits happen a handful of times per session, not per page turn.
class StudyStore {
 public:
  static StudyStore& getInstance();

  // Called when a book opens. Resolves the pubkey, loads the palette and the
  // passages, and prepares the unit index. Scans no document.
  bool openPublication(const std::shared_ptr<Epub>& epub, GfxRenderer& renderer);
  void closePublication();

  const study::TagPalette& palette() const { return palette_; }

  // Active tags in allocation order, paired with their names, for a picker or a
  // filter row. Returned by value: the caller builds a row list from it and the
  // palette may be edited underneath while that list is on screen.
  struct TagView {
    study::TagId id;
    std::string name;
  };
  std::vector<TagView> activeTags() const;

  // Indices of passages carrying `id`, in document order, for the filter.
  std::vector<size_t> passagesWithTag(study::TagId id) const;

  // Comma-joined names of the tags a passage carries, for a list row. Retired
  // tags still resolve -- a passage that carries one must not render a blank.
  std::string tagNamesFor(size_t passageIndex) const;

  struct Location {
    uint16_t spineIndex;
    uint32_t offset;
  };

  // Where a passage currently lives, for jumping to it. Tries the stored spine
  // hint first; if that no longer holds -- the publication was replaced by an
  // edition laid out differently -- a Verse address is searched for within its
  // own book, which bounds the scan to at most 150 documents.
  //
  // Builds unit indexes as it goes, so this is a user-initiated action, never
  // the page-turn path.
  std::optional<Location> locate(size_t passageIndex);
  const std::vector<study::TaggedPassage>& passages() const { return passages_.passages(); }
  const std::string& pubKey() const { return pubKey_; }
  bool isOpen() const { return units_ != nullptr; }

  // True when a load failed and the file may still hold the user's data. Every
  // save is refused while this is set, for the life of the session.
  bool saveDisabled() const { return saveDisabled_; }

  // Adds a tag name, or returns the existing id. The palette is persisted before
  // the id is handed out, so an id can never be in use but unrecorded.
  std::optional<study::TagId> addTagName(const std::string& name);

  // RETIRES the tag: it leaves the pickers, keeps resolving for display, and is
  // dropped from the open publication's passages. No passage is ever removed --
  // the palette is global and a long-press in TagFilterActivity reaches it, so a
  // destructive delete would let one gesture wipe work across every publication.
  bool retireTag(study::TagId id);

  struct PaintedPassage {
    size_t index;
    uint32_t startOffset;
    uint32_t endOffset;
    // The stored offsets no longer fit the unit; paint the whole unit instead.
    bool wholeUnit;
  };

  // Passages in this spine document, resolved to current document offsets.
  // Returns nothing for a passage whose fingerprint no longer matches: the text
  // it was attached to is not the text that is there, so painting it would mark
  // words the user never marked.
  std::vector<PaintedPassage> passagesInDocument(uint16_t spineIndex);

  bool addPassage(uint16_t spineIndex, uint32_t startOffset, uint32_t endOffset, const std::string& snippet,
                  const std::string& reference, std::vector<study::TagId> tags);
  bool removePassage(size_t index);
  bool setPassageTags(size_t index, std::vector<study::TagId> tags);

  // For the migration runner, which owns its own save cadence.
  study::PassageDoc& mutableDoc() { return passages_; }
  study::TagPalette& mutablePalette() { return palette_; }
  bool save();

 private:
  StudyStore() = default;

  study::TagPalette palette_;
  study::PassageDoc passages_;
  std::string pubKey_;
  bool saveDisabled_ = false;
  std::unique_ptr<UnitIndexCache> units_;
};

#define STUDY StudyStore::getInstance()
