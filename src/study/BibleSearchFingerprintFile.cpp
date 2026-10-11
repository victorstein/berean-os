#include "BibleSearchFingerprintFile.h"

#include <HalStorage.h>
#include <Logging.h>

#include <algorithm>
#include <cstring>

namespace BibleSearchFingerprintFile {

namespace {

constexpr const char* MODULE = "BSFP";
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

std::string pathIn(const std::string& cachePath) { return cachePath + "/" + FILE_NAME; }

Load load(const std::string& cachePath, const Key& expected, Record& out) {
  const std::string path = pathIn(cachePath);
  if (!Storage.exists(path.c_str())) return Load::Missing;
  HalFile file;
  if (!Storage.openFileForRead(MODULE, path, file)) return Load::Unreadable;

  // The real size, not the bytes read, goes to decode: a record one byte too
  // long must not pass as a whole one.
  const size_t length = file.fileSize();
  const size_t wanted = std::min(length, RECORD_BYTES);
  uint8_t raw[RECORD_BYTES] = {};
  if (wanted > 0) {
    const int got = file.read(raw, wanted);
    if (got < 0) return Load::Unreadable;
    if (static_cast<size_t>(got) != wanted) return Load::Corrupt;
  }
  return decode(raw, length, expected, out);
}

bool save(const std::string& cachePath, const Record& record) {
  const std::string path = pathIn(cachePath);
  const std::string tmpPath = path + ".tmp";
  uint8_t raw[RECORD_BYTES];
  encode(raw, record);
  {
    HalFile file;
    if (!Storage.openFileForWrite(MODULE, tmpPath, file)) {
      LOG_ERR(MODULE, "Cannot create %s", tmpPath.c_str());
      return false;
    }
    const size_t written = file.write(raw, RECORD_BYTES);
    file.flush();
    const size_t onCard = file.fileSize();
    if (written != RECORD_BYTES || onCard != RECORD_BYTES) {
      file.close();
      Storage.remove(tmpPath.c_str());
      LOG_ERR(MODULE, "Short write to %s: %u of %u bytes", tmpPath.c_str(), static_cast<unsigned>(onCard),
              static_cast<unsigned>(RECORD_BYTES));
      return false;
    }
  }

  // The card has no rename-over, so the old record goes first. A crash between
  // the two reads as "no record": one recompute, never a torn record.
  if (Storage.exists(path.c_str()) && !Storage.remove(path.c_str())) {
    LOG_ERR(MODULE, "Could not remove %s", path.c_str());
    return false;
  }
  if (!Storage.rename(tmpPath.c_str(), path.c_str())) {
    LOG_ERR(MODULE, "Rename %s -> %s failed", tmpPath.c_str(), path.c_str());
    return false;
  }
  return true;
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
