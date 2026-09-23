#pragma once

#include <Epub.h>

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "StudyStore/ChapterCompletion.h"
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
  // study::UNLABELLED yields the passages marked but not yet labelled.
  std::vector<size_t> passagesWithTag(study::TagId id) const;

  // A tag's display name. UNLABELLED resolves to its translated label, and a
  // retired tag to its real name.
  std::string tagName(study::TagId id) const;

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

  // Where a passage's link target currently lives. nullopt is an honest "not in
  // this publication": the verse is not in this edition, or a Paragraph or
  // DocumentOffset target's document is gone or no longer the kind it was.
  // Same cost as locate(): a user action, never the page-turn path.
  std::optional<Location> locateLink(size_t passageIndex, size_t linkIndex);
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
  // dropped from the open publication's passages, leaving any passage that had
  // no other tag UNLABELLED. Refuses UNLABELLED itself. No passage is ever removed --
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

  // Records that the user paged off the end of `spineIndex`, marking the Bible
  // chapters it carries. Writes only when a chapter is newly marked -- about
  // once per chapter read, at a chapter boundary where the reader is already
  // loading the next section -- and never for a re-read. Every write is
  // synchronous for the same reason tag edits are: a debounced write would lose
  // the mark on Power-off.
  //
  // Main task only, with the reader's RenderLock held: the render task reaches
  // the same UnitIndexCache through passagesInDocument.
  study::CompletionMarkResult markDocumentRead(uint16_t spineIndex);

  // True exactly once per session, and only while the Bible is open, when its
  // completion record failed to load -- so the notice is not repeated on every
  // later book open, nor shown over a publication it has nothing to do with.
  bool takeCompletionLoadFailureNotice();

  // The passage marked as the source of the next link. Session state only,
  // never persisted: it is cleared when the publication closes (a link joins
  // two passages of the one open study file) and when any passage is removed
  // (removal shifts the index it holds). Linking leaves it marked, so one
  // passage can be linked to several in a row.
  void markLinkSource(size_t index);
  std::optional<size_t> linkSource() const { return linkSource_; }

  enum class LinkOutcome : uint8_t { Linked, AlreadyLinked, AtCap, SelfLink, NoSource, NotSaved };

  // Links the marked source to `targetIndex` and saves, rolling back on failure.
  LinkOutcome linkMarkedSourceTo(size_t targetIndex);
  bool removeLink(size_t passageIndex, size_t linkIndex);

  // For the migration runner, which owns its own save cadence.
  study::PassageDoc& mutableDoc() { return passages_; }
  study::TagPalette& mutablePalette() { return palette_; }
  bool save();

 private:
  StudyStore() = default;

  std::optional<Location> locateUnit(const study::Unit& unit, uint16_t spineHint);

  study::TagPalette palette_;
  study::PassageDoc passages_;
  std::string pubKey_;
  bool saveDisabled_ = false;
  std::optional<size_t> linkSource_;
  std::unique_ptr<UnitIndexCache> units_;

  // Held for the life of an open Bible: 149 bytes of static storage in this
  // singleton, no heap. Empty for any other publication.
  study::ChapterCompletion completion_;
  // Separate from saveDisabled_: an unreadable completion record must not stop
  // the user tagging passages, nor the reverse. Session-wide for the same
  // reason saveDisabled_ is.
  bool completionSaveDisabled_ = false;
  bool completionLoadFailureAnnounced_ = false;
};

#define STUDY StudyStore::getInstance()
