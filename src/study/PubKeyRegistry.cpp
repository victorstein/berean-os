#include "PubKeyRegistry.h"

#include <ArduinoJson.h>
#include <HalStorage.h>
#include <Logging.h>
#include <PersistableStore.h>
#include <SaveBudget.h>
#include <SdPaths.h>

namespace {

constexpr const char* MODULE = "PUBKEYS";
constexpr int FORMAT_VERSION = 1;

}  // namespace

namespace PubKeyRegistry {

bool record(const std::string& bookPath, const study::RegisteredPub& pub) {
  if (bookPath.empty() || pub.symbol.empty()) return false;

  JsonDocument doc;
  const DocReadStatus status = PersistableStoreBase::readDocFromFileAdopting(PATH, doc);
  if (!mayOverwriteAfterRead(status)) {
    LOG_ERR(MODULE, "Registry unreadable; refusing to overwrite it");
    return false;
  }
  const int version = doc["v"] | 0;
  if (version > FORMAT_VERSION) {
    LOG_ERR(MODULE, "Refusing to rewrite a newer registry format");
    return false;
  }
  doc["v"] = FORMAT_VERSION;

  const auto entry = doc["p"][bookPath].to<JsonObject>();
  entry["s"] = pub.symbol;
  entry["i"] = pub.issue;
  entry["l"] = pub.language;

  if (measureJson(doc) > persist::DEFAULT_SAVE_BUDGET) {
    LOG_ERR(MODULE, "Registry exceeds the save budget; not written");
    return false;
  }

  Storage.mkdir(sdpaths::BEREAN_DIR);
  return PersistableStoreBase::writeDocToFileAtomic(PATH, doc);
}

// Reverse lookup. The registry is keyed by path because that is what the
// downloader knows, but the launcher needs the opposite question -- "is a
// Watchtower on this card?" -- and it cannot ask the recents list, which only
// holds books that have been OPENED. A publication downloaded and not yet read
// is exactly the case the meeting tile has to cover.
std::optional<std::string> findBySymbol(std::initializer_list<std::string_view> symbols, const std::string_view issue) {
  JsonDocument doc;
  if (PersistableStoreBase::readDocFromFileAdopting(PATH, doc) != DocReadStatus::Ok) return std::nullopt;
  if ((doc["v"] | 0) > FORMAT_VERSION) return std::nullopt;

  const JsonObjectConst entries = doc["p"];
  if (entries.isNull()) return std::nullopt;

  for (const JsonPairConst entry : entries) {
    const char* symbol = entry.value()["s"] | "";
    // An empty issue means "any issue of this publication"; the meeting tile
    // asks that way, the meetings screen asks for one specific week.
    if (!issue.empty() && issue != (entry.value()["i"] | "")) continue;
    for (const std::string_view wanted : symbols) {
      if (wanted != symbol) continue;
      std::string path = entry.key().c_str();
      // An entry outlives the file it describes: the registry is never pruned
      // when a publication is deleted from the card.
      if (Storage.exists(path.c_str())) return path;
    }
  }
  return std::nullopt;
}

std::optional<study::RegisteredPub> lookup(const std::string& bookPath) {
  if (bookPath.empty()) return std::nullopt;

  JsonDocument doc;
  if (PersistableStoreBase::readDocFromFileAdopting(PATH, doc) != DocReadStatus::Ok) return std::nullopt;
  if ((doc["v"] | 0) > FORMAT_VERSION) return std::nullopt;

  const JsonVariantConst entry = doc["p"][bookPath];
  if (!entry.is<JsonObjectConst>()) return std::nullopt;

  study::RegisteredPub pub;
  pub.symbol = entry["s"] | "";
  pub.issue = entry["i"] | "";
  pub.language = entry["l"] | "";
  if (pub.symbol.empty()) return std::nullopt;
  return pub;
}

}  // namespace PubKeyRegistry
