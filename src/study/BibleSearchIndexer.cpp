#include "BibleSearchIndexer.h"

#include <Arduino.h>
#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>
#include <esp_heap_caps.h>

#include <string>
#include <utility>
#include <vector>

#include "util/TaskWatchdog.h"

namespace {

constexpr const char* MODULE = "BSINDEX";

// The builder batches its output into 4 KB blocks from the PSRAM allocator, so
// each call here is one sector-sized write, not one per record. A 1.47 MB index
// is a few hundred of them, which together outlast the watchdog.
bool writeToFile(void* ctx, const void* data, const size_t length) {
  resetTaskWatchdogIfSubscribed();
  return static_cast<HalFile*>(ctx)->write(data, length) == length;
}

}  // namespace

BibleSearchIndexer::BibleSearchIndexer(std::shared_ptr<Epub> epub) : epub_(std::move(epub)) {}

bool BibleSearchIndexer::fail(const Failure failure) {
  failure_ = failure;
  builder_.reset();
  logMemory("failed");
  return false;
}

bool BibleSearchIndexer::begin() {
  builder_.reset();
  docs_ = nullptr;
  docsDone_ = 0;
  totalDocs_ = 0;
  lastCheckpointDocs_ = 0;
  skippedDocs_ = 0;
  currentBook_ = 0;
  currentChapter_ = 0;
  finished_ = false;
  failure_ = Failure::None;

  BibleSearchStore& store = BibleSearchStore::getInstance();
  store.close();
  docs_ = store.documents(epub_);
  if (!docs_ || docs_->count == 0) {
    LOG_ERR(MODULE, "No verse documents to index");
    return fail(Failure::NotABible);
  }
  totalDocs_ = docs_->count;

  builder_ = makeUniqueNoThrow<BibleSearch::IndexBuilder>(BibleSearchStore::psramAllocator());
  if (!builder_) {
    LOG_ERR(MODULE, "OOM: index builder");
    return fail(Failure::OutOfMemory);
  }
  logMemory("begin");
  return resumeFromCheckpoint();
}

bool BibleSearchIndexer::resumeFromCheckpoint() {
  if (!Storage.exists(BibleSearchStore::CHECKPOINT_PATH)) return true;

  uint32_t done = 0;
  bool resumed = false;
  {
    HalFile file;
    if (Storage.openFileForRead(MODULE, BibleSearchStore::CHECKPOINT_PATH, file)) {
      resumed = builder_->loadCheckpoint(BibleSearchStore::byteSourceFor(file), docs_->fingerprint, done) &&
                done <= totalDocs_;
    }
  }

  if (resumed) {
    docsDone_ = done;
    lastCheckpointDocs_ = done;
    if (done > 0) currentBook_ = docs_->books[done - 1];
    LOG_INF(MODULE, "Resuming at document %u of %u (%u verses, %u terms)", static_cast<unsigned>(done),
            static_cast<unsigned>(totalDocs_), static_cast<unsigned>(builder_->verseCount()),
            static_cast<unsigned>(builder_->termCount()));
    return true;
  }

  // A checkpoint for another Bible, an older build, or a torn one: start over.
  // A failed load may have left the builder half-filled, so it is replaced.
  LOG_INF(MODULE, "Checkpoint not usable for this Bible; starting over");
  builder_ = makeUniqueNoThrow<BibleSearch::IndexBuilder>(BibleSearchStore::psramAllocator());
  if (!builder_) {
    LOG_ERR(MODULE, "OOM: index builder");
    return fail(Failure::OutOfMemory);
  }
  return true;
}

bool BibleSearchIndexer::step() {
  if (!builder_ || failed() || allDocumentsIndexed()) return false;

  [[maybe_unused]] const unsigned long started = millis();
  const uint16_t spine = docs_->spines[docsDone_];
  const uint8_t book = docs_->books[docsDone_];

  BibleSearch::VerseTextScanner scanner;
  if (!scanner.valid()) {
    LOG_ERR(MODULE, "OOM: verse text scanner");
    return fail(Failure::OutOfMemory);
  }
  bool malformed = false;
  if (BibleSearchStore::scanSpine(*epub_, spine, scanner, malformed)) {
    if (!indexDocument(scanner, spine, book)) {
      LOG_ERR(MODULE, "Builder failed at spine %u (%u verses)", spine, static_cast<unsigned>(builder_->verseCount()));
      return fail(Failure::OutOfMemory);
    }
  } else if (malformed) {
    // Deterministic: failing here would fail every later attempt at the same
    // document. The build goes on without its verses.
    skippedDocs_++;
  } else {
    return fail(Failure::ReadFailed);
  }

  docsDone_++;
  currentBook_ = book;
  LOG_DBG(MODULE, "Doc %u/%u spine %u: %lu ms", static_cast<unsigned>(docsDone_), static_cast<unsigned>(totalDocs_),
          spine, millis() - started);

  if (docsDone_ % MEMORY_LOG_EVERY == 0) logMemory("progress");
  if (docsDone_ % CHECKPOINT_EVERY == 0 && !allDocumentsIndexed()) writeCheckpoint();
  return true;
}

