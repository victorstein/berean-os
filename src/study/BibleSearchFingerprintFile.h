#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

// The Bible search fingerprint, kept beside the Bible's book.bin as
// <cachePath>/search-fingerprint.bin, so the first search after a boot reads 40
// bytes instead of re-resolving every verse document. docs/file-formats.md
// documents the layout.
//
// Derived data, and it lives in the Bible's cache directory on purpose: every
// on-device delete or replace of the Bible removes that directory, and the
// record with it.
namespace BibleSearchFingerprintFile {

inline constexpr uint32_t MAGIC = 0x50465342;  // "BSFP" little-endian
// Bump when what the fingerprint covers, how verse documents are resolved, or
// (through a BOOK_CACHE_VERSION change) the spine hrefs it hashes, changes. An
// in-place book.bin rebuild leaves this record behind.
inline constexpr uint16_t VERSION = 1;
inline constexpr size_t RECORD_BYTES = 40;
inline constexpr const char* FILE_NAME = "search-fingerprint.bin";
inline constexpr uint8_t BIBLE_BOOKS = 66;

// FNV-1a 64. Every bible.idx on a card is keyed by it, so its output must never
// change: the host suite pins it to the published vectors.
struct Fnv1a64 {
  uint64_t hash = 14695981039346656037ull;

  void add(const void* data, size_t length);
  void addU32(uint32_t value);
  void addU64(uint64_t value);
};

struct Key {
  uint64_t epubSize = 0;
  uint32_t spineCount = 0;
  uint16_t unitIndexVersion = 0;
};

struct Record {
  Key key;
  uint32_t verseDocuments = 0;
  uint64_t fingerprint = 0;
};

enum class Load { Ok, Missing, Unreadable, Corrupt, TooNew, Stale };

void encode(uint8_t (&out)[RECORD_BYTES], const Record& record);
// `length` is the file's real size; `in` holds min(length, RECORD_BYTES) bytes.
// The magic is checked before the version and the version before the length, so
// a newer record of any length is TooNew and a foreign file never is.
Load decode(const uint8_t* in, size_t length, const Key& expected, Record& out);

std::string pathIn(const std::string& cachePath);
// One read of min(file size, RECORD_BYTES) bytes. Missing when there is no file,
// Unreadable when it exists but cannot be opened or read.
Load load(const std::string& cachePath, const Key& expected, Record& out);
// Writes <record>.tmp, removes the old record, renames the temp into place.
// False on any failure; a failed temp write leaves the old record as it was.
bool save(const std::string& cachePath, const Record& record);

// Whether a record that loaded this way may be replaced. A newer one belongs to
// a later build; one that could not be read may be fine on the next try.
bool mayOverwrite(Load result);

// True when every book 1-66 contributed at least one verse document. Only such a
// resolve is persisted: a book lost to a transient read failure would otherwise
// outlive the boot it happened in.
bool coversEveryBook(const uint8_t* books, uint32_t count);

}  // namespace BibleSearchFingerprintFile
