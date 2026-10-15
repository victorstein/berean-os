#include "PreachingTopicsFile.h"

#include <ArduinoJson.h>
#include <FormatVersion.h>
#include <Logging.h>
#include <PersistableStore.h>
#include <SaveBudget.h>

#include <algorithm>
#include <utility>

#include "TagPaletteFile.h"

namespace {

constexpr const char* MODULE = "PREACH";

void appendUnique(std::vector<study::TagId>& tags, const study::TagId id) {
  if (std::find(tags.begin(), tags.end(), id) == tags.end()) tags.push_back(id);
}

bool parseTopics(const JsonVariantConst json, std::vector<study::TagId>& out) {
  if (!json.is<JsonObjectConst>()) return false;
  if (!persist::isKnownFormatVersion(json["v"] | 0, PreachingTopicsFile::FORMAT_VERSION)) return false;
  const JsonVariantConst stored = json["tags"];
  if (!stored.is<JsonArrayConst>()) return false;
  const JsonArrayConst storedIds = stored.as<JsonArrayConst>();
  if (storedIds.size() > study::TagPalette::MAX_ACTIVE_TAGS) return false;

  out.clear();
  out.reserve(storedIds.size());
  for (const JsonVariantConst id : storedIds) {
    if (!id.is<uint32_t>()) return false;
    const uint32_t raw = id.as<uint32_t>();
    if (raw == 0 || raw > UINT16_MAX) return false;
    appendUnique(out, study::toTagId(static_cast<uint16_t>(raw)));
  }
  return true;
}

}  // namespace

PreachingTopicsFile::LoadResult PreachingTopicsFile::load() {
  palette_ = study::TagPalette{};
  const bool paletteFailed = TagPaletteFile::load(palette_) == TagPaletteFile::LoadResult::Failed;
  if (paletteFailed) LOG_ERR(MODULE, "Tag palette unreadable; preaching topics will not be saved this session");

  std::vector<study::TagId> loaded;
  const LoadResult result = PersistableStoreBase::loadAdopting(
      PATH, &PersistableStoreBase::readDocFromFileChecked,
      [](void* target, JsonVariantConst json) {
        return parseTopics(json, *static_cast<std::vector<study::TagId>*>(target));
      },
      &loaded);
  const bool topicsFailed = result == LoadResult::Failed;
  if (topicsFailed) LOG_ERR(MODULE, "%s unreadable or of an unknown format; not saved this session", PATH);

  tags_ = topicsFailed ? std::vector<study::TagId>{} : std::move(loaded);
  saveRefused_ = paletteFailed || topicsFailed;
  return result;
}

PreachingTopicsFile::SaveResult PreachingTopicsFile::save(const std::vector<study::TagId>& picked) {
  if (saveRefused_) {
    LOG_ERR(MODULE, "Refusing to save %s: it or the tag palette was not read cleanly", PATH);
    return SaveResult::Refused;
  }

  std::vector<study::TagId> kept;
  kept.reserve(picked.size());
  for (const study::TagId id : picked) {
    if (palette_.isActive(id)) appendUnique(kept, id);
  }
  // TagPalette::fromJson does not cap active tags, so more can be picked than load() accepts back.
  if (kept.size() > study::TagPalette::MAX_ACTIVE_TAGS) {
    LOG_ERR(MODULE, "Refusing to save %u topics: over the %u-tag cap", static_cast<unsigned>(kept.size()),
            static_cast<unsigned>(study::TagPalette::MAX_ACTIVE_TAGS));
    return SaveResult::TooLarge;
  }

  JsonDocument json;
  json["v"] = FORMAT_VERSION;
  const JsonArray storedIds = json["tags"].to<JsonArray>();
  for (const study::TagId id : kept) storedIds.add(study::toRaw(id));
  // ArduinoJson drops what it cannot allocate; the shorter document would pass the budget and be written.
  if (json.overflowed()) {
    LOG_ERR(MODULE, "Out of memory serialising %s; not written", PATH);
    return SaveResult::WriteFailed;
  }
  if (!persist::fitsBudget(measureJson(json), saveBudget_)) {
    LOG_ERR(MODULE, "%s exceeds its save budget; not written", PATH);
    return SaveResult::TooLarge;
  }
  if (!PersistableStoreBase::writeDocToFileAtomic(PATH, json)) return SaveResult::WriteFailed;

  tags_ = std::move(kept);
  return SaveResult::Ok;
}
