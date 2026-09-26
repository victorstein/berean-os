#include "PassageFile.h"

#include <ArduinoJson.h>
#include <HalStorage.h>
#include <Logging.h>
#include <PersistableStore.h>
#include <SdPaths.h>

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

DocReadStatus readInto(const char* path, JsonDocument& json) {
  if (!Storage.exists(path)) return classifyDocRead(false, false, false);

  HalFile file;
  if (!Storage.openFileForRead(MODULE, path, file)) {
    LOG_ERR(MODULE, "Failed to open %s", path);
    return classifyDocRead(true, true, false);
  }
  if (file.size() == 0) {
    LOG_ERR(MODULE, "%s is empty", path);
    return classifyDocRead(true, true, false);
  }

  HalFileReader reader(file);
  const auto error = deserializeJson(json, reader);
  if (error) {
    LOG_ERR(MODULE, "JSON parse error in %s: %s", path, error.c_str());
    return classifyDocRead(true, false, true);
  }
  return classifyDocRead(true, false, false);
}

}  // namespace

namespace PassageFile {

std::string path(const std::string& pubKey) { return std::string(sdpaths::PASSAGES_DIR) + "/" + pubKey + ".json"; }

LoadResult load(const std::string& pubKey, study::PassageDoc& doc) {
  const std::string primaryPath = path(pubKey);
  return PersistableStoreBase::loadAdopting(
      primaryPath.c_str(), readInto,
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
