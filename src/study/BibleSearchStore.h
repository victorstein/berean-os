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

// The Bible search index on the card: /.berean/search/bible.idx, and the build
// checkpoint bible.partial beside it. Both are derived data -- losing them costs
// a rebuild, never anything the user made.
//
// This class only reads. BibleSearchIndexer writes, and only after the user
// confirms a build, so an unreadable or newer-format index stays on the card
// until they do.
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

  // Resolves the Bible's verse documents and fingerprint, once per publication
  // file: the book map plus one spine-entry read per verse document. Null when
  // `epub` is not the Bible that StudyStore has open, or cannot be sized. A
  // call for a different file frees the previous list, so a caller that holds
  // on to one copies it.
  const Documents* documents(const std::shared_ptr<Epub>& epub);

  // Reads the index header only, and never writes. An index that is not Ok --
  // missing, stale, unreadable or newer -- reports Incomplete when a checkpoint
  // for this Bible exists, so the prompt can say the build will resume.
  Status status(const std::shared_ptr<Epub>& epub);

  // A reader with the term index cached, kept open until close(). Null unless
  // the index is Ok; `statusOut` then says why.
  const BibleSearch::IndexReader* open(const std::shared_ptr<Epub>& epub, Status* statusOut = nullptr);
  // Frees the reader, its term cache and its file handle.
  void close();

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
  BibleSearchStore() = default;

  bool resolveDocuments(const std::shared_ptr<Epub>& epub, uint64_t epubSize);

  std::string documentsPath_;
  uint64_t documentsEpubSize_ = 0;
  bool documentsReady_ = false;
  Documents documents_;
  BibleSearch::AllocatedBuffer documentsBuffer_;

  HalFile indexFile_;
  std::unique_ptr<BibleSearch::IndexReader> reader_;
};
