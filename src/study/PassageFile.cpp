#include "PassageFile.h"

#include <ArduinoJson.h>
#include <HalStorage.h>
#include <Logging.h>
#include <PersistableStore.h>
#include <SdPaths.h>
#include <TempAdoption.h>

namespace {

constexpr const char* MODULE = "PASSAGE";

// ArduinoJson reader over HalFile, so a passages file larger than
// SDCardManager::readFile's 50,000-byte cap still parses in full.
class HalFileReader {
 public:
  explicit HalFileReader(HalFile& file) : file_(file) {}

  int read() { return file_.read(); }

  size_t readBytes(char* buffer, const size_t length) {
    const int got = file_.read(buffer, length);
    return got < 0 ? 0 : static_cast<size_t>(got);
  }

 private:
  HalFile& file_;
};

DocReadStatus readInto(const std::string& path, JsonDocument& json) {
  if (!Storage.exists(path.c_str())) return classifyDocRead(false, false, false);

  HalFile file;
  if (!Storage.openFileForRead(MODULE, path, file)) {
    LOG_ERR(MODULE, "Failed to open %s", path.c_str());
    return classifyDocRead(true, true, false);
  }
  if (file.size() == 0) return classifyDocRead(true, true, false);

  HalFileReader reader(file);
  const auto error = deserializeJson(json, reader);
  if (error) {
    LOG_ERR(MODULE, "JSON parse error in %s: %s", path.c_str(), error.c_str());
    return classifyDocRead(true, false, true);
  }
  return classifyDocRead(true, false, false);
}

}  // namespace

namespace PassageFile {

std::string path(const std::string& pubKey) { return std::string(sdpaths::PASSAGES_DIR) + "/" + pubKey + ".json"; }

LoadResult load(const std::string& pubKey, study::PassageDoc& doc) {
  const std::string primaryPath = path(pubKey);
  const std::string tmpPath = primaryPath + ".tmp";

  JsonDocument primaryJson;
  const DocReadStatus primaryStatus = readInto(primaryPath, primaryJson);

  bool tempExists = false;
  bool tempParsed = false;
  JsonDocument tempJson;
  if (primaryStatus == DocReadStatus::Missing) {
    tempExists = Storage.exists(tmpPath.c_str());
    if (tempExists) tempParsed = readInto(tmpPath, tempJson) == DocReadStatus::Ok;
  }

  switch (tempAdoptionAction(primaryStatus, tempExists, tempParsed)) {
    case TempAdoptionAction::UseLoaded:
      if (doc.fromJson(primaryJson.as<JsonVariantConst>())) return LoadResult::Loaded;
      LOG_ERR(MODULE, "Rejected %s (future format version, or over budget)", primaryPath.c_str());
      return LoadResult::Failed;

    case TempAdoptionAction::ReportEmpty:
      return LoadResult::Empty;

    case TempAdoptionAction::PromoteTempAndUseIt: {
      // Promote first: the rename is what rescues the only surviving copy of
      // the user's data. The primary path is Missing, so nothing can be lost.
      if (!Storage.rename(tmpPath.c_str(), primaryPath.c_str())) {
        LOG_ERR(MODULE, "Failed to promote %s into place", tmpPath.c_str());
      }
      if (doc.fromJson(tempJson.as<JsonVariantConst>())) return LoadResult::RecoveredFromTemp;
      LOG_ERR(MODULE, "Recovered %s but rejected its contents", primaryPath.c_str());
      return LoadResult::Failed;
    }

    case TempAdoptionAction::KeepTempReportEmpty:
      Storage.remove(tmpPath.c_str());
      return LoadResult::Empty;

    case TempAdoptionAction::ReportFailed:
      return LoadResult::Failed;
  }
  return LoadResult::Failed;
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
