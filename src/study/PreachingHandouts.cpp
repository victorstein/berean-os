#include "PreachingHandouts.h"

#include <Arduino.h>
#include <BufferedFile.h>
#include <HalStorage.h>
#include <Logging.h>
#include <Utf8.h>

#include <cstring>
#include <utility>

namespace PreachingHandouts {
namespace {

constexpr const char* MODULE = "PREACH";
constexpr size_t READ_BUFFER_BYTES = 512;

// The parser pulls one character per read() and every HalFile::read takes storageMutex, hence the
// buffer. PreachingPassages.cpp's twin is file-local too.
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

JsonDocument newDocument(ArduinoJson::Allocator* allocator) {
  return allocator ? JsonDocument(allocator) : JsonDocument();
}

bool hrefOpens(const char* href) {
  const size_t bytes = strlen(href);
  return bytes > 0 && bytes <= MAX_HREF_BYTES;
}

// Both passes parse through here, so the filtered deserializeJson template is instantiated once.
Status readFiltered(JsonDocument& doc, const JsonDocument& filter) {
  if (filter.overflowed()) {
    LOG_ERR(MODULE, "OOM: handout filter");
    return Status::OutOfMemory;
  }
  // Checked first: a failed open logs, and a card with no handouts file is ordinary.
  if (!Storage.exists(FILE_PATH)) return Status::Missing;

  HalFile file;
  if (!Storage.openFileForRead(MODULE, FILE_PATH, file)) return Status::Unreadable;
  const size_t bytes = file.size();
  if (bytes == 0 || bytes > MAX_FILE_BYTES) {
    LOG_ERR(MODULE, "%s: %u bytes, outside 1..%u", FILE_PATH, static_cast<unsigned>(bytes),
            static_cast<unsigned>(MAX_FILE_BYTES));
    return Status::Unreadable;
  }

  serialization::BufferedFileReader buffered(file, READ_BUFFER_BYTES);
  JsonFileReader reader(buffered);
  const DeserializationError error = deserializeJson(doc, reader, DeserializationOption::Filter(filter));
  if (error == DeserializationError::NoMemory || doc.overflowed()) {
    LOG_ERR(MODULE, "OOM: parsing %s", FILE_PATH);
    return Status::OutOfMemory;
  }
  if (error) {
    LOG_ERR(MODULE, "JSON parse error in %s: %s", FILE_PATH, error.c_str());
    return Status::Unreadable;
  }
  const int version = doc["v"] | 0;
  if (version > FORMAT_VERSION) {
    LOG_ERR(MODULE, "%s: format v%d is newer than v%d", FILE_PATH, version, FORMAT_VERSION);
    return Status::TooNew;
  }
  if (version < 1 || !doc["cards"].is<JsonArrayConst>()) {
    LOG_ERR(MODULE, "%s: no v%d handouts", FILE_PATH, FORMAT_VERSION);
    return Status::Unreadable;
  }
  return Status::Ok;
}

}  // namespace

Status summaries(ArduinoJson::Allocator* json, std::vector<Summary>& out) {
  out.clear();
  [[maybe_unused]] const unsigned long started = millis();

  JsonDocument filter;
  filter["v"] = true;
  filter["cards"][0]["title"] = true;
  filter["cards"][0]["sections"][0]["verses"][0]["href"] = true;

  JsonDocument doc = newDocument(json);
  const Status status = readFiltered(doc, filter);
  if (status != Status::Ok) return status;

  const JsonArrayConst handouts = doc["cards"].as<JsonArrayConst>();
  out.reserve(handouts.size() < MAX_HANDOUTS ? handouts.size() : MAX_HANDOUTS);
  for (const JsonVariantConst handout : handouts) {
    if (out.size() == MAX_HANDOUTS) break;
    Summary summary{utf8SafeSummary(handout["title"] | "", MAX_TITLE_BYTES), 0};
    for (const JsonVariantConst section : handout["sections"].as<JsonArrayConst>()) {
      for (const JsonVariantConst verse : section["verses"].as<JsonArrayConst>()) {
        if (hrefOpens(verse["href"] | "") && summary.verses < UINT16_MAX) ++summary.verses;
      }
    }
    out.push_back(std::move(summary));
  }
  LOG_INF(MODULE, "Handouts: %u, %lu ms", static_cast<unsigned>(out.size()), millis() - started);
  return Status::Ok;
}

Status load(const size_t index, const study::PassageDoc::Allocators& allocators, Handout& out) {
  out = Handout{};
  [[maybe_unused]] const unsigned long started = millis();

  JsonDocument filter;
  filter["v"] = true;
  filter["cards"][0]["title"] = true;
  filter["cards"][0]["sections"][0]["heading"] = true;
  for (const char* key : {"ref", "href", "text"}) filter["cards"][0]["sections"][0]["verses"][0][key] = true;

  JsonDocument doc = newDocument(allocators.json);
  const Status status = readFiltered(doc, filter);
  if (status != Status::Ok) return status;

  const JsonArrayConst handouts = doc["cards"].as<JsonArrayConst>();
  if (index >= handouts.size() || index >= MAX_HANDOUTS) {
    LOG_ERR(MODULE, "No handout %u of %u", static_cast<unsigned>(index), static_cast<unsigned>(handouts.size()));
    return Status::Unreadable;
  }
  const JsonVariantConst handout = handouts[index];
  const JsonArrayConst sections = handout["sections"].as<JsonArrayConst>();
  size_t verseCount = 0;
  for (const JsonVariantConst section : sections) verseCount += section["verses"].size();

  out.title = utf8SafeSummary(handout["title"] | "", MAX_TITLE_BYTES);
  out.headings.reserve(sections.size());
  out.verses.reserve(verseCount);
  for (const JsonVariantConst section : sections) {
    if (out.headings.size() == UINT16_MAX) break;
    const auto sectionIndex = static_cast<uint16_t>(out.headings.size());
    out.headings.push_back(utf8SafeSummary(section["heading"] | "", MAX_TITLE_BYTES));
    for (const JsonVariantConst verse : section["verses"].as<JsonArrayConst>()) {
      const char* href = verse["href"] | "";
      if (!hrefOpens(href)) {
        LOG_ERR(MODULE, "Skipping \"%s\": href of %u bytes", verse["ref"] | "", static_cast<unsigned>(strlen(href)));
        continue;
      }
      Verse entry{sectionIndex, utf8SafeSummary(verse["ref"] | "", study::PassageDoc::MAX_REFERENCE_BYTES), href,
                  study::PassageText(allocators.text)};
      if (!entry.text.assign(verse["text"] | "")) {
        LOG_ERR(MODULE, "OOM: text of handout %u", static_cast<unsigned>(index));
        out = Handout{};
        return Status::OutOfMemory;
      }
      out.verses.push_back(std::move(entry));
    }
  }
  LOG_INF(MODULE, "Handout %u: %u verses, %lu ms", static_cast<unsigned>(index),
          static_cast<unsigned>(out.verses.size()), millis() - started);
  return Status::Ok;
}

}  // namespace PreachingHandouts
