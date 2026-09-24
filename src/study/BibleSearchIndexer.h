#pragma once

#include <BibleSearch/IndexBuilder.h>
#include <Epub.h>

#include <cstdint>
#include <memory>

#include "study/BibleSearchStore.h"

// Builds /.berean/search/bible.idx one spine document at a time, so the
// caller's loop() stays responsive and no single call nears the 5 s task
// watchdog. There is no task of its own: the owning activity calls step() once
// per loop pass, on the main task.
//
//   begin()  -> step() ... step() -> finish()
//                       \-> cancel()
//
// Every CHECKPOINT_EVERY documents, and on cancel(), the build so far is
// written atomically to bible.partial; the next begin() resumes from it when
// its fingerprint still matches the Bible on the card.
//
// begin() is the caller's explicit rebuild: finish() replaces whatever
// bible.idx holds, including an unreadable or newer-format file, so it is only
// called after the user confirms.
class BibleSearchIndexer {
 public:
  enum class Failure : uint8_t {
    None,
    NotABible,    // no verse documents, or the book map could not be read
    OutOfMemory,  // PSRAM exhausted, or more verses than the format holds
    ReadFailed,   // a spine document could not be streamed from the EPUB
    WriteFailed,  // the index could not be written to the card
    OverBudget,   // the index would exceed BibleSearchStore::INDEX_BYTE_BUDGET
  };

  static constexpr uint32_t CHECKPOINT_EVERY = 100;
  static constexpr uint32_t MEMORY_LOG_EVERY = 500;

  explicit BibleSearchIndexer(std::shared_ptr<Epub> epub);
  // Frees the build without a checkpoint; call cancel() first to keep the work.
  ~BibleSearchIndexer() = default;
  BibleSearchIndexer(const BibleSearchIndexer&) = delete;
  BibleSearchIndexer& operator=(const BibleSearchIndexer&) = delete;

  // Resolves the verse documents and resumes from a matching checkpoint. Closes
  // any reader BibleSearchStore holds, since finish() replaces its file.
  bool begin();
  // Indexes exactly one document. False once every document is indexed, or
  // when the build has failed.
  bool step();
  // Writes the checkpoint and frees the build.
  void cancel();
  // Writes bible.idx, removes the checkpoint and frees the build. Call once
  // allDocumentsIndexed().
  bool finish();

  bool running() const { return builder_ != nullptr; }
  bool allDocumentsIndexed() const { return totalDocs_ > 0 && docsDone_ >= totalDocs_; }
  bool finished() const { return finished_; }
  uint32_t docsDone() const { return docsDone_; }
  uint32_t totalDocs() const { return totalDocs_; }
  // Canonical book (1-66) and chapter of the most recently indexed document.
  uint8_t currentBook() const { return currentBook_; }
  uint16_t currentChapter() const { return currentChapter_; }
  bool failed() const { return failure_ != Failure::None; }
  Failure failure() const { return failure_; }

 private:
  bool fail(Failure failure);
  bool indexDocument(BibleSearch::VerseTextScanner& scanner, uint16_t spine, uint8_t book);
  bool resumeFromCheckpoint();
  bool writeCheckpoint();
  Failure writeAtomic(const char* path, bool complete);
  void logMemory(const char* when) const;

  std::shared_ptr<Epub> epub_;
  const BibleSearchStore::Documents* docs_ = nullptr;
  std::unique_ptr<BibleSearch::IndexBuilder> builder_;

  uint32_t docsDone_ = 0;
  uint32_t totalDocs_ = 0;
  uint32_t lastCheckpointDocs_ = 0;
  uint32_t skippedDocs_ = 0;
  uint16_t currentChapter_ = 0;
  uint8_t currentBook_ = 0;
  bool finished_ = false;
  Failure failure_ = Failure::None;
};
