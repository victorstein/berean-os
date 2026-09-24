#include "IndexReader.h"

#include <algorithm>
#include <cstring>

namespace BibleSearch {

IndexReader::IndexReader(const BuildAllocator allocator) : allocator_(allocator) {}

void IndexReader::close() {
  open_ = false;
  termIndex_.reset();
  page_.reset();
  pageStart_ = 0;
  pageLength_ = 0;
}

IndexReader::Status IndexReader::open(const ByteSource& source, const uint64_t expectedFingerprint) {
  close();
  source_ = source;
  if (!source.readAt || source.size == 0) return Status::Missing;

  uint8_t raw[INDEX_HEADER_BYTES];
  if (source.size < INDEX_HEADER_BYTES || !source.readAt(source.ctx, 0, raw, INDEX_HEADER_BYTES)) {
    return Status::Unreadable;
  }
  const std::optional<IndexHeader> parsed = readHeader(raw, INDEX_HEADER_BYTES);
  if (!parsed) return Status::Unreadable;
  const IndexHeader& h = *parsed;
  if (h.formatVersion > INDEX_FORMAT_VERSION) return Status::TooNew;
  if (h.formatVersion != INDEX_FORMAT_VERSION) return Status::Unreadable;

  // 64-bit sums, so a hostile count cannot wrap into a plausible offset.
  const uint64_t termTable = static_cast<uint64_t>(h.verseTableOffset) + uint64_t{h.verseCount} * VERSE_ENTRY_BYTES;
  const uint64_t termStrings = static_cast<uint64_t>(h.termTableOffset) + uint64_t{h.termCount} * TERM_ENTRY_BYTES;
  const bool consistent = h.fileSize == source.size && h.verseCount <= MAX_VERSES &&
                          h.verseTableOffset == INDEX_HEADER_BYTES && h.termTableOffset == termTable &&
                          h.termStringsOffset == termStrings && h.termStringsOffset <= h.postingsOffset &&
                          h.postingsOffset <= h.fileSize;
  if (!consistent) return Status::Unreadable;

  header_ = h;
  open_ = true;
  page_.allocate(allocator_, PAGE_BYTES);
  if (h.fingerprint != expectedFingerprint) return Status::Stale;
  if (!h.complete()) return Status::Incomplete;
  return Status::Ok;
}

bool IndexReader::cacheTermIndex() {
  if (!open_) return false;
  if (termIndex_.get()) return true;
  const uint32_t bytes = header_.postingsOffset - header_.termTableOffset;
  if (bytes == 0) return false;
  if (!termIndex_.allocate(allocator_, bytes)) return false;
  if (!readAt(header_.termTableOffset, termIndex_.get(), bytes)) {
    termIndex_.reset();
    return false;
  }
  return true;
}

bool IndexReader::readAt(const uint32_t offset, void* dst, const uint32_t len) const {
  if (!open_ || static_cast<uint64_t>(offset) + len > header_.fileSize) return false;
  return source_.readAt(source_.ctx, offset, dst, len);
}

bool IndexReader::readTermBytes(const uint32_t offset, void* dst, const uint32_t len) const {
  if (termIndex_.get() && offset >= header_.termTableOffset &&
      static_cast<uint64_t>(offset) + len <= header_.postingsOffset) {
    memcpy(dst, termIndex_.as<uint8_t>() + (offset - header_.termTableOffset), len);
    return true;
  }
  return readAt(offset, dst, len);
}

bool IndexReader::byteAt(const uint32_t position, uint8_t& out) const {
  if (position < pageStart_ || position - pageStart_ >= pageLength_) {
    if (position >= header_.fileSize) return false;
    uint8_t* page = page_.get() ? page_.as<uint8_t>() : fallbackPage_;
    const uint32_t capacity = page_.get() ? PAGE_BYTES : static_cast<uint32_t>(sizeof(fallbackPage_));
    const uint32_t length = std::min(capacity, header_.fileSize - position);
    if (!readAt(position, page, length)) {
      pageLength_ = 0;
      return false;
    }
    pageStart_ = position;
    pageLength_ = length;
  }
  const uint8_t* page = page_.get() ? page_.as<uint8_t>() : fallbackPage_;
  out = page[position - pageStart_];
  return true;
}

bool IndexReader::verse(const uint32_t n, VerseEntry& out) const {
  if (n >= header_.verseCount) return false;
  uint8_t raw[VERSE_ENTRY_BYTES];
  if (!readAt(header_.verseTableOffset + n * static_cast<uint32_t>(VERSE_ENTRY_BYTES), raw, VERSE_ENTRY_BYTES)) {
    return false;
  }
  out = readVerseEntry(raw);
  return true;
}

bool IndexReader::term(const uint32_t index, TermEntry& out) const {
  if (index >= header_.termCount) return false;
  uint8_t raw[TERM_ENTRY_BYTES];
  if (!readTermBytes(header_.termTableOffset + index * static_cast<uint32_t>(TERM_ENTRY_BYTES), raw,
                     TERM_ENTRY_BYTES)) {
    return false;
  }
  out = readTermEntry(raw);
  return true;
}

bool IndexReader::termString(const TermEntry& entry, char* out, size_t& length) const {
  const uint64_t start = uint64_t{header_.termStringsOffset} + entry.stringOffset;
  if (start >= header_.postingsOffset) return false;
  const auto available = static_cast<uint32_t>(std::min<uint64_t>(MAX_TOKEN_BYTES + 1, header_.postingsOffset - start));
  if (!readTermBytes(static_cast<uint32_t>(start), out, available)) return false;
  const void* terminator = memchr(out, '\0', available);
  if (!terminator) return false;
  length = static_cast<size_t>(static_cast<const char*>(terminator) - out);
  return true;
}

bool IndexReader::compareAt(const uint32_t index, const std::string_view key, int& order, bool& isPrefix) const {
  TermEntry entry;
  char text[MAX_TOKEN_BYTES + 1];
  size_t length = 0;
  if (!term(index, entry) || !termString(entry, text, length)) return false;
  const int byBytes = memcmp(text, key.data(), std::min(length, key.size()));
  if (byBytes != 0) {
    order = byBytes < 0 ? -1 : 1;
  } else {
    order = length < key.size() ? -1 : (length > key.size() ? 1 : 0);
  }
  isPrefix = length >= key.size() && byBytes == 0;
  return true;
}

bool IndexReader::lowerBound(const std::string_view key, uint32_t& index) const {
  uint32_t low = 0;
  uint32_t high = header_.termCount;
  while (low < high) {
    const uint32_t mid = low + (high - low) / 2;
    int order = 0;
    bool isPrefix = false;
    if (!compareAt(mid, key, order, isPrefix)) return false;
    if (order < 0) {
      low = mid + 1;
    } else {
      high = mid;
    }
  }
  index = low;
  return true;
}

bool IndexReader::findExact(const std::string_view foldedTerm, TermEntry& out, bool& found) const {
  found = false;
  if (!open_) return false;
  uint32_t index = 0;
  if (foldedTerm.empty()) return true;
  if (!lowerBound(foldedTerm, index)) return false;
  if (index >= header_.termCount) return true;
  int order = 0;
  bool isPrefix = false;
  if (!compareAt(index, foldedTerm, order, isPrefix)) return false;
  if (order != 0) return true;
  if (!term(index, out)) return false;
  found = true;
  return true;
}

bool IndexReader::findExact(const std::string_view foldedTerm, TermEntry& out) const {
  bool found = false;
  return findExact(foldedTerm, out, found) && found;
}

bool IndexReader::findPrefixRange(const std::string_view foldedPrefix, uint32_t& first, uint32_t& last) const {
  if (!open_ || foldedPrefix.empty() || !lowerBound(foldedPrefix, first)) return false;
  // Terms sharing the prefix are contiguous from `first`, so the end is the
  // first term past it that does not start with the prefix.
  uint32_t low = first;
  uint32_t high = header_.termCount;
  while (low < high) {
    const uint32_t mid = low + (high - low) / 2;
    int order = 0;
    bool isPrefix = false;
    if (!compareAt(mid, foldedPrefix, order, isPrefix)) return false;
    if (isPrefix) {
      low = mid + 1;
    } else {
      high = mid;
    }
  }
  last = low;
  return true;
}

bool IndexReader::decode(uint32_t position, const uint16_t count, const size_t cap, const PostingVisitor visit,
                         void* ctx) const {
  uint32_t verse = 0;
  for (uint32_t decoded = 0; decoded < count && decoded < cap; decoded++) {
    uint32_t delta = 0;
    for (uint32_t shift = 0;; shift += 7) {
      if (shift >= 7 * MAX_VARINT_BYTES) return false;
      uint8_t byte = 0;
      if (!byteAt(position++, byte)) return false;
      delta |= static_cast<uint32_t>(byte & 0x7F) << shift;
      if ((byte & 0x80) == 0) break;
    }
    if (decoded > 0 && delta == 0) return false;  // postings are strictly ascending
    verse += delta;
    if (verse >= header_.verseCount) return false;
    visit(ctx, static_cast<uint16_t>(verse));
  }
  return true;
}

bool IndexReader::postingsOf(const TermEntry& entry, const PostingVisitor visit, void* ctx) const {
  if (!open_) return false;
  const uint64_t position = uint64_t{header_.postingsOffset} + entry.postingsOffset;
  if (position >= header_.fileSize && entry.postingCount > 0) return false;
  return decode(static_cast<uint32_t>(position), entry.postingCount, entry.postingCount, visit, ctx);
}

bool IndexReader::postings(const TermEntry& entry, std::vector<uint16_t>& out, const size_t cap) const {
  if (!open_) return false;
  const uint64_t position = uint64_t{header_.postingsOffset} + entry.postingsOffset;
  if (position >= header_.fileSize && entry.postingCount > 0) return false;
  out.reserve(out.size() + std::min<size_t>(entry.postingCount, cap));
  return decode(
      static_cast<uint32_t>(position), entry.postingCount, cap,
      [](void* ctx, const uint16_t verse) { static_cast<std::vector<uint16_t>*>(ctx)->push_back(verse); }, &out);
}

bool IndexReader::postingsRange(const uint32_t first, const uint32_t last, const PostingVisitor visit,
                                void* ctx) const {
  if (!open_ || first > last || last > header_.termCount) return false;
  for (uint32_t index = first; index < last; index++) {
    TermEntry entry;
    if (!term(index, entry) || !postingsOf(entry, visit, ctx)) return false;
  }
  return true;
}

}  // namespace BibleSearch
