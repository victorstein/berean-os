#pragma once

#include <BibleSearch/IndexFormat.h>
#include <BibleSearch/IndexReader.h>
#include <Epub.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

#include "BibleBookNameTable.h"
#include "activities/UiListActivity.h"
#include "components/themes/BaseTheme.h"
#include "study/BibleSearchIndexer.h"

// Search the Bible by the words in its verses: prepare the index once, type a
// query, pick a verse, land on it in the reader.
//
// One activity walks every state so the index reader, the build and the result
// rows are owned in one place and released in one onExit.
//
//   Opening --(index ready)--> keyboard -> Searching -> Results <-> keyboard
//   Opening --(not ready)----> Prompt -> Starting -> Building -> Finishing -> Ready -> keyboard
//   Building -> Saving (Cancel, Back, Home) -> reader
//   Starting, Building, Finishing -> Failed on a build failure
//
// Opening, Starting, Saving, Finishing, Ready and Searching each paint their
// frame before their SD work starts: that work stalls the loop task for
// seconds, and the frame is the only sign the tap registered. Input that
// arrives around that work (Cancel or Back while Starting, the home gesture in
// any of them) is remembered and acted on once the work returns. Building steps the indexer one
// document per loop pass and repaints at most once per whole percent and once
// every PROGRESS_MIN_INTERVAL_MS.
//
// BibleSearchStore is main-task only, so every index and verse-text read
// happens here on the loop task; render() only draws what the loop published
// under RenderLock.
class BibleSearchActivity final : public UiListActivity {
 public:
  BibleSearchActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::shared_ptr<Epub> epub);

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool skipLoopDelay() override { return state == State::Building; }
  bool preventAutoSleep() override { return state == State::Building; }
  bool handleHomeGesture() override;

 private:
  enum class State : uint8_t {
    Opening,
    Prompt,
    Starting,
    Building,
    Saving,
    Finishing,
    Ready,
    Failed,
    AwaitingQuery,
    Searching,
    Results,
  };

  static constexpr freeink::ui::ActionId ACTION_PREPARE = ACTION_USER;
  static constexpr freeink::ui::ActionId ACTION_CANCEL = ACTION_USER + 1;
  static constexpr freeink::ui::ActionId ACTION_DISMISS = ACTION_USER + 2;

  static constexpr size_t MAX_QUERY_BYTES = 64;
  static constexpr unsigned long PROGRESS_MIN_INTERVAL_MS = 2000;
  // A rate measured over less than this share of the build swings too far to
  // print as a time left.
  static constexpr uint32_t ETA_MIN_PERCENT = 5;

  // Rows whose reference and verse text are held, from the first visible one.
  // A page shows about eight; a denser theme stays under this.
  static constexpr int ROW_CACHE = 16;
  // ListItems handed to list(), which reads every index from the top row to
  // the last row that fits; rows past ROW_CACHE draw blank rather than read
  // past the window.
  static constexpr int LIST_WINDOW = 24;
  // "Cantar de los Cantares 150:176" with room for a longer script.
  static constexpr int REFERENCE_BYTES = 64;
  // Two subtitle lines of the start of the verse, cut on a UTF-8 boundary.
  static constexpr int SNIPPET_BYTES = 160;

  struct Row {
    int32_t result = -1;
    BibleSearch::VerseEntry entry{};
    bool textAttempted = false;
    char reference[REFERENCE_BYTES] = {};
    char snippet[SNIPPET_BYTES] = {};
  };

  // What render() needs from a running build, published by the loop task.
  struct Progress {
    uint32_t done = 0;
    uint32_t total = 0;
    uint8_t book = 0;
    uint16_t chapter = 0;
    int minutesLeft = -1;  // -1 until a rate exists
  };

  std::shared_ptr<Epub> epub;
  // Loaded by the loop task in Opening, before any state that draws a name.
  // Empty when the load fails; references then read "c:v".
  BibleBookNameTable bookNames;
  State state = State::Opening;
  // The state render() last put on the panel; a busy state's SD work waits for
  // its own frame to land.
  std::atomic<State> shownState{State::Results};
  std::atomic<bool> fullRefreshPending{false};

  BibleSearch::IndexReader::Status promptStatus = BibleSearch::IndexReader::Status::Missing;
  BibleSearchIndexer::Failure failure = BibleSearchIndexer::Failure::None;
  std::unique_ptr<BibleSearchIndexer> indexer;
  Progress progress;
  int publishedPercent = -1;
  unsigned long lastProgressPublishMs = 0;
  unsigned long buildStartMs = 0;
  uint32_t buildStartDocs = 0;
  // Set by the Cancel button, Back or the home gesture; acted on by loop().
  bool cancelRequested = false;
  bool goHomeAfterCancel = false;
  bool prepareRequested = false;
  bool dismissRequested = false;
  // The home gesture during Opening, Finishing, Ready or Searching, honoured
  // once their work returns rather than by tearing the activity down mid-write.
  bool homeRequested = false;
  // finish() keeps a finished build when the card write fails; it is retried
  // once before the work is kept as a checkpoint instead.
  bool finishRetried = false;
  // finish() only takes effect once loop() returns; nothing may run after it.
  bool leaving = false;

  const BibleSearch::IndexReader* reader = nullptr;
  char query[MAX_QUERY_BYTES + 1] = {};
  bool hasSearched = false;
  std::vector<uint16_t> results;
  bool resultsTruncated = false;

  Row rows[ROW_CACHE];
  int rowsFirst = 0;  // result index of rows[0]
  int rowsCount = 0;
  uint8_t rowsGeneration = 0;
  // Loop-task only: the next rows are assembled here so render() never sees
  // a half-loaded page.
  Row staging[ROW_CACHE];
  BibleSearch::VerseEntry wantedEntries[ROW_CACHE];
  uint8_t wantedRows[ROW_CACHE] = {};

  // Render-task only.
  freeink::ui::ListItem listItems[LIST_WINDOW];
  uint8_t prewarmedGeneration = 0xFF;
  char headerTitle[48] = {};
  char statusLine[96] = {};
  char timeLine[48] = {};
  Rect progressBarRect{};
  bool progressBarVisible = false;
  // What the last build drew, for the loop to load any rows it lacked.
  std::atomic<int> viewTop{-1};
  std::atomic<int> viewRows{0};

  static void onPrepareEvent(const freeink::ui::ActionEvent& event, void* user);
  static void onCancelEvent(const freeink::ui::ActionEvent& event, void* user);
  static void onDismissEvent(const freeink::ui::ActionEvent& event, void* user);
  static void onVerseText(void* ctx, size_t wantedIndex, std::string_view text);

  void enterState(State next, bool fullRefresh = false);
  bool isBusy(State s) const;
  void runBusyWork();
  void openIndex();
  void beginBuild();
  void stepBuild();
  void finishBuild();
  void cancelBuild();
  void showPrompt(BibleSearch::IndexReader::Status status);
  void showFailure(BibleSearchIndexer::Failure reason);
  void publishProgress(bool force);
  void handleDialogInput();
  void leave();
  void goHome();

  void openKeyboard();
  void onQueryEntered(const char* text);
  void runSearch();
  void ensureRows(int listTop, int visibleRows);
  // Selection and paging for the results list. Each loads its page's rows
  // before requesting the render, so a page never paints without its text.
  void moveTo(int index);
  void scrollPage(int direction);
  void finishWithVerse(int resultIndex);

  void buildDialog(UiScreen& screen, const char* title, const char* message, const char* primaryLabel,
                   freeink::ui::ActionId primaryAction, const char* secondaryLabel,
                   freeink::ui::ActionId secondaryAction);
  void buildProgress(UiScreen& screen);
  void buildResults(UiScreen& screen);
  void prewarmRows();

  int listCount() const override;
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  bool handleCustomInput() override;
  void navigateButtons() override;
  void onBackButton() override;
  void drawChrome() override;
  void drawFooter() override;
};
