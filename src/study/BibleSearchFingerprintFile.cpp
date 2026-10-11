#include "BibleSearchFingerprintFile.h"

#include <cstring>

namespace BibleSearchFingerprintFile {

namespace {

constexpr size_t MAGIC_AND_VERSION_BYTES = 6;
constexpr size_t CHECKED_BYTES = 32;

void put16(uint8_t* out, const uint16_t v) { memcpy(out, &v, sizeof(v)); }
void put32(uint8_t* out, const uint32_t v) { memcpy(out, &v, sizeof(v)); }
void put64(uint8_t* out, const uint64_t v) { memcpy(out, &v, sizeof(v)); }

uint16_t get16(const uint8_t* in) {
  uint16_t v = 0;
  memcpy(&v, in, sizeof(v));
  return v;
}

uint32_t get32(const uint8_t* in) {
  uint32_t v = 0;
  memcpy(&v, in, sizeof(v));
  return v;
}

uint64_t get64(const uint8_t* in) {
  uint64_t v = 0;
  memcpy(&v, in, sizeof(v));
  return v;
}

uint64_t checkOf(const uint8_t* record) {
  Fnv1a64 check;
  check.add(record, CHECKED_BYTES);
  return check.hash;
}

}  // namespace

void Fnv1a64::add(const void* data, const size_t length) {
  const auto* bytes = static_cast<const uint8_t*>(data);
  for (size_t i = 0; i < length; i++) {
    hash ^= bytes[i];
    hash *= 1099511628211ull;
  }
}

void Fnv1a64::addU32(const uint32_t value) {
  const uint8_t le[4] = {static_cast<uint8_t>(value), static_cast<uint8_t>(value >> 8),
                         static_cast<uint8_t>(value >> 16), static_cast<uint8_t>(value >> 24)};
  add(le, sizeof(le));
}

void Fnv1a64::addU64(const uint64_t value) {
  addU32(static_cast<uint32_t>(value));
  addU32(static_cast<uint32_t>(value >> 32));
}

void encode(uint8_t (&out)[RECORD_BYTES], const Record& record) {
  memset(out, 0, RECORD_BYTES);
  put32(out, MAGIC);
  put16(out + 4, VERSION);
  put16(out + 6, record.key.unitIndexVersion);
  put64(out + 8, record.key.epubSize);
  put32(out + 16, record.key.spineCount);
  put32(out + 20, record.verseDocuments);
  put64(out + 24, record.fingerprint);
  put64(out + 32, checkOf(out));
}

Load decode(const uint8_t* in, const size_t length, const Key& expected, Record& out) {
  if (length < MAGIC_AND_VERSION_BYTES || get32(in) != MAGIC) return Load::Corrupt;
  const uint16_t version = get16(in + 4);
  if (version > VERSION) return Load::TooNew;
  if (version < VERSION || length != RECORD_BYTES) return Load::Corrupt;
  if (get64(in + 32) != checkOf(in)) return Load::Corrupt;

  Record record;
  record.key.unitIndexVersion = get16(in + 6);
  record.key.epubSize = get64(in + 8);
  record.key.spineCount = get32(in + 16);
  record.verseDocuments = get32(in + 20);
  record.fingerprint = get64(in + 24);
  if (record.key.epubSize != expected.epubSize || record.key.spineCount != expected.spineCount ||
      record.key.unitIndexVersion != expected.unitIndexVersion) {
    return Load::Stale;
  }
  out = record;
  return Load::Ok;
}

bool mayOverwrite(const Load result) { return result != Load::TooNew && result != Load::Unreadable; }

bool coversEveryBook(const uint8_t* books, const uint32_t count) {
  bool seen[BIBLE_BOOKS + 1] = {};
  for (uint32_t i = 0; i < count; i++) {
    if (books[i] >= 1 && books[i] <= BIBLE_BOOKS) seen[books[i]] = true;
  }
  for (uint8_t book = 1; book <= BIBLE_BOOKS; book++) {
    if (!seen[book]) return false;
  }
  return true;
}

}  // namespace BibleSearchFingerprintFile
