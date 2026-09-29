#include "PassageFile.h"

#include <ArduinoJson.h>
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
  // Parsed on the document's own allocator: StudyStore keeps the whole texts in
  // PSRAM, and a default document would copy every one of them into internal SRAM.
  return PersistableStoreBase::loadAdopting(
      primaryPath.c_str(), &PersistableStoreBase::readDocFromFileStreamed,
      [](void* target, JsonVariantConst json) { return static_cast<study::PassageDoc*>(target)->fromJson(json); },
      &doc, doc.jsonAllocator());
}

SaveResult save(const std::string& pubKey, const study::PassageDoc& doc) {
  JsonDocument json = doc.newJsonDocument();
  doc.toJson(json);

  // ArduinoJson drops a value it cannot allocate and reports it only here. The
  // smaller document would pass the budget check and be written over the good file.
  if (json.overflowed()) {
    LOG_ERR(MODULE, "Out of memory serialising passages for %s; not written", pubKey.c_str());
    return SaveResult::WriteFailed;
  }

  if (measureJson(json) > study::PassageDoc::SAVE_BYTE_BUDGET) {
    LOG_ERR(MODULE, "Passages for %s exceed the save budget; not written", pubKey.c_str());
    return SaveResult::TooLarge;
  }

  const std::string target = path(pubKey);
  return PersistableStoreBase::writeDocToFileAtomic(target.c_str(), json) ? SaveResult::Ok : SaveResult::WriteFailed;
}

}  // namespace PassageFile
