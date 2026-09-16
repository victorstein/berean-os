#pragma once

#include <ArduinoJson.h>
#include <SaveBudget.h>

#include <cstddef>
#include <string>
#include <vector>

#include "../BookmarkEntry.h"

// Format rules for a book's bookmark file: the JSON shape, the version and the
// byte budget. No storage access -- the shell is src/util/BookmarkFile.
//
// Deliberately free of <Arduino.h> (it reaches only ArduinoJson and
// BookmarkEntry) so the format can be host-tested; BookmarkFile.cpp cannot be.
// It also never calls measureJson: that template is what PersistableStore.h
// exists to keep out of per-store translation units, and BookmarkFile.cpp,
// which already includes PersistableStore.h, is the only place that measures.
namespace BookmarkDoc {

// Bumped only for a shape a previous build could not read. A file written
// before versioning carries no "v" and is read as version 1.
inline constexpr int FORMAT_VERSION = 1;

// Serialised bytes above which save() refuses to GROW the file: headroom under
// SDCardManager::readFile's silent 50,000-byte truncation. Taken from the
// shared constant rather than repeated, so bookmarks follow if it ever moves --
// src/study/TagPaletteFile.cpp:70 is the store that does this right.
inline constexpr size_t SAVE_BYTE_BUDGET = persist::DEFAULT_SAVE_BUDGET;

// A summary is a page's first words, bounded at creation by BookmarkUtil. A
// file on an SD card is not a trusted input, so the load path re-bounds it.
inline constexpr size_t MAX_SUMMARY_BYTES = 72;

void toJson(const std::vector<BookmarkEntry>& bookmarks, JsonDocument& doc);

// Fills `bookmarks` from `doc`. Returns false only when the document is not an
// object or carries a version this build does not know -- NEVER for size. A
// file too big to write back is still read in full: dropping the tail would
// lose it permanently on the next save.
bool fromJson(JsonVariantConst doc, std::vector<BookmarkEntry>& bookmarks);

}  // namespace BookmarkDoc
