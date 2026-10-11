#include "activities/launcher/HomeVerseFile.h"

#include <Logging.h>
#include <PersistableStore.h>
#include <SaveBudget.h>
#include <SdPaths.h>

#include "activities/launcher/HomeVerseDoc.h"

namespace HomeVerseFile {
namespace {

constexpr const char* MODULE = "HOMEV";

// JsonDocument has no null allocator: the host suite passes nullptr for ArduinoJson's default.
JsonDocument makeDocument(ArduinoJson::Allocator* const allocator) {
  return allocator != nullptr ? JsonDocument(allocator) : JsonDocument();
}

}  // namespace

Load load(const char* const buildId, const uint8_t rungCount, ArduinoJson::Allocator* const allocator,
          home_verse::Entry& out) {
  out.valid = false;
  if (!HomeVerseDoc::isUsableBuildId(buildId)) return Load::NoBuildId;

  JsonDocument doc = makeDocument(allocator);
  const DocReadStatus status = PersistableStoreBase::readDocFromFileAdopting(sdpaths::HOME_VERSE_FILE, doc);
  if (status == DocReadStatus::Missing) return Load::None;
  if (status != DocReadStatus::Ok) return Load::Unreadable;

  switch (HomeVerseDoc::fromJson(doc.as<JsonVariantConst>(), buildId, rungCount, out)) {
    case HomeVerseDoc::Restore::Restored:
      return Load::Restored;
    case HomeVerseDoc::Restore::UnknownFormat:
      LOG_ERR(MODULE, "Refusing a Home verse file of an unknown format");
      return Load::UnknownFormat;
    case HomeVerseDoc::Restore::OtherBuild:
      LOG_DBG(MODULE, "Home verse file is from build %s; running %s", doc["b"] | "?", buildId);
      return Load::OtherBuild;
    case HomeVerseDoc::Restore::Malformed:
      LOG_ERR(MODULE, "Home verse file is malformed");
      return Load::Malformed;
  }
  return Load::Malformed;
}

size_t save(const home_verse::Entry& entry, const char* const buildId, ArduinoJson::Allocator* const allocator) {
  if (!entry.valid || !HomeVerseDoc::isUsableBuildId(buildId)) return 0;

  JsonDocument doc = makeDocument(allocator);
  HomeVerseDoc::toJson(entry, buildId, doc);
  if (doc.overflowed()) {
    LOG_ERR(MODULE, "OOM: Home verse document");
    return 0;
  }
  const size_t bytes = measureJson(doc);
  if (!persist::fitsBudget(bytes, HomeVerseDoc::SAVE_BUDGET)) {
    LOG_ERR(MODULE, "Refusing to save the Home verse: %u bytes exceeds budget %u", static_cast<unsigned>(bytes),
            static_cast<unsigned>(HomeVerseDoc::SAVE_BUDGET));
    return 0;
  }
  return PersistableStoreBase::writeDocToFileAtomic(sdpaths::HOME_VERSE_FILE, doc) ? bytes : 0;
}

const char* describe(const Load outcome) {
  switch (outcome) {
    case Load::None:
      return "none";
    case Load::Restored:
      return "restored";
    case Load::Unreadable:
      return "unreadable";
    case Load::UnknownFormat:
      return "unknown format";
    case Load::OtherBuild:
      return "other build";
    case Load::Malformed:
      return "malformed";
    case Load::NoBuildId:
      return "no build id";
  }
  return "?";
}

}  // namespace HomeVerseFile
