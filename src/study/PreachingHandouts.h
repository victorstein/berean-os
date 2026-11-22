#pragma once

#include <ArduinoJson.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "StudyStore/PassageDoc.h"
#include "StudyStore/PassageText.h"

// Read-only passes over /preaching-cards.json, the owner's printed preaching cards ("handouts")
// with every verse their jw.org articles cite, for Predicación's Tarjetas. A script beside the
// cards writes it (rfid-cards/src/cards_json.py) and the owner uploads it over File Transfer,
// which refuses dot-directories: hence the SD root, outside /.berean. The device never writes it.
//
// The firmware must pass PsramJsonAllocator::json() and ::passageDoc(), as for PreachingPassages.
namespace PreachingHandouts {

inline constexpr char FILE_PATH[] = "/preaching-cards.json";
inline constexpr int FORMAT_VERSION = 1;
// cards_json.py refuses to write past the same cap.
inline constexpr size_t MAX_FILE_BYTES = 128 * 1024;
// ReaderEntryIntent::HREF_BYTES less its NUL; cards_json.py checks the same bound.
inline constexpr size_t MAX_HREF_BYTES = 63;
// So a handout's index fits PreachingOrigin's uint8_t.
inline constexpr size_t MAX_HANDOUTS = 200;
inline constexpr size_t MAX_TITLE_BYTES = 96;

enum class Status : uint8_t { Ok, Missing, Unreadable, TooNew, OutOfMemory };

struct Summary {
  std::string title;
  uint16_t verses = 0;
};

struct Verse {
  uint16_t section = 0;  // index into Handout::headings
  std::string reference;
  std::string href;
  study::PassageText text;
};

struct Handout {
  std::string title;
  std::vector<std::string> headings;  // one per section; a single-article handout has one, empty
  std::vector<Verse> verses;
};

// Each handout's title and the number of verses load() would return, in file order. `out` is
// empty unless the result is Ok.
Status summaries(ArduinoJson::Allocator* json, std::vector<Summary>& out);

// The handout at `index`, its verse texts through `allocators.text`. `out` is empty unless the
// result is Ok; an index past the end is Unreadable.
Status load(size_t index, const study::PassageDoc::Allocators& allocators, Handout& out);

}  // namespace PreachingHandouts
