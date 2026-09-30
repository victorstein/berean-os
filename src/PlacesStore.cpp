#include "PlacesStore.h"

#include <Logging.h>

#include <utility>

void PlacesStore::toJson(JsonDocument& doc) const { PlacesDoc::toJson(places, doc); }

bool PlacesStore::fromJson(const JsonVariantConst doc) {
  bool needsResave = false;
  if (!PlacesDoc::fromJson(doc, places, needsResave)) {
    LOG_ERR("PLC", "Refusing %s: unknown format v%d (this build knows v1..v%d)", getFilePath(), doc["v"] | 0,
            PlacesDoc::FORMAT_VERSION);
    return false;
  }
  // loadFromFile performs the save after releasing storeMutex; saving from here would deadlock.
  if (needsResave) requestResave();
  LOG_DBG("PLC", "Places loaded (%d entries)", static_cast<int>(places.size()));
  return true;
}

bool PlacesStore::load() {
  loadAttempted = true;
  return loadFromFile();
}

void PlacesStore::ensureLoaded() {
  if (!loadAttempted) load();
}

void PlacesStore::record(Place place) {
  ensureLoaded();
  if (loadRefused) {
    if (!refusalLogged) {
      LOG_ERR("PLC", "Not recording places: %s is a format this build refused", getFilePath());
      refusalLogged = true;
    }
    return;
  }
  if (!PlacesDoc::record(places, std::move(place))) return;
  if (!saveToFileAtomic()) {
    LOG_ERR("PLC", "Failed to persist place %s", places.front().reference.c_str());
    return;
  }
  LOG_DBG("PLC", "saved: %s", PlacesDoc::describe(places).c_str());
}

const std::vector<Place>& PlacesStore::getPlaces() {
  ensureLoaded();
  return places;
}

static_assert(PlacesStore::saveBudget() == PlacesDoc::SAVE_BUDGET,
              "PlacesStore's budget is PlacesDoc::worstCaseBytes() -- derived from the field caps, "
              "not a round number to be tidied");
static_assert(PlacesDoc::SAVE_BUDGET <= 4096, "issue #201 caps places.json at 4 KB");
static_assert(PlacesDoc::SAVE_BUDGET < persist::DEFAULT_SAVE_BUDGET,
              "a store whose fields are bounded must claim less than the shared default");
