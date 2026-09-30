#pragma once

#include <ArduinoJson.h>
#include <StudyStore/UnitAnchors.h>

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "../Place.h"

// Format and list rules for /.berean/places.json: the JSON shape, the caps, the byte budget
// derived from them, and the pure pieces of recording a place. No storage access -- the shell is
// src/PlacesStore. Free of <Arduino.h> so it can be host-tested, and never measures or serialises:
// saveToFileAtomic() is the only place that does (see RecentBooksDoc.h for why).
namespace PlacesDoc {

inline constexpr int FORMAT_VERSION = 1;

inline constexpr size_t MAX_PLACES = 12;

// study::PassageDoc::MAX_REFERENCE_BYTES; StudyStore.cpp asserts the two agree.
inline constexpr size_t MAX_REFERENCE_BYTES = 48;

// {"v":1,"places":[]}
inline constexpr size_t DOC_WRAPPER_BYTES = 19;
// {"u":"","r":"","c":,"s":,"o":}
inline constexpr size_t ENTRY_OVERHEAD_BYTES = 30;
// v:255:65535:65535:4294967295 -- every field at its type's maximum.
inline constexpr size_t MAX_UNIT_BYTES = 28;
// ArduinoJson 7.4.2 escapes " \ \b \f \n \r \t to two bytes; normalise() erases NUL, the only
// six-byte escape (RecentBooksDoc.h has the full argument).
inline constexpr size_t ESCAPE_FACTOR = 2;
inline constexpr size_t MAX_BOOL_BYTES = 5;     // false
inline constexpr size_t MAX_SPINE_BYTES = 5;    // 65535
inline constexpr size_t MAX_OFFSET_BYTES = 10;  // 4294967295

constexpr size_t worstCaseBytes() {
  return DOC_WRAPPER_BYTES + (MAX_PLACES - 1) /* commas between entries */ +
         MAX_PLACES * (ENTRY_OVERHEAD_BYTES + MAX_UNIT_BYTES + ESCAPE_FACTOR * MAX_REFERENCE_BYTES + MAX_BOOL_BYTES +
                       MAX_SPINE_BYTES + MAX_OFFSET_BYTES);
}

// Derived from the caps above, not a round number: recompute it, do not tidy it. The worst case
// fits by exactly zero bytes, which PlacesDocTest pins.
inline constexpr size_t SAVE_BUDGET = worstCaseBytes();

struct PlaceUnit {
  study::Unit unit;
  bool chapterOnly = false;
};

// The place for a page whose first visible codepoint is `pageOffset`: the verse containing it, or
// the chapter's first verse at offset 0 when the page starts before it (a chapter heading), marked
// chapterOnly. nullopt unless the document is a Verse document with a book and anchors.
std::optional<PlaceUnit> placeUnit(const study::DocumentUnits& units, uint32_t pageOffset);

// "Revelation 21:4", or "Genesis 1" for a chapter-only place; capped at MAX_REFERENCE_BYTES.
std::string formatReference(std::string_view book, const study::Unit& unit, bool chapterOnly);

// "Rev. 21:4" from the publication's own abbreviation, or the place's reference when there is
// none. `out` must hold MAX_REFERENCE_BYTES + 1.
void formatChipLabel(std::string_view abbreviation, const Place& place, char* out, size_t outBytes);

// Copies up to `max` places into `out`, newest first, skipping any verse of the chapter on screen;
// the cap applies after the skip. Returns how many were copied.
size_t pickRecent(const std::vector<Place>& places, const std::optional<study::Unit>& onScreen, Place* out,
                  size_t max);

// "Genesis 1 | Revelation 21:4", for the save log line.
std::string describe(const std::vector<Place>& places);

// Erases any embedded NUL and caps the reference on a codepoint boundary. True when anything
// changed.
bool normalise(Place& place);

// Moves `place` to the front, dropping any other entry for the same (book, chapter) and the
// oldest past MAX_PLACES. False, and nothing changes, when the head already equals it.
bool record(std::vector<Place>& places, Place place);

void toJson(const std::vector<Place>& places, JsonDocument& doc);

// Fills `places`, dropping (with needsResave) entries that are not Bible places, repeat a chapter,
// or exceed the count; re-bounds every reference. Returns false, leaving `places` untouched, for a
// format version this build does not know -- including an absent one: this file never existed
// unversioned.
bool fromJson(JsonVariantConst doc, std::vector<Place>& places, bool& needsResave);

}  // namespace PlacesDoc
