#include "network/MeetingWeekCache.h"

#include <ArduinoJson.h>
#include <HalStorage.h>
#include <Logging.h>
#include <PersistableStore.h>
#include <SaveBudget.h>

namespace {

constexpr const char* MODULE = "MEETWK";
constexpr const char* BEREAN_DIR = "/.berean";
constexpr int FORMAT_VERSION = 1;

}  // namespace

namespace MeetingWeekCache {

bool load(MeetingWeekTable& out) {
  out.clear();

  JsonDocument doc;
  if (PersistableStoreBase::readDocFromFileAdopting(PATH, doc) != DocReadStatus::Ok) return false;
  if ((doc["v"] | 0) > FORMAT_VERSION) {
    LOG_ERR(MODULE, "Refusing to read a newer week cache format");
    return false;
  }

  const JsonObjectConst weeks = doc["w"];
  if (weeks.isNull()) return false;

  for (const JsonPairConst week : weeks) {
    out.set(week.key().c_str(), week.value()["w"] | "", week.value()["m"] | "");
  }
  return true;
}

bool save(MeetingWeekTable& table) {
  table.prune();

  JsonDocument doc;
  doc["v"] = FORMAT_VERSION;
  const auto weeks = doc["w"].to<JsonObject>();
  for (const MeetingWeekEntry& entry : table.entries()) {
    const auto week = weeks[entry.key].to<JsonObject>();
    week["w"] = entry.watchtower;
    week["m"] = entry.workbook;
  }

  if (measureJson(doc) > persist::DEFAULT_SAVE_BUDGET) {
    LOG_ERR(MODULE, "Week cache exceeds the save budget; not written");
    return false;
  }

  Storage.mkdir(BEREAN_DIR);
  return PersistableStoreBase::writeDocToFileAtomic(PATH, doc);
}

bool record(const IsoWeek& week, const std::string& watchtower, const std::string& workbook) {
  const std::string key = meetingWeekKey(week);
  if (key.empty()) return false;

  // A cold or unreadable cache is not a reason to refuse the write: load leaves
  // the table empty and this becomes the first entry.
  MeetingWeekTable table;
  load(table);
  table.set(key, watchtower, workbook);
  return save(table);
}

}  // namespace MeetingWeekCache
