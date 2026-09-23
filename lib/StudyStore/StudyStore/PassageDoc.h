#pragma once

#include <ArduinoJson.h>

#include <string>
#include <vector>

#include "StudyStore/TaggedPassage.h"

// One publication's tagged passages, with all format rules and no storage
// access. The storage shell is src/study/PassageFile.
//
// Read through lib/JsonParser, never SDCardManager::readFile: that caps at
// 50,000 bytes and returns a silently truncated string, which for this file
// would mean the user's older passages simply cease to exist on the next boot.
namespace study {

class PassageDoc {
 public:
  // v2 added outgoing links ("k"). A file with no link is still written as v1,
  // because it holds nothing a v1 build would drop; one with a link is v2, which
  // a v1 build refuses outright instead of loading it, ignoring "k" and erasing
  // every link on its next save.
  static constexpr int FORMAT_VERSION = 2;
  static constexpr int LINKLESS_FORMAT_VERSION = 1;
  // Not persist::DEFAULT_SAVE_BUDGET: that figure exists to stay clear of
  // SDCardManager::readFile's 50,000-byte truncation, and this document is read
  // through a streaming parser with no such cap. The budget here bounds a single
  // write so a card-full or an absurd store is refused rather than half-written;
  // it is not a truncation guard.
  static constexpr size_t SAVE_BYTE_BUDGET = 200000;
  static constexpr size_t MAX_SNIPPET_BYTES = 120;
  static constexpr size_t MAX_REFERENCE_BYTES = 48;
  static constexpr size_t MAX_TAGS_PER_PASSAGE = 8;
  // With every field at its maximum, a passage carrying this many links still
  // measures under 1.7 KB, so SAVE_BYTE_BUDGET holds over a hundred of them --
  // the user's real store is 63 -- before it refuses anything.
  //
  // fromJson REFUSES a file with more links than this, or a link label longer
  // than MAX_REFERENCE_BYTES. Widening either one therefore needs a
  // FORMAT_VERSION bump, or an older build would refuse files this build wrote
  // under an unchanged version.
  static constexpr size_t MAX_LINKS_PER_PASSAGE = 8;

  const std::vector<TaggedPassage>& passages() const { return passages_; }

  // Normalises (UTF-8-safe truncation of snippet and reference, deduping and
  // capping tags, UNLABELLED for an empty list) and appends. Returns false when
  // adding it would exceed SAVE_BYTE_BUDGET.
  bool add(TaggedPassage passage);

  bool remove(size_t index);

  // Replaces entry `index`'s tags IN PLACE. An empty list leaves the passage
  // UNLABELLED -- untagging is not deleting. Returns false only when `index` is
  // out of range.
  bool setTags(size_t index, std::vector<TagId> tags);

  // Drops `id` from every passage in this document. Passages left with no tags
  // are KEPT as UNLABELLED, never deleted: the palette is global and
  // TagFilterActivity retires a tag on a long-press, so a destructive delete
  // would let one tidy-up gesture wipe work across every publication.
  void removeTagEverywhere(TagId id);

  size_t unlabelledCount() const;

  enum class LinkResult : uint8_t { Linked, AlreadyLinked, AtCap, SelfLink, OverBudget, NoSuchPassage };

  // Adds a link from `sourceIndex` to `targetIndex`'s start unit, labelled with
  // the target's reference (its snippet when it has none). Only the source
  // changes: links are directed, and there is no backlink to keep consistent.
  LinkResult linkPassages(size_t sourceIndex, size_t targetIndex);

  bool removeLink(size_t passageIndex, size_t linkIndex);

  // Replaces a passage's links in place, normalised as add() does -- the
  // rollback for a failed save, which must restore their order too.
  bool setLinks(size_t passageIndex, std::vector<PassageLink> links);

  // Updates a passage's stale spine hint after its address resolved in a
  // different document -- which happens when the publication is replaced by
  // another edition whose spine is laid out differently. The ADDRESS is
  // authoritative; documentSpine is only a shortcut for finding it again.
  void repairDocumentSpine(size_t index, uint16_t spineIndex);

  // Indices of passages whose start unit is in `document`, for the render pass.
  std::vector<size_t> findByDocument(const std::string& document) const;

  void toJson(JsonDocument& doc) const;

  // Parses and validates. Rejects a future format version, and any stored link
  // it would otherwise have to drop or cut (see MAX_LINKS_PER_PASSAGE). Returns
  // false when the parsed document exceeds the budget -- that is a load FAILURE
  // the caller must refuse to save over, never a silent truncation.
  bool fromJson(JsonVariantConst doc);

  size_t measureBytes() const;

 private:
  std::vector<TaggedPassage> passages_;
};

}  // namespace study
