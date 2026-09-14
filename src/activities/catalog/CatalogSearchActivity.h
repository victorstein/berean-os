#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "activities/UiListActivity.h"

// Buscar: search the publication catalog and download what you pick.
//
// The index is held in PSRAM only while this screen is open (see
// CatalogIndexStore), so entering costs one inflate and leaving gives the 217 KB
// back. Searching is a linear scan of that buffer, which is single-digit
// milliseconds -- the e-ink refresh, not the scan, is what the user feels.
//
// Text entry on this device is the modal keyboard activity, so the query arrives
// complete rather than a character at a time. The design's ~250 ms keystroke
// debounce has nothing to debounce here; the scan runs once per submitted query.
class CatalogSearchActivity final : public UiListActivity {
 public:
  explicit CatalogSearchActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;
  void onExit() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override { return state == State::FETCHING_INDEX || state == State::DOWNLOADING; }

 private:
  enum class State : uint8_t {
    BROWSING,
    WIFI_SELECTION,
    FETCHING_INDEX,
    DOWNLOADING,
    COMPLETE,
    ERROR,
  };

  // What to run once WiFi is up. Both entry points need the radio, and the
  // selection activity returns asynchronously.
  enum class Pending : uint8_t { None, CatalogAction, Download };

  // Bounds the row vectors: nobody scrolls past sixty hits, and an unbounded
  // list of a 3,768-row catalog would allocate for every one of them.
  static constexpr size_t MAX_RESULTS = 64;

  struct Hit {
    std::string symbol;
    std::string issue;
    std::string title;
    std::string year;
  };

  int listCount() const override;
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  bool handleCustomInput() override;
  void onBackButton() override;

  void rebuildRowItems();
  void runSearch();

  // Row layout: the query row, the catalog row, then an optional
  // download-this-symbol row, then the hits.
  int queryRow() const { return 0; }
  int catalogRow() const { return 1; }
  bool hasSymbolRow() const;
  int symbolRow() const { return hasSymbolRow() ? 2 : -1; }
  int firstHitRow() const { return hasSymbolRow() ? 3 : 2; }

  void openKeyboard();
  void onQueryEntered(const std::string& text);

  // Ensures the radio is up, deferring `pending` through WifiSelectionActivity
  // when it is not. Returns true when the caller may proceed immediately.
  bool ensureWifi(Pending pending);
  void onWifiSelectionComplete(bool connected);
  void runPending();

  void runCatalogAction();
  void downloadHit(size_t hitIndex);
  void downloadBySymbol(const std::string& symbol, const std::string& issue);
  void drawMessageLine(const char* text, int y, EpdFontFamily::Style style) const;
  void drawMessageBlock(const char* text, int centreY) const;

  void fail(const char* message);
  // "Catalogo: 12 sep 2026", or the reason there is no date to show.
  void refreshCatalogSubtitle();

  static void onDownloadResolved(void* ctx, const char* filename);
  static void onDownloadProgress(void* ctx, size_t downloaded, size_t total);
  static void onIndexProgress(void* ctx, size_t downloaded, size_t total);
  void throttledProgressRepaint(size_t downloaded, size_t total);

  State state = State::BROWSING;
  Pending pending = Pending::None;
  Pending pendingAfterWifi = Pending::None;
  size_t pendingHit = 0;

  std::string query;
  std::string downloadFolder;
  std::string catalogSubtitle;
  std::string statusMessage;
  std::string errorMessage;
  std::string currentFilename;

  std::vector<Hit> hits;
  bool resultsTruncated = false;
  // The index could not be read; the symbol row is then the only way through,
  // which is exactly what it is for.
  bool indexUnavailable = false;

  std::vector<std::string> rowLabels;
  std::vector<std::string> rowSubtitles;
  std::vector<freeink::ui::ListItem> rowItems;
  bool rowsDirty = true;

  size_t downloadProgress = 0;
  size_t downloadTotal = 0;
  int lastRenderedPercent = -1;
  unsigned long lastProgressUpdateMs = 0;

  bool cancelDownload = false;
  bool goHomeAfterCancel = false;
};
