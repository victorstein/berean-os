#pragma once

#include <ArduinoJson.h>

#include <cstdint>
#include <string>
#include <vector>

#include "StudyStore/PassageDoc.h"
#include "StudyStore/PassageText.h"
#include "StudyStore/TagPalette.h"
#include "StudyStore/Unit.h"

// Read-only passes over the Bible's tagged passages, /.berean/passages/bible.json,
// for Predicación's Mis temas. Both stream the file through an ArduinoJson filter.
// Neither writes it, nor renames a leftover .tmp beside it.
//
// The firmware must pass PsramJsonAllocator::json() and ::passageDoc():
// ArduinoJson's pool blocks are under the 4,096-byte PSRAM routing threshold, so
// a nullptr on the device would parse into internal SRAM.
namespace PreachingPassages {

enum class Status : uint8_t { Ok, Missing, Unreadable, OutOfMemory };

// Missing and Unreadable both mean "cannot read the verses" to the UI; they stay
// apart for the log.
constexpr bool isAvailable(const Status status) { return status == Status::Ok; }

struct TagCount {
  study::TagId tag;
  uint16_t passages;
};

struct Passage {
  std::string reference;
  study::PassageText text;  // the whole text, else the snippet
  study::Unit start;
  uint16_t spine = 0;  // documentSpine, a hint
};

// One count per entry of `tags`, in the same order, zero included. `out` is
// empty unless the result is Ok.
Status countByTag(const std::vector<study::TagId>& tags, ArduinoJson::Allocator* json, std::vector<TagCount>& out);

// Every Bible passage carrying `tag`, by spine, then start Unit. `out` is empty
// unless the result is Ok.
Status passagesWithTag(study::TagId tag, const study::PassageDoc::Allocators& allocators, std::vector<Passage>& out);

}  // namespace PreachingPassages
