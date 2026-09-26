#include "ChapterCompletionFile.h"

#include <ArduinoJson.h>
#include <HalStorage.h>
#include <Logging.h>
#include <PersistableStore.h>
#include <SdPaths.h>

namespace {

constexpr const char* MODULE = "COMPLETE";

}  // namespace

namespace ChapterCompletionFile {

std::string path(const std::string& pubKey) { return std::string(sdpaths::COMPLETION_DIR) + "/" + pubKey + ".json"; }

LoadResult load(const std::string& pubKey, study::ChapterCompletion& record) {
  const std::string primaryPath = path(pubKey);
  return PersistableStoreBase::loadAdopting(
      primaryPath.c_str(), &PersistableStoreBase::readDocFromFileChecked,
      [](void* target, JsonVariantConst json) {
        return static_cast<study::ChapterCompletion*>(target)->fromJson(json);
      },
      &record);
}

SaveResult save(const std::string& pubKey, const study::ChapterCompletion& record) {
  JsonDocument json;
  if (!record.toJsonWithinBudget(json)) {
    LOG_ERR(MODULE, "Completion record for %s exceeds the save budget; not written", pubKey.c_str());
    return SaveResult::TooLarge;
  }

  Storage.mkdir(sdpaths::COMPLETION_DIR);
  const std::string target = path(pubKey);
  return PersistableStoreBase::writeDocToFileAtomic(target.c_str(), json) ? SaveResult::Ok : SaveResult::WriteFailed;
}

}  // namespace ChapterCompletionFile
