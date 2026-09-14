#include "TagPaletteFile.h"

#include <ArduinoJson.h>
#include <HalStorage.h>
#include <Logging.h>
#include <PersistableStore.h>
#include <SaveBudget.h>

#include <string>

#include "util/HighlightFileAction.h"

namespace {

constexpr const char* MODULE = "TAGS";
constexpr const char* BEREAN_DIR = "/.berean";

}  // namespace

namespace TagPaletteFile {

LoadResult load(study::TagPalette& palette) {
  const std::string primaryPath = PATH;
  const std::string tmpPath = primaryPath + ".tmp";

  JsonDocument primaryJson;
  const DocReadStatus primaryStatus = PersistableStoreBase::readDocFromFileChecked(primaryPath.c_str(), primaryJson);

  bool tempExists = false;
  bool tempParsed = false;
  JsonDocument tempJson;
  if (primaryStatus == DocReadStatus::Missing) {
    tempExists = Storage.exists(tmpPath.c_str());
    if (tempExists) {
      tempParsed = PersistableStoreBase::readDocFromFileChecked(tmpPath.c_str(), tempJson) == DocReadStatus::Ok;
    }
  }

  switch (highlightLoadAction(primaryStatus, tempExists, tempParsed)) {
    case HighlightLoadAction::UseLoaded:
      if (palette.fromJson(primaryJson.as<JsonVariantConst>())) return LoadResult::Loaded;
      LOG_ERR(MODULE, "Rejected %s (future format version?)", primaryPath.c_str());
      return LoadResult::Failed;

    case HighlightLoadAction::ReportEmpty:
      return LoadResult::Empty;

    case HighlightLoadAction::PromoteTempAndUseIt: {
      if (!Storage.rename(tmpPath.c_str(), primaryPath.c_str())) {
        LOG_ERR(MODULE, "Failed to promote %s into place", tmpPath.c_str());
      }
      if (palette.fromJson(tempJson.as<JsonVariantConst>())) return LoadResult::RecoveredFromTemp;
      return LoadResult::Failed;
    }

    case HighlightLoadAction::DeleteTempReportEmpty:
      Storage.remove(tmpPath.c_str());
      return LoadResult::Empty;

    case HighlightLoadAction::ReportFailed:
      return LoadResult::Failed;
  }
  return LoadResult::Failed;
}

SaveResult save(const study::TagPalette& palette) {
  JsonDocument json;
  palette.toJson(json);

  if (measureJson(json) > persist::DEFAULT_SAVE_BUDGET) {
    LOG_ERR(MODULE, "Tag palette exceeds the save budget; not written");
    return SaveResult::TooLarge;
  }

  Storage.mkdir(BEREAN_DIR);
  return PersistableStoreBase::writeDocToFileAtomic(PATH, json) ? SaveResult::Ok : SaveResult::WriteFailed;
}

}  // namespace TagPaletteFile
