#pragma once
#include <ArduinoJson.h>
#include <PersistableStore.h>
#include <SdPaths.h>

#include <vector>

#include "Place.h"
#include "util/PlacesDoc.h"

// /.berean/places.json: the last Bible places the reader left, newest first. Owned by the loop
// task -- the reader records from pageTurn, skipPages, navigateTo and onExit, all on it.
class PlacesStore : public PersistableStore<PlacesStore> {
 private:
  std::vector<Place> places;
  bool loadAttempted = false;
  bool refusalLogged = false;

  PlacesStore() = default;
  ~PlacesStore() = default;

  friend class PersistableStore<PlacesStore>;

  // A store that records before any load would overwrite a file it has never read, a newer
  // format's included, so the first use loads if main.cpp has not.
  void ensureLoaded();

 public:
  // Derived from PlacesDoc's field caps; see PlacesDoc::worstCaseBytes().
  static constexpr size_t SAVE_BUDGET = PlacesDoc::SAVE_BUDGET;

  static const char* getFilePath() { return sdpaths::PLACES_FILE; }
  void toJson(JsonDocument& doc) const;
  bool fromJson(JsonVariantConst doc);

  bool load();

  // Moves the place to the front and saves. A no-op while the file on the card is a format this
  // build refused: that file must survive, and a list the card will never hold would mislead.
  void record(Place place);

  const std::vector<Place>& getPlaces();
};

#define PLACES PlacesStore::getInstance()
