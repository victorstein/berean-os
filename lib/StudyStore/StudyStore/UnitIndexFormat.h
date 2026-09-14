#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "StudyStore/UnitAnchors.h"

// On-disk layout of /.berean/units/<pubkey>.bin -- one publication's cached
// unit anchors, one entry per spine document.
//
// This is a CACHE and nothing else. Losing it costs a rescan, so it needs
// corruption DETECTION rather than crash atomicity: a CRC over the document
// table, checked on open, and the whole file is rebuilt on mismatch.
//
// Validation is on source file size alone. An earlier design also stored a
// per-document CRC of the visible text, which required reading, inflating and
// parsing the document -- the entire rebuild minus a few push_backs, so the
// cache saved nothing. A same-size content change is deliberately not detected
// here: the per-passage fingerprint catches changed text at paint time, where
// the consequence is visible and recoverable. (Modification time is not an
// option either -- HalFile exposes none and no dateTimeCallback is registered,
// so FAT timestamps on device-written files are a constant.)
//
// Every multi-byte field moves through memcpy. The S3 tolerates unaligned loads
// where the C3 faults, but CLAUDE.md's rule is unconditional and this is shared
// code.
namespace study {

inline constexpr uint32_t UNIT_INDEX_MAGIC = 0x31495542;  // "BUI1" little-endian
inline constexpr uint16_t UNIT_INDEX_VERSION = 1;
inline constexpr size_t UNIT_INDEX_HEADER_BYTES = 32;
inline constexpr size_t UNIT_INDEX_ENTRY_BYTES = 12;
inline constexpr size_t UNIT_INDEX_ANCHOR_BYTES = 8;

struct UnitIndexHeader {
  uint16_t documentCount = 0;
  uint32_t sourceSize = 0;
  uint32_t tableCrc = 0;
  uint32_t tableOffset = 0;
  uint32_t bookMapOffset = 0;  // 0 when the spine-to-book map has not been built
};

struct UnitIndexEntry {
  uint32_t dataOffset = 0;  // 0 means this document has not been indexed yet
  uint16_t anchorCount = 0;
  UnitKind kind = UnitKind::DocumentOffset;
  uint8_t book = 0;

  bool indexed() const { return dataOffset != 0; }
};

void writeHeader(uint8_t* out, const UnitIndexHeader& h);
std::optional<UnitIndexHeader> readHeader(const uint8_t* in, size_t length);

void writeEntry(uint8_t* out, const UnitIndexEntry& e);
UnitIndexEntry readEntry(const uint8_t* in);

void writeAnchors(uint8_t* out, const std::vector<UnitAnchor>& anchors);
std::vector<UnitAnchor> readAnchors(const uint8_t* in, uint16_t count);

// CRC over a serialised document table, for the header's tableCrc.
uint32_t tableChecksum(const uint8_t* table, size_t length);

// True when the EPUB behind this index has been replaced.
bool headerIsStale(const UnitIndexHeader& h, uint32_t sourceSize);

}  // namespace study
