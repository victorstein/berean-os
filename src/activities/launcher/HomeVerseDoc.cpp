#include "activities/launcher/HomeVerseDoc.h"

#include <FormatVersion.h>
#include <StudyStore/Unit.h>

#include <cstdio>
#include <cstring>
#include <optional>
#include <string>
#include <vector>

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

std::optional<home_verse::Empty> emptyFromCode(const int code) {
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
  static_cast<void>(rungCount);

  const JsonVariantConst key = doc["k"];
  out.key.dated = key["dt"] | false;
  out.key.day.year = key["y"] | 0;
  out.key.day.month = key["m"] | 0;
  out.key.day.day = key["d"] | 0;
  out.key.fileExists = key["f"] | false;
  out.key.fileBytes = key["n"] | 0u;

  const std::optional<home_verse::Empty> empty = emptyFromCode(doc["e"] | 0);
  if (!empty) return Restore::Malformed;
  out.empty = *empty;
  out.hasPick = *empty == home_verse::Empty::None;
  if (out.hasPick) {
    const JsonVariantConst pick = doc["p"];
    const std::optional<study::Unit> start = study::unitFromCompact(std::string(pick["u"] | ""));
    if (!start) return Restore::Malformed;
    std::vector<std::string> lines;
    for (const JsonVariantConst line : pick["l"].as<JsonArrayConst>()) lines.emplace_back(line | "");
    if (!home_verse::packLines(lines, out.pick)) return Restore::Malformed;
    out.pick.start = *start;
    out.pick.spine = pick["s"] | 0;
    snprintf(out.pick.reference, sizeof(out.pick.reference), "%s", pick["r"] | "");
    out.pick.rung = pick["rung"] | 0;
  }
  out.valid = true;
  return Restore::Restored;
}

}  // namespace HomeVerseDoc
