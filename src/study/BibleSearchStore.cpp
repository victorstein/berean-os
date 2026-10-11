#include "BibleSearchStore.h"

#include <Arduino.h>
#include <Logging.h>
#include <Memory.h>
#include <esp_heap_caps.h>

#include <algorithm>
#include <cstring>
#include <vector>

#include "StudyStore/UnitIndexFormat.h"
#include "study/StudyStore.h"
#include "util/TaskWatchdog.h"

namespace {

constexpr const char* MODULE = "BSEARCH";
constexpr size_t STREAM_CHUNK_BYTES = 4096;
// A resume replays a checkpoint through tens of thousands of small reads. The
// loop task is not subscribed to the task watchdog today, so these resets only
// matter if it ever is; the real cost of the long passes is a stalled UI.
constexpr uint32_t READS_PER_WATCHDOG_RESET = 256;
constexpr uint32_t SPINE_READS_PER_WATCHDOG_RESET = 64;

void* psramAllocate(const size_t bytes) { return heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT); }

void psramRelease(void* block) { heap_caps_free(block); }

bool readFileAt(void* ctx, const uint32_t offset, void* dst, const uint32_t len) {
  static uint32_t readsSinceReset = 0;
  if (++readsSinceReset >= READS_PER_WATCHDOG_RESET) {
    readsSinceReset = 0;
    resetTaskWatchdogIfSubscribed();
  }
  auto* file = static_cast<HalFile*>(ctx);
  if (!file->seek(offset)) return false;
  return file->read(dst, len) == static_cast<int>(len);
}

class ScannerPrint final : public Print {
 public:
  explicit ScannerPrint(BibleSearch::VerseTextScanner& scanner) : scanner_(scanner) {}

  size_t write(const uint8_t b) override { return write(&b, 1); }
  size_t write(const uint8_t* buffer, const size_t size) override {
    if (rejected_) return 0;
    if (!scanner_.feed(reinterpret_cast<const char*>(buffer), size, false)) {
      rejected_ = true;
      return 0;
    }
    return size;
  }
  bool rejected() const { return rejected_; }

 private:
  BibleSearch::VerseTextScanner& scanner_;
  bool rejected_ = false;
};

// False when the file cannot be opened or is empty: a size of 0 would still
// fingerprint, as a Bible that is not there.
bool fileSizeOf(const std::string& path, uint64_t& size) {
  HalFile file;
  if (!Storage.openFileForRead(MODULE, path, file)) return false;
  size = file.fileSize64();
  return size > 0;
}

}  // namespace

BibleSearchStore& BibleSearchStore::getInstance() {
  static BibleSearchStore instance;
  return instance;
}

BibleSearch::BuildAllocator BibleSearchStore::psramAllocator() { return {psramAllocate, psramRelease}; }

BibleSearch::ByteSource BibleSearchStore::byteSourceFor(HalFile& file) {
  BibleSearch::ByteSource source;
  source.ctx = &file;
  source.readAt = readFileAt;
  source.size = static_cast<uint32_t>(file.fileSize());
  return source;
}

bool BibleSearchStore::identify(const std::shared_ptr<Epub>& epub) {
  if (!epub) return false;
  uint64_t epubSize = 0;
  if (!fileSizeOf(epub->getPath(), epubSize)) {
    LOG_ERR(MODULE, "Cannot size %s", epub->getPath().c_str());
    forgetBible();
    return false;
  }
  const auto spineCount = static_cast<uint32_t>(std::max(epub->getSpineItemsCount(), 0));
  if (identified_ && identity_.path == epub->getPath() && identity_.epubSize == epubSize &&
      identity_.spineCount == spineCount) {
    return true;
  }
  if (identified_) LOG_INF(MODULE, "Another Bible on the card; dropping what search kept");
  forgetBible();
  identity_ = Identity{epub->getPath(), epubSize, spineCount};
  identified_ = true;
  return true;
}

void BibleSearchStore::forgetBible() {
  close();
  identified_ = false;
  identity_ = Identity{};
  fingerprint_ = 0;
  fingerprintKnown_ = false;
  recordLoad_ = BibleSearchFingerprintFile::Load::Missing;
  recordLoaded_ = false;
  recordFingerprint_ = 0;
  documentsReady_ = false;
  documents_ = Documents{};
  documentsBuffer_.reset();
}

