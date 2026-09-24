#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "Allocator.h"
#include "IndexFormat.h"
#include "IndexReader.h"

namespace BibleSearch {

using ByteSink = bool (*)(void* ctx, const void* data, size_t length);

// Fixed-size records in chunks from a BuildAllocator, so the build is a few
// hundred large blocks rather than tens of thousands of small ones, and an
// allocation failure is a false return rather than an abort inside a
// container.
class RecordPool {
 public:
  RecordPool(BuildAllocator allocator, uint32_t recordBytes, uint32_t recordsPerChunk);
  ~RecordPool();
  RecordPool(const RecordPool&) = delete;
  RecordPool& operator=(const RecordPool&) = delete;

  // Index of the first of `count` consecutive records within one chunk, or
  // NONE when out of memory.
  uint32_t append(uint32_t count = 1);
  uint8_t* at(uint32_t index) const;
  uint32_t size() const { return size_; }
  size_t allocatedBytes() const { return chunks_.size() * static_cast<size_t>(recordBytes_) * recordsPerChunk_; }

  static constexpr uint32_t NONE = UINT32_MAX;

 private:
  BuildAllocator allocator_;
  uint32_t recordBytes_;
  uint32_t recordsPerChunk_;
  uint32_t size_ = 0;
  std::vector<uint8_t*> chunks_;
};

// Accumulates verses and their terms, then serialises the index. Pure: it
// knows nothing of HalStorage, and every failure -- out of memory, too many
// verses, verses out of order -- is a false return that sticks until the
// builder is discarded.
class IndexBuilder {
 public:
  explicit IndexBuilder(BuildAllocator allocator = defaultBuildAllocator());
  ~IndexBuilder();
  IndexBuilder(const IndexBuilder&) = delete;
  IndexBuilder& operator=(const IndexBuilder&) = delete;

  static constexpr uint32_t INVALID_VERSE = UINT32_MAX;

  // Records a verse and returns its global number, or INVALID_VERSE. Verses
  // must arrive in canonical order.
  uint32_t addVerse(uint8_t book, uint8_t chapter, uint8_t verse, uint16_t spine, uint32_t offset);
  // Folds and tokenizes `rawText`, recording each distinct term once for this
  // verse. `verseNumber` must be the most recently added verse.
  bool addVerseText(uint32_t verseNumber, std::string_view rawText);
  // Appends continuation text to the most recently added verse.
  bool appendToLastVerse(std::string_view rawText);

  // Writes are batched through a SINK_BUFFER_BYTES block from the allocator,
  // so an SD sink sees a few hundred sector-sized writes, not one per record.
  static constexpr size_t SINK_BUFFER_BYTES = 4096;

  bool write(ByteSink sink, void* ctx, uint64_t fingerprint) const;
  // The same format with the complete flag clear and docsDone set.
  bool writeCheckpoint(ByteSink sink, void* ctx, uint64_t fingerprint, uint32_t docsDone) const;
  // Restores a checkpoint written by writeCheckpoint into an empty builder.
  bool loadCheckpoint(const ByteSource& source, uint64_t expectedFingerprint, uint32_t& docsDoneOut);

  // Size of the file write() would produce, for the byte budget.
  size_t estimatedBytes() const;
  // PSRAM held by the build, for the memory log.
  size_t allocatedBytes() const;

  // Verse `n` as recorded; false past the end.
  bool verseAt(uint32_t n, VerseEntry& out) const;
  uint32_t verseCount() const { return verses_.size(); }
  uint32_t termCount() const { return terms_.size(); }
  bool failed() const { return failed_; }

 private:
  struct Term;

  bool recordTerm(std::string_view term, uint16_t verseNumber);
  bool appendPostingBytes(Term& term, const uint8_t* bytes, size_t length);
  uint32_t findOrInsert(std::string_view term, uint32_t hash);
  bool growSlots();
  std::string_view termString(const Term& term) const;
  Term& termAt(uint32_t id) const;
  bool serialise(ByteSink sink, void* ctx, uint64_t fingerprint, uint16_t flags, uint32_t docsDone) const;

  BuildAllocator allocator_;
  RecordPool verses_;
  RecordPool terms_;
  RecordPool strings_;
  RecordPool blocks_;
  uint32_t* slots_ = nullptr;  // term id + 1 per slot; 0 is empty
  uint32_t slotCount_ = 0;
  size_t stringBytes_ = 0;
  size_t postingBytes_ = 0;
  std::string foldBuffer_;
  mutable bool failed_ = false;
};

}  // namespace BibleSearch
