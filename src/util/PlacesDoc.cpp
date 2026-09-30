#include "PlacesDoc.h"

#include <FormatVersion.h>
#include <Utf8.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>

namespace PlacesDoc {
namespace {

constexpr uint8_t LAST_BIBLE_BOOK = 66;

// The same two helpers as RecentBooksDoc.cpp: an embedded NUL would break ESCAPE_FACTOR, and
// utf8SafeSummary clamps before utf8SafeTruncateBuffer, which trusts its length (Utf8.cpp:148).
bool eraseNuls(std::string& field) {
  const auto it = std::remove(field.begin(), field.end(), '\0');
  if (it == field.end()) return false;
  field.erase(it, field.end());
  return true;
}

bool capField(std::string& field, const size_t maxBytes) {
  std::string capped = utf8SafeSummary(field, maxBytes);
  if (capped == field) return false;
  field = std::move(capped);
  return true;
}

bool isBiblePlace(const study::Unit& unit) {
  return unit.kind == study::UnitKind::Verse && unit.book >= 1 && unit.book <= LAST_BIBLE_BOOK && unit.major > 0;
}

bool sameChapter(const study::Unit& a, const study::Unit& b) { return a.book == b.book && a.major == b.major; }

}  // namespace

std::optional<PlaceUnit> placeUnit(const study::DocumentUnits& units, const uint32_t pageOffset) {
  if (units.kind != study::UnitKind::Verse || units.book == 0 || units.anchors.empty()) return std::nullopt;
  const study::Unit resolved = study::resolve(units, pageOffset);
  if (resolved.kind == study::UnitKind::Verse) {
    if (resolved.major == 0) return std::nullopt;
    return PlaceUnit{resolved, false};
  }
  const study::UnitAnchor& first = units.anchors.front();
  if (first.major == 0) return std::nullopt;
  return PlaceUnit{study::Unit{study::UnitKind::Verse, units.book, first.major, first.minor, 0}, true};
}

std::string formatReference(const std::string_view book, const study::Unit& unit, const bool chapterOnly) {
  std::string reference(book);
  reference += ' ';
  reference += std::to_string(unit.major);
  if (!chapterOnly) {
    reference += ':';
    reference += std::to_string(unit.minor);
  }
  return utf8SafeSummary(std::move(reference), MAX_REFERENCE_BYTES);
}

void formatChipLabel(const std::string_view abbreviation, const Place& place, char* out, const size_t outBytes) {
  if (abbreviation.empty()) {
    snprintf(out, outBytes, "%s", place.reference.c_str());
    return;
  }
  // An abbreviation is at most 15 bytes (BibleBookNameTable::ABBREV_BYTES) and the numbers at
  // most 11, so this never reaches MAX_REFERENCE_BYTES and never cuts a UTF-8 sequence.
  if (place.chapterOnly) {
    snprintf(out, outBytes, "%.*s %u", static_cast<int>(abbreviation.size()), abbreviation.data(),
             static_cast<unsigned>(place.unit.major));
  } else {
    snprintf(out, outBytes, "%.*s %u:%u", static_cast<int>(abbreviation.size()), abbreviation.data(),
             static_cast<unsigned>(place.unit.major), static_cast<unsigned>(place.unit.minor));
  }
}

size_t pickRecent(const std::vector<Place>& places, const std::optional<study::Unit>& onScreen, Place* out,
                  const size_t max) {
  size_t picked = 0;
  for (const Place& place : places) {
    if (picked >= max) break;
    if (onScreen && sameChapter(place.unit, *onScreen)) continue;
    out[picked++] = place;
  }
  return picked;
}

std::string describe(const std::vector<Place>& places) {
  std::string line;
  line.reserve(places.size() * (MAX_REFERENCE_BYTES + 3));
  for (const Place& place : places) {
    if (!line.empty()) line += " | ";
    line += place.reference;
  }
  return line;
}

bool normalise(Place& place) {
  bool changed = eraseNuls(place.reference);
  changed = capField(place.reference, MAX_REFERENCE_BYTES) || changed;
  return changed;
}

bool record(std::vector<Place>& places, Place place) {
  normalise(place);
  if (!places.empty() && places.front() == place) return false;
  places.erase(std::remove_if(places.begin(), places.end(),
                              [&](const Place& existing) { return sameChapter(existing.unit, place.unit); }),
               places.end());
  places.insert(places.begin(), std::move(place));
  if (places.size() > MAX_PLACES) places.resize(MAX_PLACES);
  return true;
}

void toJson(const std::vector<Place>& places, JsonDocument& doc) {
  doc["v"] = FORMAT_VERSION;
  JsonArray arr = doc["places"].to<JsonArray>();
  for (const Place& place : places) {
    JsonObject obj = arr.add<JsonObject>();
    obj["u"] = study::unitToCompact(place.unit);
    obj["r"] = place.reference;
    obj["c"] = place.chapterOnly;
    obj["s"] = place.spineIndex;
    obj["o"] = place.visibleTextOffset;
  }
}

bool fromJson(const JsonVariantConst doc, std::vector<Place>& places, bool& needsResave) {
  needsResave = false;
  const int version = doc["v"] | 0;
  if (!persist::isKnownFormatVersion(version, FORMAT_VERSION)) return false;
  places.clear();
  places.reserve(MAX_PLACES);

  // A missing or non-array "places" key iterates zero times -- the "no data yet" case.
  for (JsonObjectConst obj : doc["places"].as<JsonArrayConst>()) {
    if (places.size() >= MAX_PLACES) {
      needsResave = true;
      break;
    }
    // Strings are read as const char*, never | std::string(""): see PersistableStore.h.
    const auto unit = study::unitFromCompact(std::string(obj["u"] | ""));
    const bool repeatsAChapter =
        unit && std::any_of(places.begin(), places.end(),
                            [&](const Place& kept) { return sameChapter(kept.unit, *unit); });
    if (!unit || !isBiblePlace(*unit) || repeatsAChapter) {
      needsResave = true;
      continue;
    }
    Place place;
    place.unit = *unit;
    place.reference = obj["r"] | "";
    place.chapterOnly = obj["c"] | false;
    const uint32_t spine = obj["s"] | 0u;
    if (spine > UINT16_MAX) needsResave = true;
    place.spineIndex = spine > UINT16_MAX ? 0 : static_cast<uint16_t>(spine);
    place.visibleTextOffset = obj["o"] | 0u;
    if (normalise(place)) needsResave = true;
    places.push_back(std::move(place));
  }
  return true;
}

}  // namespace PlacesDoc