BibleSearchFingerprintFile::Key BibleSearchStore::recordKey() const {
  BibleSearchFingerprintFile::Key key;
  key.epubSize = identity_.epubSize;
  key.spineCount = identity_.spineCount;
  key.unitIndexVersion = study::UNIT_INDEX_VERSION;
  return key;
}

void BibleSearchStore::loadRecord(const std::shared_ptr<Epub>& epub) {
  BibleSearchFingerprintFile::Record record;
  recordLoad_ = BibleSearchFingerprintFile::load(epub->getCachePath(), recordKey(), record);
  recordLoaded_ = true;
  recordFingerprint_ = recordLoad_ == BibleSearchFingerprintFile::Load::Ok ? record.fingerprint : 0;
}

bool BibleSearchStore::fingerprintFor(const std::shared_ptr<Epub>& epub, uint64_t& out) {
  if (!identify(epub)) return false;
  if (fingerprintKnown_) {
    LOG_DBG(MODULE, "Fingerprint from memory");
    out = fingerprint_;
    return true;
  }

  [[maybe_unused]] const unsigned long started = millis();
  if (!recordLoaded_) loadRecord(epub);
  if (recordLoad_ == BibleSearchFingerprintFile::Load::Ok) {
    fingerprint_ = recordFingerprint_;
    fingerprintKnown_ = true;
    out = fingerprint_;
    LOG_DBG(MODULE, "Fingerprint from record in %lu ms", millis() - started);
    return true;
  }
  if (recordLoad_ != BibleSearchFingerprintFile::Load::Missing) {
    LOG_INF(MODULE, "Fingerprint record not used (load %d)", static_cast<int>(recordLoad_));
  }

  if (!resolveDocuments(epub)) return false;
  out = fingerprint_;
  LOG_DBG(MODULE, "Fingerprint computed in %lu ms", millis() - started);
  return true;
}

const BibleSearchStore::Documents* BibleSearchStore::documents(const std::shared_ptr<Epub>& epub) {
  if (!identify(epub)) return nullptr;
  if (documentsReady_) return &documents_;
  if (!resolveDocuments(epub)) return nullptr;
  return &documents_;
}

