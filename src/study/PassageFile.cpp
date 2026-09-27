#include "PassageFile.h"

#include <ArduinoJson.h>
#include <HalStorage.h>
#include <Logging.h>
#include <PersistableStore.h>
#include <SdPaths.h>

namespace {

constexpr const char* MODULE = "PASSAGE";

}  // namespace

namespace PassageFile {

std::string path(const std::string& pubKey) { return std::string(sdpaths::PASSAGES_DIR) + "/" + pubKey + ".json"; }

LoadResult load(const std::string& pubKey, study::PassageDoc& doc) {
  const std::string primaryPath = path(pubKey);
  return PersistableStoreBase::loadAdopting(
      primaryPath.c_str(), PersistableStoreBase::readDocFromFileStreamed,
      [](void* target, JsonVariantConst json) { return static_cast<study::PassageDoc*>(target)->fromJson(json); },
      &doc);
}

SaveResult save(const std::string& pubKey, const study::PassageDoc& doc) {
  JsonDocument json;
  doc.toJson(json);

  if (measureJson(json) > study::PassageDoc::SAVE_BYTE_BUDGET) {
    LOG_ERR(MODULE, "Passages for %s exceed the save budget; not written", pubKey.c_str());
    return SaveResult::TooLarge;
  }

  Storage.mkdir(sdpaths::PASSAGES_DIR);
  const std::string target = path(pubKey);
  return PersistableStoreBase::writeDocToFileAtomic(target.c_str(), json) ? SaveResult::Ok : SaveResult::WriteFailed;
}

}  // namespace PassageFile
