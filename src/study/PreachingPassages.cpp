#include "PreachingPassages.h"

#include <Arduino.h>
#include <BufferedFile.h>
#include <FormatVersion.h>
#include <HalStorage.h>
#include <Logging.h>
#include <SdPaths.h>

#include <algorithm>
#include <cstdio>
#include <optional>

#include "StudyStore/PubKey.h"

namespace PreachingPassages {
namespace {

constexpr const char* MODULE = "PREACH";
constexpr size_t READ_BUFFER_BYTES = 512;
// The Home verse scan's cap (StudyPassageScan.cpp): the store's budget plus slack.
constexpr size_t MAX_FILE_BYTES = study::PassageDoc::SAVE_BYTE_BUDGET + 4096;
constexpr size_t PATH_BYTES = sizeof(sdpaths::PASSAGES_DIR) + 16;

// The parser pulls one character per read() and every HalFile::read takes
// storageMutex, hence the buffer. PersistableStore.cpp's twin is file-local.
class JsonFileReader {
 public:
  explicit JsonFileReader(serialization::BufferedFileReader& in) : in_(in) {}

  int read() {
    uint8_t c = 0;
    return in_.read(&c, 1) == 1 ? c : -1;
  }

 private:
  serialization::BufferedFileReader& in_;
};

struct RowTags {
  study::TagId ids[study::PassageDoc::MAX_TAGS_PER_PASSAGE];
  size_t count = 0;

  bool carries(const study::TagId id) const { return std::find(ids, ids + count, id) != ids + count; }
};

JsonDocument newDocument(ArduinoJson::Allocator* allocator) {
  return allocator ? JsonDocument(allocator) : JsonDocument();
}

void biblePassagesPath(char* out, const size_t outSize) {
  snprintf(out, outSize, "%s/%s.json", sdpaths::PASSAGES_DIR, study::BIBLE_PUB_KEY);
}

// Both passes parse through here, so the filtered deserializeJson template is
// instantiated once.
Status readFiltered(JsonDocument& doc, const JsonDocument& filter) {
  if (filter.overflowed()) {
    LOG_ERR(MODULE, "OOM: passage filter");
    return Status::OutOfMemory;
  }
  char path[PATH_BYTES];
  biblePassagesPath(path, sizeof(path));
  // Checked first: a failed open logs, and a Bible with nothing tagged is ordinary.
  if (!Storage.exists(path)) return Status::Missing;

  HalFile file;
  if (!Storage.openFileForRead(MODULE, path, file)) return Status::Unreadable;
  const size_t bytes = file.size();
  if (bytes == 0 || bytes > MAX_FILE_BYTES) {
    LOG_ERR(MODULE, "%s: %u bytes, outside 1..%u", path, static_cast<unsigned>(bytes),
            static_cast<unsigned>(MAX_FILE_BYTES));
    return Status::Unreadable;
  }

  serialization::BufferedFileReader buffered(file, READ_BUFFER_BYTES);
  JsonFileReader reader(buffered);
  const DeserializationError error = deserializeJson(doc, reader, DeserializationOption::Filter(filter));
  if (error == DeserializationError::NoMemory || doc.overflowed()) {
    LOG_ERR(MODULE, "OOM: parsing %s", path);
    return Status::OutOfMemory;
  }
  if (error) {
    LOG_ERR(MODULE, "JSON parse error in %s: %s", path, error.c_str());
    return Status::Unreadable;
  }
  const int version = doc["v"] | 0;
  if (!persist::isKnownFormatVersion(version, study::PassageDoc::FORMAT_VERSION) || !doc["p"].is<JsonArrayConst>()) {
    LOG_ERR(MODULE, "%s: unknown passage format v%d", path, version);
    return Status::Unreadable;
  }
  return Status::Ok;
}

// PassageDoc::fromJson skips a row whose start does not parse.
std::optional<study::Unit> rowStart(const JsonVariantConst row) { return study::unitFromCompact(row["u"] | ""); }

// As PassageDoc::fromJson and its normaliseTags read "t": no 0, nothing past
// UINT16_MAX, no repeats, at most MAX_TAGS_PER_PASSAGE.
RowTags rowTags(const JsonVariantConst row) {
  RowTags tags;
  for (const JsonVariantConst stored : row["t"].as<JsonArrayConst>()) {
    if (tags.count == study::PassageDoc::MAX_TAGS_PER_PASSAGE) break;
    const uint32_t raw = stored | 0u;
    if (raw == 0 || raw > UINT16_MAX) continue;
    const study::TagId id = study::toTagId(static_cast<uint16_t>(raw));
    if (!tags.carries(id)) tags.ids[tags.count++] = id;
  }
  return tags;
}

}  // namespace

Status countByTag(const std::vector<study::TagId>& tags, ArduinoJson::Allocator* json, std::vector<TagCount>& out) {
  out.clear();
  [[maybe_unused]] const unsigned long started = millis();

  JsonDocument filter;
  filter["v"] = true;
  filter["p"][0]["u"] = true;
  filter["p"][0]["t"] = true;

  JsonDocument doc = newDocument(json);
  const Status status = readFiltered(doc, filter);
  if (status != Status::Ok) return status;

  out.reserve(tags.size());
  for (const study::TagId id : tags) out.push_back({id, 0});
  [[maybe_unused]] uint32_t rows = 0;
  for (const JsonVariantConst row : doc["p"].as<JsonArrayConst>()) {
    if (!rowStart(row)) continue;
    ++rows;
    const RowTags carried = rowTags(row);
    for (TagCount& count : out) {
      if (carried.carries(count.tag) && count.passages < UINT16_MAX) ++count.passages;
    }
  }
  LOG_INF(MODULE, "Counts: %u rows, %u tags, %lu ms", static_cast<unsigned>(rows), static_cast<unsigned>(tags.size()),
          millis() - started);
  return Status::Ok;
}

Status passagesWithTag(study::TagId, const study::PassageDoc::Allocators&, std::vector<Passage>& out) {
  out.clear();
  return Status::Unreadable;
}

}  // namespace PreachingPassages
