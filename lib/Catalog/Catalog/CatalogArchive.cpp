#include "Catalog/CatalogArchive.h"

namespace catalog {
namespace {

constexpr uint8_t MAGIC_0 = 0x1F;
constexpr uint8_t MAGIC_1 = 0x8B;
constexpr uint8_t DEFLATE = 0x08;

constexpr size_t FIXED_HEADER_BYTES = 10;
constexpr size_t TRAILER_BYTES = 8;

constexpr uint8_t FLG_FHCRC = 0x02;
constexpr uint8_t FLG_FEXTRA = 0x04;
constexpr uint8_t FLG_FNAME = 0x08;
constexpr uint8_t FLG_FCOMMENT = 0x10;
constexpr uint8_t FLG_RESERVED = 0xE0;

// Advances past a NUL-terminated header field. False when the terminator is not
// inside the buffer.
bool skipZeroTerminated(const uint8_t* data, const size_t len, size_t& cursor) {
  while (cursor < len) {
    if (data[cursor++] == 0) return true;
  }
  return false;
}

}  // namespace

bool looksGzipped(const uint8_t* data, const size_t len) {
  return data != nullptr && len >= 2 && data[0] == MAGIC_0 && data[1] == MAGIC_1;
}

bool gzipPayloadOffset(const uint8_t* data, const size_t len, size_t& out) {
  if (!looksGzipped(data, len) || len < FIXED_HEADER_BYTES) return false;
  if (data[2] != DEFLATE) return false;

  const uint8_t flags = data[3];
  if ((flags & FLG_RESERVED) != 0) return false;

  size_t cursor = FIXED_HEADER_BYTES;

  if ((flags & FLG_FEXTRA) != 0) {
    if (cursor + 2 > len) return false;
    const size_t extraLen = static_cast<size_t>(data[cursor]) | (static_cast<size_t>(data[cursor + 1]) << 8);
    cursor += 2;
    if (extraLen > len - cursor) return false;
    cursor += extraLen;
  }
  if ((flags & FLG_FNAME) != 0 && !skipZeroTerminated(data, len, cursor)) return false;
  if ((flags & FLG_FCOMMENT) != 0 && !skipZeroTerminated(data, len, cursor)) return false;
  if ((flags & FLG_FHCRC) != 0) {
    if (cursor + 2 > len) return false;
    cursor += 2;
  }

  // A payload of zero bytes cannot inflate to anything, and the trailer must
  // still fit after it.
  if (cursor + TRAILER_BYTES >= len) return false;
  out = cursor;
  return true;
}

bool gzipDeclaredSize(const uint8_t* data, const size_t len, uint32_t& out) {
  if (!looksGzipped(data, len) || len < GZIP_MIN_BYTES) return false;
  const uint8_t* isize = data + len - 4;
  out = static_cast<uint32_t>(isize[0]) | (static_cast<uint32_t>(isize[1]) << 8) |
        (static_cast<uint32_t>(isize[2]) << 16) | (static_cast<uint32_t>(isize[3]) << 24);
  return true;
}

}  // namespace catalog
