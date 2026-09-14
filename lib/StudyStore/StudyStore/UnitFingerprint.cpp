#include "StudyStore/UnitFingerprint.h"

#include <cstdio>

namespace study {
namespace {

// CRC-32/ISO-HDLC, computed nibble-wise so the table is 64 bytes of flash
// rather than 1 KB. This runs once per unit on a page turn, not per character
// of a stream, so the halved throughput is invisible.
constexpr uint32_t kNibbleTable[16] = {0x00000000, 0x1DB71064, 0x3B6E20C8, 0x26D930AC, 0x76DC4190, 0x6B6B51F4,
                                       0x4DB26158, 0x5005713C, 0xEDB88320, 0xF00F9344, 0xD6D6A3E8, 0xCB61B38C,
                                       0x9B64C2B0, 0x86D3D2D4, 0xA00AE278, 0xBDBDF21C};

uint32_t countCodepoints(const std::string_view s) {
  uint32_t n = 0;
  for (const unsigned char c : s) {
    if ((c & 0xC0u) != 0x80u) ++n;  // every byte that is not a continuation starts one
  }
  return n;
}

}  // namespace

uint32_t crc32Begin() { return 0xFFFFFFFFu; }

uint32_t crc32Update(uint32_t crc, const std::string_view data) {
  for (const unsigned char byte : data) {
    crc ^= byte;
    crc = (crc >> 4) ^ kNibbleTable[crc & 0x0Fu];
    crc = (crc >> 4) ^ kNibbleTable[crc & 0x0Fu];
  }
  return crc;
}

uint32_t crc32End(const uint32_t crc) { return ~crc; }

Fingerprint fingerprintOf(const std::string_view visibleText) {
  return Fingerprint{countCodepoints(visibleText), crc32End(crc32Update(crc32Begin(), visibleText))};
}

std::string fingerprintToCompact(const Fingerprint& f) {
  char buf[32];
  snprintf(buf, sizeof(buf), "%u:%08x", f.length, f.crc);
  return buf;
}

std::optional<Fingerprint> fingerprintFromCompact(const std::string& s) {
  unsigned length = 0;
  unsigned crc = 0;
  char tail = '\0';
  if (sscanf(s.c_str(), "%u:%x%c", &length, &crc, &tail) != 2) return std::nullopt;
  return Fingerprint{length, crc};
}

}  // namespace study