bool BibleSearchStore::resolveDocuments(const std::shared_ptr<Epub>& epub) {
  documentsReady_ = false;
  documents_ = Documents{};
  documentsBuffer_.reset();

  if (epub->getBibleBookNavSpineIndex() < 0) {
    LOG_ERR(MODULE, "%s is not an NWT-shaped Bible", epub->getPath().c_str());
    return false;
  }
  const int spineCount = epub->getSpineItemsCount();
  if (spineCount <= 0 || spineCount > UINT16_MAX) {
    LOG_ERR(MODULE, "Spine count %d out of range", spineCount);
    return false;
  }

  // Collected at spine-count size, then kept at the size actually used: the
  // NWT has 3,937 spine items and 1,189 verse documents.
  const size_t capacity = static_cast<size_t>(spineCount);
  BibleSearch::AllocatedBuffer scratch(psramAllocator(), capacity * (sizeof(uint16_t) + sizeof(uint8_t)));
  if (!scratch.get()) {
    LOG_ERR(MODULE, "OOM: document list for %d spine items", spineCount);
    return false;
  }
  auto* scratchSpines = scratch.as<uint16_t>();
  auto* scratchBooks = reinterpret_cast<uint8_t*>(scratchSpines + capacity);

  uint32_t count = 0;
  for (uint8_t book = 1; book <= BibleSearchFingerprintFile::BIBLE_BOOKS; book++) {
    const uint32_t before = count;
    for (const uint16_t spine : STUDY.spineIndicesForBook(book)) {
      if (count >= capacity) break;
      scratchSpines[count] = spine;
      scratchBooks[count] = book;
      count++;
    }
    // A book the map cannot resolve would silently be left out of the index.
    if (count == before) LOG_ERR(MODULE, "Book %u resolves to no verse documents", book);
    resetTaskWatchdogIfSubscribed();
  }
  if (count == 0) {
    LOG_ERR(MODULE, "No verse documents: the Bible's book map is unavailable");
    return false;
  }

  // Spines (u16) first, then books (u8): the block's alignment covers the u16s.
  if (!documentsBuffer_.allocate(psramAllocator(), count * (sizeof(uint16_t) + sizeof(uint8_t)))) {
    LOG_ERR(MODULE, "OOM: document list for %u verse documents", static_cast<unsigned>(count));
    return false;
  }
  auto* spines = documentsBuffer_.as<uint16_t>();
  auto* books = reinterpret_cast<uint8_t*>(spines + count);
  memcpy(spines, scratchSpines, count * sizeof(uint16_t));
  memcpy(books, scratchBooks, count);
  scratch.reset();

  // Device-written FAT timestamps are constant, so there is no mtime to hash.
  // The spine hrefs catch a replaced Bible whose file size happens to match.
  BibleSearchFingerprintFile::Fnv1a64 fingerprint;
  fingerprint.addU64(identity_.epubSize);
  fingerprint.addU32(static_cast<uint32_t>(spineCount));
  for (uint32_t i = 0; i < count; i++) {
    const std::string href = epub->getSpineItem(spines[i]).href;
    if (href.empty()) {
      LOG_ERR(MODULE, "Cannot read spine entry %u", spines[i]);
      documentsBuffer_.reset();
      return false;
    }
    fingerprint.add(href.data(), href.size() + 1);
    if ((i + 1) % SPINE_READS_PER_WATCHDOG_RESET == 0) resetTaskWatchdogIfSubscribed();
  }
  fingerprint.addU32(count);

  documents_.fingerprint = fingerprint.hash;
  documents_.count = count;
  documents_.spines = spines;
  documents_.books = books;
  documentsReady_ = true;
  fingerprint_ = fingerprint.hash;
  fingerprintKnown_ = true;
  LOG_INF(MODULE, "%u verse documents, fingerprint %08lx%08lx", static_cast<unsigned>(count),
          static_cast<unsigned long>(documents_.fingerprint >> 32), static_cast<unsigned long>(documents_.fingerprint));

  if (BibleSearchFingerprintFile::coversEveryBook(books, count)) {
    persistFingerprint(epub);
  } else {
    LOG_ERR(MODULE, "Not every book resolved; the fingerprint is kept for this boot only");
  }
  return true;
}

void BibleSearchStore::persistFingerprint(const std::shared_ptr<Epub>& epub) {
  if (!recordLoaded_) loadRecord(epub);
  if (recordLoad_ == BibleSearchFingerprintFile::Load::Ok && recordFingerprint_ == fingerprint_) return;
  if (!BibleSearchFingerprintFile::mayOverwrite(recordLoad_)) {
    LOG_INF(MODULE, "Fingerprint record left in place (load %d)", static_cast<int>(recordLoad_));
    return;
  }

  BibleSearchFingerprintFile::Record record;
  record.key = recordKey();
  record.verseDocuments = documents_.count;
  record.fingerprint = fingerprint_;
  if (!BibleSearchFingerprintFile::save(epub->getCachePath(), record)) {
    LOG_ERR(MODULE, "Fingerprint record not saved; the next boot recomputes it");
    return;
  }
  recordLoad_ = BibleSearchFingerprintFile::Load::Ok;
  recordFingerprint_ = fingerprint_;
}

BibleSearchStore::Status BibleSearchStore::status(const std::shared_ptr<Epub>& epub) {
  uint64_t fingerprint = 0;
  if (!fingerprintFor(epub, fingerprint)) return Status::Unreadable;

  BibleSearch::IndexReader reader(psramAllocator());
  Status indexStatus = Status::Missing;
  if (Storage.exists(INDEX_PATH)) {
    HalFile file;
    indexStatus = Storage.openFileForRead(MODULE, INDEX_PATH, file) ? reader.open(byteSourceFor(file), fingerprint)
                                                                    : Status::Unreadable;
  }
  if (indexStatus == Status::Ok) return indexStatus;

  // Whatever is wrong with bible.idx, a build confirmed now resumes from a
  // matching checkpoint, so that is what the prompt should say.
  if (Storage.exists(CHECKPOINT_PATH)) {
    HalFile file;
    if (Storage.openFileForRead(MODULE, CHECKPOINT_PATH, file) &&
        reader.open(byteSourceFor(file), fingerprint) == Status::Incomplete) {
      return Status::Incomplete;
    }
  }
  return indexStatus;
}

