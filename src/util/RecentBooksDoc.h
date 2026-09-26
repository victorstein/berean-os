#pragma once

#include <ArduinoJson.h>

#include <cstddef>
#include <vector>

#include "../RecentBook.h"

// Format rules for /.crosspoint/recent.json: the JSON shape, the field caps, and
// the byte budget derived from them. No storage access — the shell is
// src/RecentBooksStore.
//
// Deliberately free of <Arduino.h> (it reaches only ArduinoJson and RecentBook)
// so the format can be host-tested; RecentBooksStore.cpp cannot be. It also
// never calls measureJson or serializeJson: those templates are what
// PersistableStore.h exists to keep out of per-store translation units, and
// saveToFileAtomic() is the only place that measures.
namespace RecentBooksDoc {

// Written as "v". A file from before versioning has none and reads as 1.
inline constexpr int FORMAT_VERSION = 1;

// Entry-count cap. size_t, so it compares cleanly against vector::size().
inline constexpr size_t MAX_RECENT_BOOKS = 10;

// Display caps, picked against what the recents surfaces can render rather than
// against the budget. Both sit ~1.8x above the longest real publication title
// and author, and above every other surface's own cap for the same field (48
// bytes at LauncherActivity.cpp:83 and PublicationsActivity.cpp:56, 40 at
// LauncherActivity.cpp:103, 30 at :132).
//
// title is not display-only: the launcher picks the Bible tile's book by
// substring-matching it (LauncherActivity.cpp:96-99). The markers sit at byte 16
// of a real Spanish NWT title, far under this cap.
inline constexpr size_t MAX_TITLE_BYTES = 128;
inline constexpr size_t MAX_AUTHOR_BYTES = 96;

// path is the store's key and is NEVER truncated: pruneMissing() erases any
// entry whose path does not resolve, so a shortened one deletes itself on the
// next boot. It gets a budget allowance instead — two full FAT LFN components
// (SdFat FsStructs.h), roughly 7x a realistic path on this device.
inline constexpr size_t PATH_BUDGET_ALLOWANCE = 512;
// coverBmpPath is bounded by construction at 57 bytes: "/.crosspoint" (12) +
// "/epub_" (6) + at most 20 hash digits + "/thumb_[HEIGHT].bmp" (19). See
// Epub.h:48 and Epub.cpp:653.
inline constexpr size_t COVER_PATH_BUDGET_ALLOWANCE = 128;

// {"v":1,"books":[]}
inline constexpr size_t DOC_WRAPPER_BYTES = 18;
// {"path":"","title":"","author":"","coverBmpPath":""}
inline constexpr size_t ENTRY_OVERHEAD_BYTES = 52;
// Worst-case JSON escape expansion. ArduinoJson 7.4.2 emits two bytes for
// " \ \b \f \n \r \t and passes every other control character through raw. The
// one exception is NUL, which becomes a six-byte \u escape, so normalise()
// erases it and this factor holds. SdFat rejects ", \ and everything below 0x20
// in both a FAT LFN and an exFAT name, so a path THIS FIRMWARE writes serialises
// 1:1 and is not multiplied below. A hand-edited or corrupted recent.json could
// carry one: nothing re-bounds path on load, by design, and the consequence is
// the same save refusal PATH_BUDGET_ALLOWANCE already accepts.
inline constexpr size_t ESCAPE_FACTOR = 2;

constexpr size_t worstCaseBytes() {
  return DOC_WRAPPER_BYTES + (MAX_RECENT_BOOKS - 1) /* commas between entries */
         + MAX_RECENT_BOOKS * (ENTRY_OVERHEAD_BYTES + ESCAPE_FACTOR * (MAX_TITLE_BYTES + MAX_AUTHOR_BYTES) +
                               PATH_BUDGET_ALLOWANCE + COVER_PATH_BUDGET_ALLOWANCE);
}

// Derived from the caps above, not a round number: recompute it, do not tidy it.
// The worst case fits by exactly zero bytes, so a serialiser change that moves
// the measurement fails RecentBooksDocTest — raise a named allowance there
// rather than loosening the assertion.
inline constexpr size_t SAVE_BUDGET = worstCaseBytes();

// Erases any embedded NUL, then caps title and author through utf8SafeSummary —
// never resize(), which can cut mid-sequence and produce invalid UTF-8 that the
// next save serialises. path and coverBmpPath are left alone. Returns true when
// anything changed, which is what drives the load-side resave.
bool normalise(RecentBook& book);

void toJson(const std::vector<RecentBook>& books, JsonDocument& doc);

// Fills `books`, capping the count at MAX_RECENT_BOOKS and re-bounding every
// entry — a file on an SD card is not a trusted input. Sets `needsResave` when
// normalise() changed anything. A missing or non-array "books" key is tolerated
// as an empty list; a JSON parse error is handled upstream in
// PersistableStoreBase::readDocFromFileChecked. Returns false, leaving `books`
// untouched, for a format version this build does not know.
bool fromJson(JsonVariantConst doc, std::vector<RecentBook>& books, bool& needsResave);

}  // namespace RecentBooksDoc
