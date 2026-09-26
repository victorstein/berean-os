#include "TagPaletteFile.h"

#include <ArduinoJson.h>
#include <HalStorage.h>
#include <Logging.h>
#include <PersistableStore.h>
#include <SaveBudget.h>
#include <SdPaths.h>

#include <string>

namespace {

constexpr const char* MODULE = "TAGS";

}  // namespace

namespace TagPaletteFile {

LoadResult load(study::TagPalette& palette) {
  return PersistableStoreBase::loadAdopting(
      PATH, &PersistableStoreBase::readDocFromFileChecked,
      [](void* target, JsonVariantConst json) { return static_cast<study::TagPalette*>(target)->fromJson(json); },
      &palette);
}

SaveResult save(const study::TagPalette& palette) {
  JsonDocument json;
  palette.toJson(json);

  if (measureJson(json) > persist::DEFAULT_SAVE_BUDGET) {
    LOG_ERR(MODULE, "Tag palette exceeds the save budget; not written");
    return SaveResult::TooLarge;
  }

  Storage.mkdir(sdpaths::BEREAN_DIR);
  return PersistableStoreBase::writeDocToFileAtomic(PATH, json) ? SaveResult::Ok : SaveResult::WriteFailed;
}

}  // namespace TagPaletteFile
