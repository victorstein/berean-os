#pragma once

// Shared values for the fingerprint record's codec and I/O tests.

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "src/study/BibleSearchFingerprintFile.h"

namespace fingerprint_fixtures {

inline BibleSearchFingerprintFile::Key key() {
  BibleSearchFingerprintFile::Key k;
  k.epubSize = 36000123;
  k.spineCount = 3937;
  k.unitIndexVersion = 1;
  return k;
}

inline BibleSearchFingerprintFile::Record record() {
  BibleSearchFingerprintFile::Record r;
  r.key = key();
  r.verseDocuments = 1189;
  r.fingerprint = 0xB063E8DC59554F2Dull;
  return r;
}

inline std::vector<uint8_t> encoded(const BibleSearchFingerprintFile::Record& r = record()) {
  uint8_t raw[BibleSearchFingerprintFile::RECORD_BYTES];
  BibleSearchFingerprintFile::encode(raw, r);
  return std::vector<uint8_t>(raw, raw + BibleSearchFingerprintFile::RECORD_BYTES);
}

inline void setVersion(std::vector<uint8_t>& bytes, const uint16_t version) {
  bytes[4] = static_cast<uint8_t>(version);
  bytes[5] = static_cast<uint8_t>(version >> 8);
}

// Recomputes the check over bytes 0-31, so a test can change a field and still
// reach the checks that follow the check comparison.
inline void reseal(std::vector<uint8_t>& bytes) {
  BibleSearchFingerprintFile::Fnv1a64 check;
  check.add(bytes.data(), 32);
  memcpy(bytes.data() + 32, &check.hash, sizeof(check.hash));
}

inline std::string asString(const std::vector<uint8_t>& bytes) { return std::string(bytes.begin(), bytes.end()); }

}  // namespace fingerprint_fixtures