bool BibleSearchIndexer::indexDocument(BibleSearch::VerseTextScanner& scanner, const uint16_t spine,
                                       const uint8_t book) {
  const std::string continuation = scanner.continuation();
  if (!continuation.empty() && !builder_->appendToLastVerse(continuation)) return false;

  for (const auto& verse : scanner.take()) {
    // The format stores chapter and verse as u8; Psalm 119:176 is the largest.
    if (verse.chapter > UINT8_MAX || verse.verse > UINT8_MAX) {
      LOG_ERR(MODULE, "Skipping %u:%u in spine %u: out of range", verse.chapter, verse.verse, spine);
      continue;
    }
    const uint32_t number = builder_->addVerse(book, static_cast<uint8_t>(verse.chapter),
                                               static_cast<uint8_t>(verse.verse), spine, verse.anchorOffset);
    if (number == BibleSearch::IndexBuilder::INVALID_VERSE || !builder_->addVerseText(number, verse.text)) {
      return false;
    }
    currentChapter_ = verse.chapter;
  }
  return true;
}

void BibleSearchIndexer::cancel() {
  if (builder_ && !failed() && !finished_ && docsDone_ > lastCheckpointDocs_) writeCheckpoint();
  builder_.reset();
  logMemory("cancelled");
}

bool BibleSearchIndexer::finish() {
  if (!builder_ || failed()) return false;
  if (!allDocumentsIndexed()) {
    LOG_ERR(MODULE, "finish() at document %u of %u", static_cast<unsigned>(docsDone_),
            static_cast<unsigned>(totalDocs_));
    return false;
  }

  [[maybe_unused]] const unsigned long started = millis();
  const Failure written = writeAtomic(BibleSearchStore::INDEX_PATH, true);
  if (written != Failure::None) return fail(written);

  LOG_INF(MODULE, "Wrote %u verses, %u terms, %u bytes in %lu ms; %u documents skipped",
          static_cast<unsigned>(builder_->verseCount()), static_cast<unsigned>(builder_->termCount()),
          static_cast<unsigned>(builder_->estimatedBytes()), millis() - started, static_cast<unsigned>(skippedDocs_));
  if (Storage.exists(BibleSearchStore::CHECKPOINT_PATH) && !Storage.remove(BibleSearchStore::CHECKPOINT_PATH)) {
    // Harmless: status() consults it only when bible.idx is missing.
    LOG_ERR(MODULE, "Could not remove %s", BibleSearchStore::CHECKPOINT_PATH);
  }
  builder_.reset();
  finished_ = true;
  logMemory("end");
  return true;
}

bool BibleSearchIndexer::writeCheckpoint() {
  [[maybe_unused]] const unsigned long started = millis();
  const Failure written = writeAtomic(BibleSearchStore::CHECKPOINT_PATH, false);
  if (written != Failure::None) {
    // Not fatal: the build continues from memory, and the next checkpoint retries.
    LOG_ERR(MODULE, "Checkpoint at document %u not written", static_cast<unsigned>(docsDone_));
    return false;
  }
  lastCheckpointDocs_ = docsDone_;
  LOG_INF(MODULE, "Checkpoint at document %u: %u bytes in %lu ms", static_cast<unsigned>(docsDone_),
          static_cast<unsigned>(builder_->estimatedBytes()), millis() - started);
  return true;
}

BibleSearchIndexer::Failure BibleSearchIndexer::writeAtomic(const char* path, const bool complete) {
  const size_t bytes = builder_->estimatedBytes();
  if (bytes > BibleSearchStore::INDEX_BYTE_BUDGET) {
    LOG_ERR(MODULE, "Refusing to write %s: %u bytes exceeds budget %u", path, static_cast<unsigned>(bytes),
            static_cast<unsigned>(BibleSearchStore::INDEX_BYTE_BUDGET));
    return Failure::OverBudget;
  }

  if (!Storage.exists(BibleSearchStore::SEARCH_DIR) && !Storage.mkdir(BibleSearchStore::SEARCH_DIR)) {
    LOG_ERR(MODULE, "mkdir %s failed", BibleSearchStore::SEARCH_DIR);
    return Failure::WriteFailed;
  }

  const std::string tmpPath = std::string(path) + ".tmp";
  {
    HalFile file;
    if (!Storage.openFileForWrite(MODULE, tmpPath, file)) {
      LOG_ERR(MODULE, "Cannot create %s", tmpPath.c_str());
      return Failure::WriteFailed;
    }
    const uint64_t fingerprint = docs_->fingerprint;
    const bool written = complete ? builder_->write(writeToFile, &file, fingerprint)
                                  : builder_->writeCheckpoint(writeToFile, &file, fingerprint, docsDone_);
    file.flush();
    const size_t onCard = file.fileSize();
    if (!written || onCard != bytes) {
      LOG_ERR(MODULE, "Short write to %s: %u of %u bytes", tmpPath.c_str(), static_cast<unsigned>(onCard),
              static_cast<unsigned>(bytes));
      file.close();
      Storage.remove(tmpPath.c_str());
      return builder_->failed() ? Failure::OutOfMemory : Failure::WriteFailed;
    }
  }

  // The card has no rename-over, so the previous file goes first. The window
  // where neither exists reads as "not built yet", never as a torn index.
  if (Storage.exists(path) && !Storage.remove(path)) {
    LOG_ERR(MODULE, "Could not remove %s", path);
    return Failure::WriteFailed;
  }
  if (!Storage.rename(tmpPath.c_str(), path)) {
    LOG_ERR(MODULE, "Rename %s -> %s failed", tmpPath.c_str(), path);
    return Failure::WriteFailed;
  }
  return Failure::None;
}

void BibleSearchIndexer::logMemory([[maybe_unused]] const char* when) const {
  LOG_INF(MODULE, "Memory %s: internal free %u (largest %u), PSRAM free %u, build holds %u", when,
          static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
          static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)),
          static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)),
          static_cast<unsigned>(builder_ ? builder_->allocatedBytes() : 0));
}
