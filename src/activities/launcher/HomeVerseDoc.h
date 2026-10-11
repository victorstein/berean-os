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
