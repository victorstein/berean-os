#pragma once

#include <ArduinoJson.h>

#include <cstddef>
#include <cstdint>

#include "activities/launcher/HomeVerseCache.h"

// /.berean/home-verse.json on the card: HomeVerseDoc's format, read and written through
// PersistableStoreBase's adopting read and atomic write. Renderer-free so test/home_layout runs it
// against the in-memory storage fake. Modelled on network/MeetingWeekCache. Loop task only: the
// read can rename a .tmp into place (PersistableStore.h, loadAdopting).
namespace HomeVerseFile {

enum class Load : uint8_t { None, Restored, Unreadable, UnknownFormat, OtherBuild, Malformed, NoBuildId };

// Fills `out` in place; `out.valid` is true only on Restored. `allocator` backs the parse
// (nullptr: ArduinoJson's default).
Load load(const char* buildId, uint8_t rungCount, ArduinoJson::Allocator* allocator, home_verse::Entry& out);

// Writes a valid `entry` atomically. Returns the bytes written, or 0 when nothing was written: an
// invalid entry, an unusable build id, a document that ran out of memory, one over
// HomeVerseDoc::SAVE_BUDGET, or a failed write.
size_t save(const home_verse::Entry& entry, const char* buildId, ArduinoJson::Allocator* allocator);

const char* describe(Load outcome);

}  // namespace HomeVerseFile
