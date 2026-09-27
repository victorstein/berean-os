#include "PersistableStore.h"

#include <BufferedFile.h>
#include <HalStorage.h>
#include <Logging.h>
#include <ObfuscationUtils.h>

#include <cstring>
#include <limits>
#include <string>

namespace {

// One SdFat sector. Under the 4 KB PSRAM routing threshold, so internal SRAM.
constexpr size_t WRITE_BUFFER_BYTES = 512;

// ArduinoJson reader over HalFile, so a file larger than
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

// ArduinoJson writer over a BufferedFileWriter. ArduinoJson writes strings one
// character at a time, and every HalFile::write takes storageMutex, hence the
// buffer. Reports every byte as taken: a short write surfaces through
// BufferedFileWriter::flush() instead.
class JsonFileWriter {
 public:
  explicit JsonFileWriter(serialization::BufferedFileWriter& out) : out_(out) {}

  size_t write(const uint8_t c) {
    out_.write(&c, 1);
    return 1;
  }

  size_t write(const uint8_t* buffer, const size_t length) {
    out_.write(buffer, length);
    return length;
  }

 private:
  serialization::BufferedFileWriter& out_;
};

void ensureParentDirectory(const char* path) {
  const char* slash = strrchr(path, '/');
  if (slash == nullptr || slash == path) return;
  // Fails harmlessly when the directory already exists; a real failure
  // surfaces as the open that follows failing.
  Storage.mkdir(std::string(path, static_cast<size_t>(slash - path)).c_str());
}

// The buffer is flushed and freed before this returns, so it never flushes
// into a closed file.
bool serializeInto(HalFile& file, const JsonDocument& doc) {
  serialization::BufferedFileWriter buffered(file, WRITE_BUFFER_BYTES);
  JsonFileWriter sink(buffered);
  serializeJson(doc, sink);
  return buffered.flush();
}

bool writeDocStreamed(const char* path, const JsonDocument& doc) {
  HalFile file;
  if (!Storage.openFileForWrite("PERSIST", path, file)) {
    LOG_ERR("PERSIST", "Failed to open %s for write", path);
    return false;
  }
  const bool written = serializeInto(file, doc);
  // Closed here, not at scope exit: the caller renames this path next, and
  // close() reports the final sync.
  const bool closed = file.close();
  if (!written || !closed) {
    LOG_ERR("PERSIST", "Failed to write %s (%s)", path, written ? "close failed" : "short write");
    return false;
  }
  return true;
}

}  // namespace

bool PersistableStoreBase::writeDocToFile(const char* path, const JsonDocument& doc) {
  ensureParentDirectory(path);
  String json;
  serializeJson(doc, json);
  if (!Storage.writeFile(path, json)) {
    LOG_ERR("PERSIST", "Failed to write %s", path);
    return false;
  }
  return true;
}

bool PersistableStoreBase::writeDocToFileAtomic(const char* path, const JsonDocument& doc) {
  ensureParentDirectory(path);
  const std::string finalPath = path;
  const std::string tmpPath = finalPath + ".tmp";

  // A failed write leaves the destination alone and the partial .tmp on the
  // card; the adopting read handles that .tmp and the next save truncates it.
  if (!writeDocStreamed(tmpPath.c_str(), doc)) return false;

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

DocReadStatus PersistableStoreBase::readDocFromFileStreamed(const char* path, JsonDocument& doc) {
  if (!Storage.exists(path)) return classifyDocRead(false, false, false);

  HalFile file;
  if (!Storage.openFileForRead("PERSIST", path, file)) {
    LOG_ERR("PERSIST", "Failed to open %s", path);
    return classifyDocRead(true, true, false);
  }
  if (file.size() == 0) {
    LOG_ERR("PERSIST", "%s is empty", path);
    return classifyDocRead(true, true, false);
  }

  HalFileReader reader(file);
  const auto error = deserializeJson(doc, reader);
  if (error) {
    LOG_ERR("PERSIST", "JSON parse error in %s: %s", path, error.c_str());
    return classifyDocRead(true, false, true);
  }
  return classifyDocRead(true, false, false);
}

namespace {

// Reads `path` with `read`; when it is Missing, reads `<path>.tmp` into the
// same doc and promotes it if it parsed. `primary` receives the primary's
// status. Both readers return before touching doc for a missing path, so the
// .tmp never lands on top of a half-read primary.
TempAdoptionAction readAdopting(const char* path, const PersistableStoreBase::DocReader read, JsonDocument& doc,
                                DocReadStatus& primary) {
  primary = read(path, doc);

  bool tempExists = false;
  bool tempParsed = false;
  std::string tmpPath;
  if (primary == DocReadStatus::Missing) {
    tmpPath = std::string(path) + ".tmp";
    tempExists = Storage.exists(tmpPath.c_str());
    if (tempExists) tempParsed = read(tmpPath.c_str(), doc) == DocReadStatus::Ok;
  }

  const TempAdoptionAction action = tempAdoptionAction(primary, tempExists, tempParsed);
  if (action == TempAdoptionAction::PromoteTempAndUseIt) {
    // Promote first: the rename is what rescues the only surviving copy. The
    // primary path is Missing, so nothing here can be overwritten.
    if (Storage.rename(tmpPath.c_str(), path)) {
      LOG_INF("PERSIST", "Recovered %s from an interrupted write", path);
    } else {
      // Still usable: the document is in hand and the .tmp survives for the
      // next boot to retry. Only the rename failed, so do not claim a recovery
      // -- that log line is what the on-device test reads as "the file is back".
      LOG_ERR("PERSIST", "Failed to promote %s into place", tmpPath.c_str());
    }
  }
  return action;
}

}  // namespace

DocReadStatus PersistableStoreBase::readDocFromFileAdopting(const char* path, JsonDocument& doc) {
  DocReadStatus primary = DocReadStatus::Missing;
  const TempAdoptionAction action = readAdopting(path, readDocFromFileChecked, doc, primary);
  if (action == TempAdoptionAction::KeepTempReportEmpty) {
    // deserializeJson leaves the partially parsed document behind, and callers
    // that ignore the status read it immediately. Deliberately NOT extended to
    // the ReportFailed arm: clearing there would make a read-modify-write
    // caller overwrite a corrupt-but-present file instead of merging onto what
    // did parse.
    doc.clear();
  }
  return adoptedReadStatus(primary, action);
}

AdoptedLoad PersistableStoreBase::loadAdopting(const char* path, const DocReader read, const DocAcceptor accept,
                                               void* target) {
  JsonDocument doc;
  DocReadStatus primary = DocReadStatus::Missing;
  const TempAdoptionAction action = readAdopting(path, read, doc, primary);

  bool accepted = false;
  if (action == TempAdoptionAction::UseLoaded) {
    accepted = accept(target, doc.as<JsonVariantConst>());
    if (!accepted) LOG_ERR("PERSIST", "Rejected %s (future format version, over budget, or corrupt)", path);
  } else if (action == TempAdoptionAction::PromoteTempAndUseIt) {
    accepted = accept(target, doc.as<JsonVariantConst>());
    if (!accepted) LOG_ERR("PERSIST", "Recovered %s but rejected its contents", path);
  }
  return adoptedLoad(action, accepted);
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
