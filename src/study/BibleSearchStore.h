#pragma once

#include <BibleSearch/Allocator.h>
#include <BibleSearch/IndexFormat.h>
#include <BibleSearch/IndexReader.h>
#include <BibleSearch/VerseTextScanner.h>
#include <Epub.h>
#include <HalStorage.h>
#include <SdPaths.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

#include "study/BibleSearchFingerprintFile.h"

class BibleBookNameTable;

// The Bible search index on the card: /.berean/search/bible.idx, and the build
// checkpoint bible.partial beside it. Both are derived data -- losing them costs
// a rebuild, never anything the user made.
//
// This class only reads the index. BibleSearchIndexer writes it, and only after
// the user confirms a build, so an unreadable or newer-format index stays on the
// card until they do.
//
// The fingerprint that ties an index to its Bible is kept in memory for the Bible
// on the card -- its path, size and spine count -- and in search-fingerprint.bin
// in that Bible's cache directory, so a boot does not re-resolve every verse
// document to find it.
//
// Between visits to the search screen it also keeps, for the same Bible, the
// opened reader -- its ~444 KB term index and 4 KB posting page, in PSRAM -- and
// a copy of the book names. park() closes the index file and keeps them while
// PSRAM allows; the next open() re-reads the index header and reuses the reader
// only if the file is unchanged. Another Bible, close() (a rebuild or a failed
// query) or a park below the PSRAM floor drops them; sleep is a chip reset.
//
// Main task only. Every SD access goes through HalStorage, which holds
// storageMutex per call; the web server task is a second writer to the card.
// Nothing here may be reached from render(): a read would put storageMutex on
// the render path.
class BibleSearchStore {
 public:
  using Status = BibleSearch::IndexReader::Status;

  static constexpr const char* SEARCH_DIR = sdpaths::SEARCH_DIR;
  static constexpr const char* INDEX_PATH = sdpaths::SEARCH_INDEX_FILE;
  static constexpr const char* CHECKPOINT_PATH = sdpaths::SEARCH_CHECKPOINT_FILE;
  // The NWT's index is ~1.47 MB. The budget bounds a runaway build, not the
  // 50,000-byte readFile cap: this file is only ever read by offset.
  static constexpr size_t INDEX_BYTE_BUDGET = 8u * 1024u * 1024u;

  // The verse-bearing spine documents in canonical order -- book 1 to 66, each
  // book's documents in spine order -- with the book of each. The fingerprint
  // covers this list, so a checkpoint's docsDone indexes the same list it was
  // written against.
  struct Documents {
    uint64_t fingerprint = 0;
    uint32_t count = 0;
    const uint16_t* spines = nullptr;
    const uint8_t* books = nullptr;
  };

  static BibleSearchStore& getInstance();

  // Where the build's and the reader's large blocks come from. PSRAM only: a
  // board without it gets a clean OOM rather than internal SRAM starved of the
  // megabytes a build holds.
  static BibleSearch::BuildAllocator psramAllocator();

  // Resolves the Bible's verse documents and fingerprint for real, once per Bible
  // (path, size and spine count): the book map plus one spine-entry read per verse
  // document. Never served from the fingerprint record. Null when `epub` is not
  // the Bible that StudyStore has open, or cannot be sized. A call for a different
  // Bible frees the previous list, so a caller that holds on to one copies it.
  const Documents* documents(const std::shared_ptr<Epub>& epub);

  // Reads the index header only, and never writes the index. An index that is not
  // Ok -- missing, stale, unreadable or newer -- reports Incomplete when a
  // checkpoint for this Bible exists, so the prompt can say the build will resume.
  Status status(const std::shared_ptr<Epub>& epub);

  // A reader with the term index cached, kept until close() or a release. Null
  // unless the index is Ok; `statusOut` then says why.
  const BibleSearch::IndexReader* open(const std::shared_ptr<Epub>& epub, Status* statusOut = nullptr);
  // Frees the reader, its term cache and its file handle. Kept book names stay:
  // they belong to the Bible, not to the index.
  void close();
  // The search screen is leaving: closes the index file, and keeps the reader and
  // the book names for the next open() unless PSRAM is below the retention floor.
  void park();

  // Copies the book names kept for the Bible that the last open() or status()
  // identified into `out`. False when none are kept for it.
  bool restoreBookNames(const std::shared_ptr<Epub>& epub, BibleBookNameTable& out) const;
  // Keeps a copy of `names` for the Bible that the last open() or status()
  // identified. Neither call sizes the EPUB again.
  void keepBookNames(const std::shared_ptr<Epub>& epub, const BibleBookNameTable& names);

  // Streams one spine document through a VerseTextScanner. `malformed` tells a
  // document the scanner rejected from one that could not be read; running out
  // of memory is neither, and scanner.outOfMemory() reports it.
  static bool scanSpine(const Epub& epub, uint16_t spine, BibleSearch::VerseTextScanner& scanner, bool& malformed);

  using VerseTextSink = void (*)(void* ctx, size_t wantedIndex, std::string_view text);

  // Streams `spine` once and hands `sink` the visible text of each wanted
  // verse, matched by chapter:verse. A verse not found in the document is not
  // reported. False when the document cannot be read.
  static bool verseTexts(const Epub& epub, uint16_t spine, const BibleSearch::VerseEntry* wanted, size_t count,
                         VerseTextSink sink, void* ctx);

  // A ByteSource over an open HalFile, for the reader and the checkpoint load.
  static BibleSearch::ByteSource byteSourceFor(HalFile& file);

 private:
  struct Identity {
    std::string path;
    uint64_t epubSize = 0;
    uint32_t spineCount = 0;
  };

  BibleSearchStore() = default;

  // Sizes `epub` and makes it the Bible everything here is kept for, dropping
  // whatever was kept for another. False, with nothing kept, when it cannot be
  // sized.
  bool identify(const std::shared_ptr<Epub>& epub);
  bool identifiedAs(const std::shared_ptr<Epub>& epub) const;
  void forgetBible();
  // Re-opens bible.idx under the held reader. True when its header and size are
  // the ones the reader was opened on.
  bool resumeHeldReader();
  void releaseResident();
  // From memory, else from the record, else by resolving the documents.
  bool fingerprintFor(const std::shared_ptr<Epub>& epub, uint64_t& out);
  bool resolveDocuments(const std::shared_ptr<Epub>& epub);
  BibleSearchFingerprintFile::Key recordKey() const;
  void loadRecord(const std::shared_ptr<Epub>& epub);
  // The only writer of the record. Called after a resolve that covered every
  // book; leaves a record that is newer or could not be read where it is.
  void persistFingerprint(const std::shared_ptr<Epub>& epub);
  Status openIndexFile(uint64_t fingerprint);
  size_t termIndexBytes() const;

  Identity identity_;
  bool identified_ = false;
  uint64_t fingerprint_ = 0;
  bool fingerprintKnown_ = false;
  // The record as last loaded for this Bible; reset whenever the Bible changes.
  BibleSearchFingerprintFile::Load recordLoad_ = BibleSearchFingerprintFile::Load::Missing;
  bool recordLoaded_ = false;
  uint64_t recordFingerprint_ = 0;

  bool documentsReady_ = false;
  Documents documents_;
  BibleSearch::AllocatedBuffer documentsBuffer_;

  // The held reader's source. Closed while parked: the reader must not be read
  // until resumeHeldReader() has re-opened and re-checked it.
  HalFile indexFile_;
  std::unique_ptr<BibleSearch::IndexReader> reader_;
  BibleSearch::AllocatedBuffer names_;
};
