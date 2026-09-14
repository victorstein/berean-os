#include "StudyStore/UnitIndexFormat.h"

#include <cstring>
#include <string_view>

#include "StudyStore/UnitFingerprint.h"

namespace study {
namespace {

void put16(uint8_t* out, const uint16_t v) { memcpy(out, &v, sizeof(v)); }
void put32(uint8_t* out, const uint32_t v) { memcpy(out, &v, sizeof(v)); }

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

}  // namespace

void writeHeader(uint8_t* out, const UnitIndexHeader& h) {
  memset(out, 0, UNIT_INDEX_HEADER_BYTES);
  put32(out + 0, UNIT_INDEX_MAGIC);
  put16(out + 4, UNIT_INDEX_VERSION);
  put16(out + 6, h.documentCount);
  put32(out + 8, h.sourceSize);
  put32(out + 12, h.tableCrc);
  put32(out + 16, h.tableOffset);
  put32(out + 20, h.bookMapOffset);
}

std::optional<UnitIndexHeader> readHeader(const uint8_t* in, const size_t length) {
  if (length < UNIT_INDEX_HEADER_BYTES) return std::nullopt;
  if (get32(in + 0) != UNIT_INDEX_MAGIC) return std::nullopt;
  if (get16(in + 4) != UNIT_INDEX_VERSION) return std::nullopt;

  UnitIndexHeader h;
  h.documentCount = get16(in + 6);
  h.sourceSize = get32(in + 8);
  h.tableCrc = get32(in + 12);
  h.tableOffset = get32(in + 16);
  h.bookMapOffset = get32(in + 20);
  return h;
}

void writeEntry(uint8_t* out, const UnitIndexEntry& e) {
  memset(out, 0, UNIT_INDEX_ENTRY_BYTES);
  put32(out + 0, e.dataOffset);
  put16(out + 4, e.anchorCount);
  out[6] = static_cast<uint8_t>(e.kind);
  out[7] = e.book;
}

UnitIndexEntry readEntry(const uint8_t* in) {
  UnitIndexEntry e;
  e.dataOffset = get32(in + 0);
  e.anchorCount = get16(in + 4);
  const uint8_t kind = in[6];
  e.kind = kind <= static_cast<uint8_t>(UnitKind::Verse) ? static_cast<UnitKind>(kind) : UnitKind::DocumentOffset;
  e.book = in[7];
  return e;
}

void writeAnchors(uint8_t* out, const std::vector<UnitAnchor>& anchors) {
  for (size_t i = 0; i < anchors.size(); ++i) {
    uint8_t* row = out + i * UNIT_INDEX_ANCHOR_BYTES;
    put32(row + 0, anchors[i].offset);
    put16(row + 4, anchors[i].major);
    put16(row + 6, anchors[i].minor);
  }
}

std::vector<UnitAnchor> readAnchors(const uint8_t* in, const uint16_t count) {
  std::vector<UnitAnchor> out;
  out.reserve(count);
  for (uint16_t i = 0; i < count; ++i) {
    const uint8_t* row = in + i * UNIT_INDEX_ANCHOR_BYTES;
    out.push_back({get32(row + 0), get16(row + 4), get16(row + 6)});
  }
  return out;
}

uint32_t tableChecksum(const uint8_t* table, const size_t length) {
  return crc32End(crc32Update(crc32Begin(), std::string_view(reinterpret_cast<const char*>(table), length)));
}

bool headerIsStale(const UnitIndexHeader& h, const uint32_t sourceSize) { return h.sourceSize != sourceSize; }

}  // namespace study
