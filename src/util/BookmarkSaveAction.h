#pragma once

#include <cstddef>
#include <cstdint>

// Pure decision logic behind BookmarkFile::save.
//
// This header carries the one rule specific to bookmarks, free of Arduino and
// HalStorage, so it can be host-tested on its own. BookmarkFile.cpp itself has
// no host suite yet; test/stubs now carries an Arduino.h and an in-memory
// Storage fake it can be built against (see test/storage_io/).
//
// Growth stops at `budget`. A document already over it may still be written
// when it is strictly SHRINKING and stays readable, so a bookmark file
// inherited from a build with no budget can be deleted back under the budget
// instead of freezing read-only: without this arm every delete measures over
// budget, is refused, and the caller's rollback puts the entry back.
enum class BookmarkSaveAction : uint8_t { Write, RefuseTooLarge };

// `bytesOnDisk` is the size of the file being replaced; 0 when none exists, and
// also 0 when the caller has not looked, which is safe because a document at or
// under `budget` is written whatever is already there.
// `readCap` is the size past which SDCardManager::readFile returns a silently
// truncated string; a file exactly at it still reads whole.
constexpr BookmarkSaveAction bookmarkSaveAction(const size_t measuredBytes, const size_t bytesOnDisk,
                                                const size_t budget, const size_t readCap) {
  if (measuredBytes > readCap) return BookmarkSaveAction::RefuseTooLarge;
  if (measuredBytes <= budget) return BookmarkSaveAction::Write;
  return measuredBytes < bytesOnDisk ? BookmarkSaveAction::Write : BookmarkSaveAction::RefuseTooLarge;
}
