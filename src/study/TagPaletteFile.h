#pragma once

#include <SdPaths.h>
#include <TempAdoption.h>

#include <cstdint>

#include "StudyStore/TagPalette.h"

// Moves bytes between TagPalette and /.berean/tags.json.
//
// The palette is small -- the user's real 48 tags serialise to well under 2 KB
// -- so the ordinary PersistableStore read path is safe here, unlike the
// passages file.
namespace TagPaletteFile {

using LoadResult = AdoptedLoad;

// Failed means the file may still hold the user's vocabulary. The caller MUST
// latch saving off: a palette overwritten with an empty one orphans the tag ids
// every passage carries.
LoadResult load(study::TagPalette& palette);

enum class SaveResult : uint8_t { Ok, TooLarge, WriteFailed };

SaveResult save(const study::TagPalette& palette);

inline constexpr const char* PATH = sdpaths::TAGS_FILE;

}  // namespace TagPaletteFile
