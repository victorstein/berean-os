#include "PubKeyRegistry.h"

#include <ArduinoJson.h>
#include <HalStorage.h>
#include <Logging.h>
#include <PersistableStore.h>
#include <SaveBudget.h>

namespace {

constexpr const char* MODULE = "PUBKEYS";
constexpr const char* BEREAN_DIR = "/.berean";
constexpr int FORMAT_VERSION = 1;

}  // namespace

namespace PubKeyRegistry {

bool record(const std::string& bookPath, const study::RegisteredPub& pub) {
  if (bookPath.empty() || pub.symbol.empty()) return false;

  JsonDocument doc;
  PersistableStoreBase::readDocFromFileChecked(PATH, doc);
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

  Storage.mkdir(BEREAN_DIR);
  return PersistableStoreBase::writeDocToFileAtomic(PATH, doc);
}

std::optional<study::RegisteredPub> lookup(const std::string& bookPath) {
  if (bookPath.empty()) return std::nullopt;

  JsonDocument doc;
  if (PersistableStoreBase::readDocFromFileChecked(PATH, doc) != DocReadStatus::Ok) return std::nullopt;
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
