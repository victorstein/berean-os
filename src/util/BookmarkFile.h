#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "../BookmarkEntry.h"

// Per-book bookmark persistence. Takes the book's path; bookmark-file path
// derivation (BookmarkUtil) and directory creation are hidden inside.
//
// Single-writer only: these helpers take no lock of their own. HalStorage
// serialises each CALL, not a sequence, and save() stats the existing file and
// then writes it as separate acquisitions -- a second writer could change the
// file between the two and make the shrink decision race. The owning task is
// the main/UI task; every call site is a reader-side activity. If the web
// server ever writes bookmarks alongside it, add a mutex.
namespace BookmarkFile {

enum class LoadResult : uint8_t {
  Loaded,             // file read and parsed
  Empty,              // genuinely absent -- safe to save over
  RecoveredFromTemp,  // a failed rename left .tmp as the only copy; promoted
  Failed,             // unreadable or unparseable -- DATA MAY STILL EXIST
};

// Loads the bookmarks for bookPath. The vector is cleared first.
//
// Failed means the bytes could not be read or parsed and the file may still
// hold the user's data -- the caller MUST NOT call save() after Failed. A
// status is returned rather than a bool because collapsing "no file yet" into
// "unreadable" is what let an empty list overwrite a real file.
LoadResult load(const std::string& bookPath, std::vector<BookmarkEntry>& bookmarks);

enum class SaveResult : uint8_t { Ok, TooLarge, WriteFailed };

// Saves atomically, creating the bookmarks directory as needed. Measures the
// serialised document and refuses BEFORE touching any file when it would grow
// past BookmarkDoc::SAVE_BYTE_BUDGET; a document already over the budget may
// still be written when it is strictly smaller than the file it replaces.
SaveResult save(const std::string& bookPath, const std::vector<BookmarkEntry>& bookmarks);

}  // namespace BookmarkFile
