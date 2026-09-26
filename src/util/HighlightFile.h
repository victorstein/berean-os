#pragma once

#include <TempAdoption.h>

#include <cstddef>
#include <cstdint>
#include <string>

#include "Epub/HighlightDoc.h"

// Storage shell for a book's highlights: moves bytes between HighlightDoc and
// /.crosspoint/highlights/. All format rules live in HighlightDoc; .tmp
// adoption and the never-overwrite-on-failure rule live in
// PersistableStoreBase::loadAdopting and Serialization/TempAdoption.h, and the
// save budget check in util/HighlightFileAction.h. Those are host-tested; this
// file has no host suite of its own.
//
// Single-writer only: the static helpers here take no lock. If the web
// server ever writes highlights alongside the main task, add a mutex --
// PersistableStore.h documents the same hazard for storeMutex.
namespace HighlightFile {

using LoadResult = AdoptedLoad;

// Loads the highlights for bookPath.
//
// Failed means the bytes could not be read, parsed, or validated, and the
// file may still hold the user's data -- the caller MUST NOT call save()
// after Failed. A status is returned rather than a bool because collapsing
// these four states into one is precisely the bug the foundations plan fixed
// in readDocFromFile.
LoadResult load(const std::string& bookPath, HighlightDoc& doc);

enum class SaveResult : uint8_t { Ok, TooLarge, WriteFailed };

// Serialised bytes above which save refuses. Headroom under SDCardManager's
// silent 50,000-byte read truncation; see the worst-case measurement in
// HighlightDocTest for why MAX_HIGHLIGHTS alone cannot bound this.
constexpr size_t SAVE_BYTE_BUDGET = 45000;

// Saves atomically. Measures the serialised document and refuses BEFORE
// touching any file when it exceeds SAVE_BYTE_BUDGET, so the caller can
// surface an error instead of writing a file that reads back truncated and
// unparseable forever.
SaveResult save(const std::string& bookPath, const HighlightDoc& doc);

}  // namespace HighlightFile
