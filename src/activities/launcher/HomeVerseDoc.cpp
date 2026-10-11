#include "activities/launcher/HomeVerseDoc.h"

#include <StudyStore/Unit.h>

#include <cstring>

namespace HomeVerseDoc {
namespace {

// The on-card codes, kept apart from home_verse::Empty's underlying values so that reordering the
// enum cannot change what a saved file means.
constexpr uint8_t EMPTY_CODE_PICK = 0;
constexpr uint8_t EMPTY_CODE_NO_PASSAGES = 1;
constexpr uint8_t EMPTY_CODE_TOO_LONG = 2;

uint8_t emptyCode(const home_verse::Empty empty) {
  switch (empty) {
    case home_verse::Empty::None:
      return EMPTY_CODE_PICK;
    case home_verse::Empty::NoPassages:
      return EMPTY_CODE_NO_PASSAGES;
    case home_verse::Empty::TooLong:
      return EMPTY_CODE_TOO_LONG;
  }
  return EMPTY_CODE_NO_PASSAGES;
}

}  // namespace

bool isUsableBuildId(const char* const buildId) {
  if (buildId == nullptr) return false;
  const size_t length = strnlen(buildId, MAX_BUILD_ID_BYTES + 1);
  if (length == 0 || length > MAX_BUILD_ID_BYTES) return false;
  return strspn(buildId, "0") != length;
}

void toJson(const home_verse::Entry& entry, const char* const buildId, JsonDocument& doc) {
  doc["v"] = FORMAT_VERSION;
  doc["b"] = buildId;

  const JsonObject key = doc["k"].to<JsonObject>();
  key["dt"] = entry.key.dated;
  key["y"] = entry.key.dated ? entry.key.day.year : 0;
  key["m"] = entry.key.dated ? entry.key.day.month : 0;
  key["d"] = entry.key.dated ? entry.key.day.day : 0;
  key["f"] = entry.key.fileExists;
  key["n"] = entry.key.fileBytes;

  doc["e"] = emptyCode(entry.empty);
  if (!entry.hasPick) return;

  const JsonObject pick = doc["p"].to<JsonObject>();
  pick["u"] = study::unitToCompact(entry.pick.start);
  pick["s"] = entry.pick.spine;
  // Cast: a char array would be stored as a fixed-length literal, garbage after the NUL included.
  pick["r"] = static_cast<const char*>(entry.pick.reference);
  pick["rung"] = entry.pick.rung;
  const JsonArray lines = pick["l"].to<JsonArray>();
  for (uint8_t i = 0; i < entry.pick.lineCount; ++i) lines.add(entry.pick.line(i));
}

}  // namespace HomeVerseDoc
