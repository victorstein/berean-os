#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

// On-disk layout of /.berean/search/bible.idx, and of the build checkpoint
// bible.partial, which is the same format with the complete flag clear.
//
//   header        INDEX_HEADER_BYTES
//   verse table   verseCount x VERSE_ENTRY_BYTES, canonical order
//   term table    termCount x TERM_ENTRY_BYTES, sorted by folded bytes (memcmp order)
//   term strings  folded UTF-8, each NUL-terminated, in term-table order
//   postings      per term, ascending global verse numbers as LEB128 deltas,
//                 the first taken from 0, in term-table order
//
// Little-endian throughout. Every multi-byte field moves through memcpy, as in
// UnitIndexFormat, so no field is ever read unaligned.
//
// Derived data: losing it costs a rebuild. A newer FORMAT_VERSION is refused,
// never reinterpreted, and a file whose recorded size disagrees with its real
// size is unreadable rather than half-trusted.
namespace BibleSearch {

inline constexpr char INDEX_MAGIC[4] = {'B', 'S', 'I', 'X'};
inline constexpr uint16_t INDEX_FORMAT_VERSION = 1;
inline constexpr uint16_t INDEX_FLAG_COMPLETE = 0x0001;

inline constexpr size_t INDEX_HEADER_BYTES = 48;
inline constexpr size_t VERSE_ENTRY_BYTES = 9;
inline constexpr size_t TERM_ENTRY_BYTES = 10;

// Verse numbers are u16 in postings and results, and a term's postingCount is
// u16. About 31,100 verses exist; this bounds a malformed or foreign book.
inline constexpr uint32_t MAX_VERSES = 65535;

struct IndexHeader {
  uint16_t formatVersion = INDEX_FORMAT_VERSION;
  uint16_t flags = 0;
  uint64_t fingerprint = 0;
  uint32_t verseCount = 0;
  uint32_t termCount = 0;
  uint32_t docsDone = 0;  // spine documents indexed; meaningful in a checkpoint
  uint32_t verseTableOffset = 0;
  uint32_t termTableOffset = 0;
  uint32_t termStringsOffset = 0;
  uint32_t postingsOffset = 0;
  uint32_t fileSize = 0;

  bool complete() const { return (flags & INDEX_FLAG_COMPLETE) != 0; }
};

struct VerseEntry {
  uint8_t book = 0;  // canonical, 1-66
  uint8_t chapter = 0;
  uint8_t verse = 0;
  uint16_t spine = 0;
  uint32_t offset = 0;  // VerseAnchors' visible-codepoint offset in the spine document
};

struct TermEntry {
  uint32_t stringOffset = 0;    // from the start of the term strings
  uint32_t postingsOffset = 0;  // from the start of the postings
  uint16_t postingCount = 0;
};

void writeHeader(uint8_t* out, const IndexHeader& h);
// Checks the magic only. The caller decides what a different version means.
std::optional<IndexHeader> readHeader(const uint8_t* in, size_t length);

void writeVerseEntry(uint8_t* out, const VerseEntry& e);
VerseEntry readVerseEntry(const uint8_t* in);

void writeTermEntry(uint8_t* out, const TermEntry& e);
TermEntry readTermEntry(const uint8_t* in);

// A u16 delta takes at most three LEB128 bytes.
inline constexpr size_t MAX_VARINT_BYTES = 3;
// Returns the number of bytes written to `out`.
size_t writeVarint(uint8_t* out, uint16_t value);

}  // namespace BibleSearch
