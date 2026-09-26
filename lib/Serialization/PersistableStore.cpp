#include "PersistableStore.h"

#include <HalStorage.h>
#include <Logging.h>
#include <ObfuscationUtils.h>
#include <SdPaths.h>

#include <cstring>
#include <limits>
#include <string>

bool PersistableStoreBase::writeDocToFile(const char* path, const JsonDocument& doc) {
  Storage.mkdir(sdpaths::CROSSPOINT_DIR);
  String json;
  serializeJson(doc, json);
  if (!Storage.writeFile(path, json)) {
    LOG_ERR("PERSIST", "Failed to write %s", path);
    return false;
  }
  return true;
}

bool PersistableStoreBase::writeDocToFileAtomic(const char* path, const JsonDocument& doc) {
  Storage.mkdir(sdpaths::CROSSPOINT_DIR);
  const std::string finalPath = path;
  const std::string tmpPath = finalPath + ".tmp";

  String json;
  serializeJson(doc, json);

  if (!Storage.writeFile(tmpPath.c_str(), json)) {
    LOG_ERR("PERSIST", "Failed to write temp file %s", tmpPath.c_str());
    return false;
  }

  // SdFat's rename does not overwrite an existing destination, so drop the old
  // file first. The brief window where neither exists reads as "no data yet",
  // which is recoverable; a torn file is not.
  Storage.remove(finalPath.c_str());
  if (!Storage.rename(tmpPath.c_str(), finalPath.c_str())) {
    LOG_ERR("PERSIST", "Failed to rename %s into place", finalPath.c_str());
    return false;
  }
  return true;
}

DocReadStatus PersistableStoreBase::readDocFromFileChecked(const char* path, JsonDocument& doc) {
  if (!Storage.exists(path)) {
    return classifyDocRead(false, false, false);  // Expected on first boot — not an error.
  }
  String json = Storage.readFile(path);
  if (json.isEmpty()) {
    LOG_ERR("PERSIST", "Failed to read %s (empty)", path);
    return classifyDocRead(true, true, false);
  }
  const auto error = deserializeJson(doc, json);
  if (error) {
    LOG_ERR("PERSIST", "JSON parse error in %s: %s", path, error.c_str());
    return classifyDocRead(true, false, true);
  }
  return classifyDocRead(true, false, false);
}

DocReadStatus PersistableStoreBase::readDocFromFileAdopting(const char* path, JsonDocument& doc) {
  const DocReadStatus primary = readDocFromFileChecked(path, doc);

  bool tempExists = false;
  bool tempParsed = false;
  std::string tmpPath;
  if (primary == DocReadStatus::Missing) {
    tmpPath = std::string(path) + ".tmp";
    tempExists = Storage.exists(tmpPath.c_str());
    if (tempExists) tempParsed = readDocFromFileChecked(tmpPath.c_str(), doc) == DocReadStatus::Ok;
  }

  const TempAdoptionAction action = tempAdoptionAction(primary, tempExists, tempParsed);
  switch (action) {
    case TempAdoptionAction::PromoteTempAndUseIt:
      // Promote first: the rename is what rescues the only surviving copy. The
      // primary path is Missing, so nothing here can be overwritten.
      if (Storage.rename(tmpPath.c_str(), path)) {
        LOG_INF("PERSIST", "Recovered %s from an interrupted write", path);
      } else {
        // Still Ok: the document is in hand and the .tmp survives for the next
        // boot to retry. Only the rename failed, so do not claim a recovery --
        // that log line is what the on-device test reads as "the file is back".
        LOG_ERR("PERSIST", "Failed to promote %s into place", tmpPath.c_str());
      }
      break;
    case TempAdoptionAction::DeleteTempReportEmpty:
      // deserializeJson leaves the partially parsed document behind, and callers
      // that ignore the status read it immediately. Deliberately NOT extended to
      // the ReportFailed arm: clearing there would make a read-modify-write
      // caller overwrite a corrupt-but-present file instead of merging onto what
      // did parse.
      doc.clear();
      // The .tmp is left alone on purpose. Removing it buys nothing -- the next
      // save truncates it, since SDCardManager::writeFile removes the
      // destination before re-creating it -- and a transient SD read failure is
      // indistinguishable from an empty file, so deleting here could destroy the
      // only surviving copy.
      break;
    default:
      break;
  }
  return adoptedReadStatus(primary, action);
}

bool PersistableStoreBase::readDocFromFile(const char* path, JsonDocument& doc) {
  return readDocFromFileChecked(path, doc) == DocReadStatus::Ok;
}

std::string PersistableStoreBase::extractPassword(JsonVariantConst doc, bool& needsResave) {
  bool valid = false;
  return extractPassword(doc, needsResave, std::numeric_limits<size_t>::max(), valid);
}

std::string PersistableStoreBase::extractPassword(JsonVariantConst doc, bool& needsResave, const size_t maxLength,
                                                  bool& valid) {
  valid = true;
  bool ok = false;
  bool tooLong = false;
  std::string pass = obfuscation::deobfuscateFromBase64(doc["password_obf"] | "", maxLength, &ok, &tooLong);
  if (tooLong) {
    valid = false;
    return "";
  }
  if (!ok) {
    // Deobfuscation failed — fall back to legacy plaintext password.
    const char* legacyPassword = doc["password"] | "";
    const size_t legacyLength = strlen(legacyPassword);
    if (legacyLength > maxLength) {
      valid = false;
      return "";
    }
    pass.assign(legacyPassword, legacyLength);
    if (!pass.empty()) needsResave = true;
  }
  // A successfully decoded empty string is a legitimate value; preserve as-is.
  return pass;
}
