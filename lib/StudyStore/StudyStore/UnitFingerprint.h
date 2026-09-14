#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

// Whether the text a passage was attached to is still the text that is there.
//
// Computed over VISIBLE codepoints -- what the reader sees -- so a markup-only
// reissue does not orphan a mark, while a corrected word does. Length is in
// codepoints, matching the unit the offsets are in; a byte length would disagree
// with every offset in the record for any accented text.
namespace study {

struct Fingerprint {
  uint32_t length = 0;  // visible codepoints
  uint32_t crc = 0;

  bool operator==(const Fingerprint&) const = default;
};

Fingerprint fingerprintOf(std::string_view visibleText);

// CRC-32/ISO-HDLC, shared with UnitText's document-level CRC so the two cannot
// diverge. Seed with crc32Begin, feed chunks, finish with crc32End.
uint32_t crc32Begin();
uint32_t crc32Update(uint32_t crc, std::string_view data);
uint32_t crc32End(uint32_t crc);

// "14:a1b2c3d4"
std::string fingerprintToCompact(const Fingerprint& f);
std::optional<Fingerprint> fingerprintFromCompact(const std::string& s);

}  // namespace study
