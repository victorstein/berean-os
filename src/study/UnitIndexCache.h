#pragma once

#include <Epub.h>

#include <memory>
#include <string>
#include <vector>

#include "StudyStore/UnitIndexFormat.h"
#include "study/MigrationProgress.h"

class GfxRenderer;

// Lazily indexes one publication's documents, one document at a time, on first
// use of that document.
//
// Never indexes eagerly. The NWT's 3,937 documents at ~30 ms each is about two
// minutes, and CONFIG_ESP_TASK_WDT_PANIC=y with a 5 s timeout means any
// uninterrupted run past 5 s panics -- during a boot-time pass that reboots
// into the same pass.
//
// Owned by the main task. Every SD access goes through HalStorage, which holds
// storageMutex; SdFat is not thread-safe and the web server task also writes.
class UnitIndexCache {
 public:
  UnitIndexCache(std::shared_ptr<Epub> epub, std::string pubKey, GfxRenderer& renderer);

  // Prepares the index file. Cheap: reads or writes only the header and the
  // fixed-size table, and scans no document.
  bool begin();

  // The units for `spineIndex`, building and persisting them if absent, stale
  // or corrupt. Returns a DocumentOffset-kind result when the document has no
  // units or the build fails -- readable, addressing degraded, never fatal.
  const study::DocumentUnits& unitsFor(uint16_t spineIndex);

  // Canonical Bible book for a spine index, or 0. Built once from
  // biblebooknav.xhtml AND the 66 chapter-nav pages it points at -- the nav page
  // alone yields only the nav pages, not the chapters -- then persisted, so that
  // cost is paid once per publication and never at open.
  uint8_t bookFor(uint16_t spineIndex);

  // Spine indices belonging to a canonical Bible book, from the persisted map.
  // Empty outside a Bible, or before the map has been built. Used to find a
  // passage whose stored spine hint no longer matches the publication on the
  // card -- a book has at most 150 chapters, so this bounds that search.
  std::vector<uint16_t> spineIndicesForBook(uint8_t book);

  // Visible text of a unit in `spineIndex`, for a fingerprint. Streams the
  // document; does not cache it.
  std::string unitText(uint16_t spineIndex, const study::Unit& unit);

  // Reported during the one-off book-map build, which is the only part of this
  // class slow enough for a user to notice.
  void setProgress(const MigrationProgress& progress) { progress_ = progress; }

  bool ready() const { return ready_; }
  uint16_t indexedDocumentCount() const { return header_.documentCount; }

  // True when unitsFor(spineIndex) returned a placeholder because the document
  // could not be indexed. The placeholder is DocumentOffset-kind with no
  // anchors, which is indistinguishable from a real DocumentOffset document.
  bool indexFailed(const uint16_t spineIndex) const { return cachedSpine_ == spineIndex && cachedBuildFailed_; }
  const std::string& pubKey() const { return pubKey_; }

 private:
  std::string indexPath() const;
  bool readEntry(uint16_t spineIndex, study::UnitIndexEntry& out) const;
  bool writeEntry(uint16_t spineIndex, const study::UnitIndexEntry& entry) const;
  bool loadAnchors(const study::UnitIndexEntry& entry, study::DocumentUnits& out) const;
  bool buildDocument(uint16_t spineIndex);
  bool buildBookMap();

  std::shared_ptr<Epub> epub_;
  std::string pubKey_;
  GfxRenderer& renderer_;

  bool ready_ = false;
  // 66 books, and the spine sweep that resolves filenames is O(spine x names) --
  // so a batch trades a little RAM for far fewer sweeps. 192 names is ~12 KB of
  // std::string and cuts the NWT's ~1,189 chapter links to about six sweeps.
  static constexpr size_t RESOLVE_BATCH = 192;
  static constexpr size_t MAX_BIBLE_BOOKS = 66;

  MigrationProgress progress_;
  bool bookMapBuilt_ = false;
  study::UnitIndexHeader header_;

  // Exactly one document is held in RAM. Measured over 180 real documents the
  // anchor count is median 55, p95 111, max 413 -- so at most ~3.3 KB, and the
  // access pattern is one document per page turn.
  uint16_t cachedSpine_ = UINT16_MAX;
  study::DocumentUnits cached_;
  bool cachedBuildFailed_ = false;
};
