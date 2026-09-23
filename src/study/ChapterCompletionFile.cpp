#include "ChapterCompletionFile.h"

#include <ArduinoJson.h>
#include <HalStorage.h>
#include <Logging.h>
#include <PersistableStore.h>
#include <TempAdoption.h>

namespace {

constexpr const char* MODULE = "COMPLETE";
constexpr const char* COMPLETION_DIR = "/.berean/completion";

}  // namespace

namespace ChapterCompletionFile {

std::string path(const std::string& pubKey) { return std::string(COMPLETION_DIR) + "/" + pubKey + ".json"; }

LoadResult load(const std::string& pubKey, study::ChapterCompletion& record) {
  const std::string primaryPath = path(pubKey);
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

  switch (tempAdoptionAction(primaryStatus, tempExists, tempParsed)) {
    case TempAdoptionAction::UseLoaded:
      if (record.fromJson(primaryJson.as<JsonVariantConst>())) return LoadResult::Loaded;
      LOG_ERR(MODULE, "Rejected %s (future format version, or corrupt)", primaryPath.c_str());
      return LoadResult::Failed;

    case TempAdoptionAction::ReportEmpty:
      return LoadResult::Empty;

    case TempAdoptionAction::PromoteTempAndUseIt: {
      if (!Storage.rename(tmpPath.c_str(), primaryPath.c_str())) {
        LOG_ERR(MODULE, "Failed to promote %s into place", tmpPath.c_str());
      }
      if (record.fromJson(tempJson.as<JsonVariantConst>())) return LoadResult::RecoveredFromTemp;
      LOG_ERR(MODULE, "Recovered %s but rejected its contents", primaryPath.c_str());
      return LoadResult::Failed;
    }

    case TempAdoptionAction::DeleteTempReportEmpty:
      Storage.remove(tmpPath.c_str());
      return LoadResult::Empty;

    case TempAdoptionAction::ReportFailed:
      return LoadResult::Failed;
  }
  return LoadResult::Failed;
}

SaveResult save(const std::string& pubKey, const study::ChapterCompletion& record) {
  JsonDocument json;
  if (!record.toJsonWithinBudget(json)) {
    LOG_ERR(MODULE, "Completion record for %s exceeds the save budget; not written", pubKey.c_str());
    return SaveResult::TooLarge;
  }

  Storage.mkdir(COMPLETION_DIR);
  const std::string target = path(pubKey);
  return PersistableStoreBase::writeDocToFileAtomic(target.c_str(), json) ? SaveResult::Ok : SaveResult::WriteFailed;
}

}  // namespace ChapterCompletionFile
