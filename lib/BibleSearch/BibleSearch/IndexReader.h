#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

#include "Allocator.h"
#include "Fold.h"
#include "IndexFormat.h"

namespace BibleSearch {

// Random access to a serialised index. Memory on the host, a HalFile on the
// device; the reader never assumes the whole file is resident.
struct ByteSource {
  void* ctx = nullptr;
  bool (*readAt)(void* ctx, uint32_t offset, void* dst, uint32_t len) = nullptr;
  uint32_t size = 0;
};

using PostingVisitor = void (*)(void* ctx, uint16_t verse);

// Reads the header at open and everything else on demand. Postings stream
// through one reader-owned page, so a list or a prefix range's contiguous
// span costs one read per page rather than one per term. cacheTermIndex()
// pulls the term table and strings into memory in one read, after which a
// lookup touches the source not at all.
//
// Not thread-safe: the page is shared state even behind const methods. The
// owning task is the only caller.
class IndexReader {
 public:
  enum class Status { Ok, Missing, Unreadable, TooNew, Stale, Incomplete };

  // Reads of about a sector or more are what SD rewards; 4 KB keeps a prefix
  // range over the NWT's widest two-letter span to a few dozen reads.
  static constexpr uint32_t PAGE_BYTES = 4096;

  explicit IndexReader(BuildAllocator allocator = defaultBuildAllocator());
  ~IndexReader() = default;
  IndexReader(const IndexReader&) = delete;
  IndexReader& operator=(const IndexReader&) = delete;

  // Stale and Incomplete leave the reader usable: the file is well formed, it
  // just describes another Bible or an unfinished build. Every other non-Ok
  // status leaves it closed.
  Status open(const ByteSource& source, uint64_t expectedFingerprint);
  // Frees the page and the term cache.
  void close();

  // Loads [termTableOffset, postingsOffset) -- ~440 KB for the NWT -- in one
  // sequential read. False when it cannot be allocated or read; the reader
  // then keeps serving lookups from the source.
  bool cacheTermIndex();
  bool termIndexCached() const { return termIndex_.get() != nullptr; }

  const IndexHeader& header() const { return header_; }
  uint32_t verseCount() const { return header_.verseCount; }
  uint32_t termCount() const { return header_.termCount; }
  BuildAllocator allocator() const { return allocator_; }

  bool verse(uint32_t n, VerseEntry& out) const;
  bool term(uint32_t index, TermEntry& out) const;
  // Copies the folded term into `out`, which must hold MAX_TOKEN_BYTES + 1.
  bool termString(const TermEntry& entry, char* out, size_t& length) const;

  // False on an I/O failure or a malformed table; `found` says whether the
  // term exists.
  bool findExact(std::string_view foldedTerm, TermEntry& out, bool& found) const;
  // Found and read without error.
  bool findExact(std::string_view foldedTerm, TermEntry& out) const;
  // [first, last) range of terms that start with `foldedPrefix`; empty when
  // first == last. False only on an I/O failure or a malformed table.
  bool findPrefixRange(std::string_view foldedPrefix, uint32_t& first, uint32_t& last) const;

  // Decodes one term's postings, appending to `out`, stopping at `cap`
  // entries. False on a malformed list.
  bool postings(const TermEntry& entry, std::vector<uint16_t>& out, size_t cap) const;
  // Streams every posting of terms [first, last), term by term, each list
  // ascending. The lists are contiguous in the file, so this reads the span
  // once, page by page. False on a malformed list.
  bool postingsRange(uint32_t first, uint32_t last, PostingVisitor visit, void* ctx) const;
  // Streams one term's postings.
  bool postingsOf(const TermEntry& entry, PostingVisitor visit, void* ctx) const;

 private:
  bool readAt(uint32_t offset, void* dst, uint32_t len) const;
  // Serves the term table and strings from the cache when there is one.
  bool readTermBytes(uint32_t offset, void* dst, uint32_t len) const;
  bool byteAt(uint32_t position, uint8_t& out) const;
  bool decode(uint32_t position, uint16_t count, size_t cap, PostingVisitor visit, void* ctx) const;
  // -1, 0 or 1 as the term at `index` sorts before, equal to or after `key`;
  // `isPrefix` reports whether the term starts with `key`.
  bool compareAt(uint32_t index, std::string_view key, int& order, bool& isPrefix) const;
  bool lowerBound(std::string_view key, uint32_t& index) const;

  BuildAllocator allocator_;
  ByteSource source_{};
  IndexHeader header_{};
  bool open_ = false;

  AllocatedBuffer termIndex_;
  AllocatedBuffer page_;
  // Used when the page cannot be allocated, so a reader never fails to open
  // for want of a buffer; it just reads in smaller steps.
  mutable uint8_t fallbackPage_[64] = {};
  mutable uint32_t pageStart_ = 0;
  mutable uint32_t pageLength_ = 0;
};

}  // namespace BibleSearch
