#pragma once

#include <ArduinoJson.h>

#include <cstddef>
#include <cstdint>

#include "activities/launcher/HomeVerseCache.h"

// Format rules for /.berean/home-verse.json: Home's last completed verse scan, kept so a wake on
// the same day, file and build skips it. A cache, not study data: losing it costs one scan. No
// storage access -- HomeVerseFile is the shell -- and free of <Arduino.h> so test/home_layout runs
// it on the host. Never measures or serialises, as PlacesDoc.
namespace HomeVerseDoc {

inline constexpr int FORMAT_VERSION = 1;

// Room for a whole hex SHA-256; the firmware stores its first 8 digits.
inline constexpr size_t MAX_BUILD_ID_BYTES = 64;

// {"v":1,"b":"","k":{"dt":false,"y":,"m":,"d":,"f":false,"n":},"e":,"p":{"u":"","s":,"r":"","rung":,"l":[]}}
inline constexpr size_t DOC_WRAPPER_BYTES = 106;
static_assert(FORMAT_VERSION < 10, "DOC_WRAPPER_BYTES counts a one-digit version");
// 65535, 255, 255, 4294967295: the key's year, month, day and size at their types' maximum.
inline constexpr size_t MAX_KEY_NUMBER_BYTES = 5 + 3 + 3 + 10;
inline constexpr size_t MAX_EMPTY_CODE_BYTES = 1;
inline constexpr size_t MAX_SPINE_BYTES = 5;
inline constexpr size_t MAX_RUNG_BYTES = 3;
// d:255:65535:65535:4294967295 -- unitToCompact with every field at its maximum.
inline constexpr size_t MAX_UNIT_BYTES = 28;
// \u00XX is the longest escape JSON has, so every string byte is counted at six.
inline constexpr size_t ESCAPE_FACTOR = 6;
// Each line's two quotes, and the commas between lines.
inline constexpr size_t LINE_FRAMING_BYTES = 2 * home_verse::MAX_LINES + (home_verse::MAX_LINES - 1);

constexpr size_t worstCaseBytes() {
  return DOC_WRAPPER_BYTES + ESCAPE_FACTOR * MAX_BUILD_ID_BYTES + MAX_KEY_NUMBER_BYTES + MAX_EMPTY_CODE_BYTES +
         MAX_UNIT_BYTES + MAX_SPINE_BYTES + ESCAPE_FACTOR * (home_verse::REFERENCE_BYTES - 1) + MAX_RUNG_BYTES +
         LINE_FRAMING_BYTES + ESCAPE_FACTOR * home_verse::LINE_BUFFER_BYTES;
}

// Derived from the caps above, not a round number: recompute it, do not tidy it.
inline constexpr size_t SAVE_BUDGET = worstCaseBytes();

enum class Restore : uint8_t { Restored, UnknownFormat, OtherBuild, Malformed };

// An all-zero identity is an image whose SHA was never filled in, which would make every build
// look the same.
bool isUsableBuildId(const char* buildId);

// `entry` must be valid.
void toJson(const home_verse::Entry& entry, const char* buildId, JsonDocument& doc);

// Fills `out` in place, since an Entry is too large for the stack. `out.valid` is true only on
// Restored: any field of the wrong type or out of range refuses the whole entry. `rungCount`
// bounds the stored font rung.
Restore fromJson(JsonVariantConst doc, const char* buildId, uint8_t rungCount, home_verse::Entry& out);

}  // namespace HomeVerseDoc
