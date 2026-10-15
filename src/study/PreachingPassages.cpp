#include "PreachingPassages.h"

#include <Arduino.h>
#include <BufferedFile.h>
#include <FormatVersion.h>
#include <HalStorage.h>
#include <Logging.h>
#include <SdPaths.h>
#include <Utf8.h>

#include <algorithm>
#include <cstdio>
#include <optional>
#include <utility>

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

// As StudyPassageScan reads "s": absent, or past UINT16_MAX, is 0.
uint16_t spineHint(const JsonVariantConst row) {
  const uint32_t raw = row["s"] | 0u;
  return raw <= UINT16_MAX ? static_cast<uint16_t>(raw) : 0;
}

// Separate from passagesWithTag so the parse document is released before the sort.
Status collectPassages(const study::TagId tag, const study::PassageDoc::Allocators& allocators,
                       std::vector<Passage>& out, uint32_t& rowsSeen) {
  JsonDocument filter;
  filter["v"] = true;
  for (const char* key : {"u", "s", "x", "w", "r", "t"}) filter["p"][0][key] = true;

  JsonDocument doc = newDocument(allocators.json);
  const Status status = readFiltered(doc, filter);
  if (status != Status::Ok) return status;

  const JsonArrayConst rows = doc["p"].as<JsonArrayConst>();
  size_t matching = 0;
  for (const JsonVariantConst row : rows) {
    ++rowsSeen;
    if (rowStart(row) && rowTags(row).carries(tag)) ++matching;
  }
  out.reserve(matching);

  for (const JsonVariantConst row : rows) {
    const std::optional<study::Unit> start = rowStart(row);
    if (!start || !rowTags(row).carries(tag)) continue;

    Passage passage{utf8SafeSummary(row["r"] | "", study::PassageDoc::MAX_REFERENCE_BYTES),
                    study::PassageText(allocators.text), *start, spineHint(row)};
    const char* whole = row["w"] | "";
    const bool assigned =
        whole[0] != '\0' ? passage.text.assign(whole)
                         : passage.text.assign(utf8SafeSummary(row["x"] | "", study::PassageDoc::MAX_SNIPPET_BYTES));
    if (!assigned) {
      LOG_ERR(MODULE, "OOM: passage text for tag %u", static_cast<unsigned>(study::toRaw(tag)));
      out.clear();
      return Status::OutOfMemory;
    }
    out.push_back(std::move(passage));
  }
  return Status::Ok;
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

Status passagesWithTag(const study::TagId tag, const study::PassageDoc::Allocators& allocators,
                       std::vector<Passage>& out) {
  out.clear();
  [[maybe_unused]] const unsigned long started = millis();
  [[maybe_unused]] uint32_t rowsSeen = 0;
  const Status status = collectPassages(tag, allocators, out, rowsSeen);
  if (status != Status::Ok) return status;

  // One key, spine then Unit: ordering verses by Unit and the rest by spine is not
  // transitive when the file mixes two editions' spine hints.
  std::stable_sort(out.begin(), out.end(), [](const Passage& a, const Passage& b) {
    if (a.spine != b.spine) return a.spine < b.spine;
    return a.start < b.start;
  });
  LOG_INF(MODULE, "Tag %u: %u of %u rows, %lu ms", static_cast<unsigned>(study::toRaw(tag)),
          static_cast<unsigned>(out.size()), static_cast<unsigned>(rowsSeen), millis() - started);
  return Status::Ok;
}

}  // namespace PreachingPassages
