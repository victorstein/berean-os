#include "activities/launcher/HomeVerseDoc.h"

#include <FormatVersion.h>
#include <StudyStore/Unit.h>

#include <cstring>
#include <optional>
#include <string>
#include <vector>

#include "util/CivilDate.h"

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

std::optional<home_verse::Empty> emptyFromCode(const uint8_t code) {
  switch (code) {
    case EMPTY_CODE_PICK:
      return home_verse::Empty::None;
    case EMPTY_CODE_NO_PASSAGES:
      return home_verse::Empty::NoPassages;
    case EMPTY_CODE_TOO_LONG:
      return home_verse::Empty::TooLong;
    default:
      return std::nullopt;
  }
}

// is<T>() before every read: a `| default` read takes a wrong type silently, and most of these
// defaults are themselves valid values. ArduinoJson's is<T>() on an integer type also checks range.
bool readKey(const JsonVariantConst json, home_verse::Key& key) {
  if (!json.is<JsonObjectConst>()) return false;
  if (!json["dt"].is<bool>() || !json["f"].is<bool>() || !json["y"].is<uint16_t>() || !json["m"].is<uint8_t>() ||
      !json["d"].is<uint8_t>() || !json["n"].is<uint32_t>()) {
    return false;
  }
  key.dated = json["dt"].as<bool>();
  key.day.year = json["y"].as<uint16_t>();
  key.day.month = json["m"].as<uint8_t>();
  key.day.day = json["d"].as<uint8_t>();
  key.fileExists = json["f"].as<bool>();
  key.fileBytes = json["n"].as<uint32_t>();
  return !key.dated || isValidCivilDate(key.day);
}

bool readPick(const JsonVariantConst json, const uint8_t rungCount, home_verse::Pick& pick) {
  if (!json.is<JsonObjectConst>()) return false;
  if (!json["u"].is<const char*>() || !json["s"].is<uint16_t>() || !json["r"].is<const char*>() ||
      !json["rung"].is<uint8_t>() || !json["l"].is<JsonArrayConst>()) {
    return false;
  }

  const std::optional<study::Unit> start = study::unitFromCompact(std::string(json["u"].as<const char*>()));
  if (!start) return false;
  const char* const reference = json["r"].as<const char*>();
  const size_t referenceBytes = strlen(reference);
  if (referenceBytes >= home_verse::REFERENCE_BYTES) return false;
  const uint8_t rung = json["rung"].as<uint8_t>();
  if (rung >= rungCount) return false;

  const JsonArrayConst storedLines = json["l"].as<JsonArrayConst>();
  if (storedLines.size() == 0 || storedLines.size() > home_verse::MAX_LINES) return false;
  std::vector<std::string> lines;
  lines.reserve(storedLines.size());
  for (const JsonVariantConst line : storedLines) {
    if (!line.is<const char*>()) return false;
    lines.emplace_back(line.as<const char*>());
  }
  if (!home_verse::packLines(lines, pick)) return false;

  pick.start = *start;
  pick.spine = json["s"].as<uint16_t>();
  pick.rung = rung;
  memcpy(pick.reference, reference, referenceBytes + 1);
  return true;
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

Restore fromJson(const JsonVariantConst doc, const char* const buildId, const uint8_t rungCount,
                 home_verse::Entry& out) {
  out.valid = false;
  if (!persist::isKnownFormatVersion(doc["v"] | 0, FORMAT_VERSION)) return Restore::UnknownFormat;
  if (!doc["b"].is<const char*>() || strcmp(doc["b"].as<const char*>(), buildId) != 0) return Restore::OtherBuild;

  if (!readKey(doc["k"], out.key)) return Restore::Malformed;
  if (!doc["e"].is<uint8_t>()) return Restore::Malformed;
  const std::optional<home_verse::Empty> empty = emptyFromCode(doc["e"].as<uint8_t>());
  if (!empty) return Restore::Malformed;

  const bool hasPick = *empty == home_verse::Empty::None;
  // HomeVerse::scan scans only when the file exists, so a pick from a missing file is corrupt.
  if (hasPick && !out.key.fileExists) return Restore::Malformed;
  if (hasPick == doc["p"].isNull()) return Restore::Malformed;
  if (hasPick && !readPick(doc["p"], rungCount, out.pick)) return Restore::Malformed;

  out.empty = *empty;
  out.hasPick = hasPick;
  out.valid = true;
  return Restore::Restored;
}

}  // namespace HomeVerseDoc
