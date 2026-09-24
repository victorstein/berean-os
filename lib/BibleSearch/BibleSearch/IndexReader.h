#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

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

// Reads the header once and everything else on demand, in small reads: a
// lookup is a binary search of 10-byte term entries and their strings, and
// postings decode through a small stack page.
class IndexReader {
 public:
  enum class Status { Ok, Missing, Unreadable, TooNew, Stale, Incomplete };

  // Stale and Incomplete leave the reader usable: the file is well formed, it
  // just describes another Bible or an unfinished build. Every other non-Ok
  // status leaves it closed.
  Status open(const ByteSource& source, uint64_t expectedFingerprint);

  const IndexHeader& header() const { return header_; }
  uint32_t verseCount() const { return header_.verseCount; }
  uint32_t termCount() const { return header_.termCount; }

  bool verse(uint32_t n, VerseEntry& out) const;
  bool term(uint32_t index, TermEntry& out) const;
  // Copies the folded term into `out`, which must hold MAX_TOKEN_BYTES + 1.
  bool termString(const TermEntry& entry, char* out, size_t& length) const;

  bool findExact(std::string_view foldedTerm, TermEntry& out) const;
  // [first, last) range of terms that start with `foldedPrefix`; empty when
  // first == last.
  bool findPrefixRange(std::string_view foldedPrefix, uint32_t& first, uint32_t& last) const;

  // Decodes one term's postings, appending to `out`, stopping at `cap`
  // entries. False on a malformed list.
  bool postings(const TermEntry& entry, std::vector<uint16_t>& out, size_t cap) const;

 private:
  bool readAt(uint32_t offset, void* dst, uint32_t len) const;
  // -1, 0 or 1 as the term at `index` sorts before, equal to or after `key`;
  // `isPrefix` reports whether the term starts with `key`.
  bool compareAt(uint32_t index, std::string_view key, int& order, bool& isPrefix) const;
  bool lowerBound(std::string_view key, uint32_t& index) const;

  ByteSource source_{};
  IndexHeader header_{};
  bool open_ = false;
};

}  // namespace BibleSearch