BibleSearchStore::Status BibleSearchStore::openIndexFile(const uint64_t fingerprint) {
  if (!Storage.exists(INDEX_PATH)) return Status::Missing;
  reader_ = makeUniqueNoThrow<BibleSearch::IndexReader>(psramAllocator());
  if (!reader_) {
    LOG_ERR(MODULE, "OOM: index reader");
    return Status::Unreadable;
  }
  if (!Storage.openFileForRead(MODULE, INDEX_PATH, indexFile_)) return Status::Unreadable;
  return reader_->open(byteSourceFor(indexFile_), fingerprint);
}

size_t BibleSearchStore::termIndexBytes() const {
  if (!reader_ || !reader_->termIndexCached()) return 0;
  return reader_->header().postingsOffset - reader_->header().termTableOffset;
}

const BibleSearch::IndexReader* BibleSearchStore::open(const std::shared_ptr<Epub>& epub, Status* statusOut) {
  close();
  uint64_t fingerprint = 0;
  const bool identified = fingerprintFor(epub, fingerprint);
  [[maybe_unused]] const unsigned long started = millis();
  const Status result = identified ? openIndexFile(fingerprint) : Status::Unreadable;
  if (statusOut) *statusOut = result;

  if (result != Status::Ok) {
    LOG_ERR(MODULE, "Index not usable (status %d)", static_cast<int>(result));
    close();
    return nullptr;
  }
  if (!reader_->cacheTermIndex()) {
    // Still correct, only slower: lookups then read the term table from the card.
    LOG_ERR(MODULE, "Term index not cached; lookups will read the card");
  }
  LOG_DBG(MODULE, "Index opened in %lu ms (term index %u bytes)", millis() - started,
          static_cast<unsigned>(termIndexBytes()));
  return reader_.get();
}

void BibleSearchStore::close() {
  reader_.reset();
  if (indexFile_.isOpen()) indexFile_.close();
}

bool BibleSearchStore::scanSpine(const Epub& epub, const uint16_t spine, BibleSearch::VerseTextScanner& scanner,
                                 bool& malformed) {
  malformed = false;
  if (!scanner.valid()) {
    LOG_ERR(MODULE, "OOM: verse text scanner");
    return false;
  }
  const std::string href = epub.getSpineItem(spine).href;
  if (href.empty()) {
    LOG_ERR(MODULE, "Cannot read spine entry %u", spine);
    return false;
  }

  ScannerPrint sink(scanner);
  const bool streamed = epub.readItemContentsToStream(href, sink, STREAM_CHUNK_BYTES);
  if (sink.rejected() || (streamed && !scanner.feed("", 0, true))) {
    if (scanner.outOfMemory()) {
      LOG_ERR(MODULE, "Out of memory parsing spine %u (%s)", spine, href.c_str());
      return false;
    }
    LOG_ERR(MODULE, "Spine %u (%s) is not well-formed verse markup", spine, href.c_str());
    malformed = true;
    return false;
  }
  if (!streamed) {
    LOG_ERR(MODULE, "Cannot stream spine %u (%s)", spine, href.c_str());
    return false;
  }
  return true;
}

bool BibleSearchStore::verseTexts(const Epub& epub, const uint16_t spine, const BibleSearch::VerseEntry* wanted,
                                  const size_t count, const VerseTextSink sink, void* ctx) {
  BibleSearch::VerseTextScanner scanner;
  bool malformed = false;
  if (!scanSpine(epub, spine, scanner, malformed)) return false;

  const std::vector<BibleSearch::VerseText> verses = scanner.take();
  for (size_t i = 0; i < count; i++) {
    const auto match = std::find_if(verses.begin(), verses.end(), [&](const BibleSearch::VerseText& verse) {
      return verse.chapter == wanted[i].chapter && verse.verse == wanted[i].verse;
    });
    if (match != verses.end()) sink(ctx, i, match->text);
  }
  return true;
}
