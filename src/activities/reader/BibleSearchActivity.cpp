#include "BibleSearchActivity.h"

#include <Arduino.h>
#include <BibleSearch/Query.h>
#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>
#include <Utf8.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <utility>

#include "MappedInputManager.h"
#include "activities/PostedMessage.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/UIScale.h"
#include "components/UITheme.h"
#include "study/BibleSearchStore.h"
#include "util/TaskWatchdog.h"

namespace fui = freeink::ui;

namespace {

constexpr const char* MODULE = "BSUI";
constexpr char ELLIPSIS[] = "...";
// Lines a dialog's message may wrap to before it is cut.
constexpr uint8_t DIALOG_MESSAGE_LINES = 5;

using Status = BibleSearch::IndexReader::Status;
using Failure = BibleSearchIndexer::Failure;

const char* promptMessage(const Status status) {
  switch (status) {
    case Status::Incomplete:
      return tr(STR_BIBLE_SEARCH_INCOMPLETE);
    case Status::Stale:
      return tr(STR_BIBLE_SEARCH_STALE);
    case Status::Unreadable:
    case Status::TooNew:
      return tr(STR_BIBLE_SEARCH_UNREADABLE);
    case Status::Missing:
    case Status::Ok:
      break;
  }
  return tr(STR_BIBLE_SEARCH_MISSING);
}

const char* failureMessage(const Failure failure) {
  switch (failure) {
    case Failure::NotABible:
      return tr(STR_BIBLE_SEARCH_FAIL_NOT_BIBLE);
    case Failure::OutOfMemory:
      return tr(STR_BIBLE_SEARCH_FAIL_MEMORY);
    case Failure::ReadFailed:
      return tr(STR_BIBLE_SEARCH_FAIL_READ);
    case Failure::OverBudget:
      return tr(STR_BIBLE_SEARCH_FAIL_BUDGET);
    case Failure::WriteFailed:
    case Failure::None:
      break;
  }
  return tr(STR_BIBLE_SEARCH_FAIL_WRITE);
}

// Cut on a UTF-8 boundary, with an ellipsis when the verse runs on. The list
// only adds its own ellipsis when the text overflows the row, which a 160-byte
// cut of a long verse may not.
void copySnippet(char* dest, const size_t destBytes, const std::string_view text) {
  if (text.size() < destBytes) {
    memcpy(dest, text.data(), text.size());
    dest[text.size()] = '\0';
    return;
  }
  const int room = static_cast<int>(destBytes - sizeof(ELLIPSIS));
  int kept = utf8SafeTruncateBuffer(text.data(), room);
  while (kept > 0 && text[kept - 1] == ' ') kept--;
  memcpy(dest, text.data(), static_cast<size_t>(kept));
  memcpy(dest + kept, ELLIPSIS, sizeof(ELLIPSIS));
}

void clearRow(auto& row) {
  row.result = -1;
  row.entry = BibleSearch::VerseEntry{};
  row.textAttempted = false;
  row.reference[0] = '\0';
  row.snippet[0] = '\0';
}

}  // namespace

const char* BibleBookNames::forBook(const uint8_t book) const {
  if (!names || book == 0 || book > count) return "";
  return names + static_cast<size_t>(book - 1) * static_cast<size_t>(stride);
}

BibleSearchActivity::BibleSearchActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                         std::shared_ptr<Epub> epub, const BibleBookNames bookNames)
    : UiListActivity("BibleSearch", renderer, mappedInput), epub(std::move(epub)), bookNames(bookNames) {}

void BibleSearchActivity::onEnter() {
  UiListActivity::onEnter();
  app.on(ACTION_PREPARE, &BibleSearchActivity::onPrepareEvent, this);
  app.on(ACTION_CANCEL, &BibleSearchActivity::onCancelEvent, this);
  app.on(ACTION_DISMISS, &BibleSearchActivity::onDismissEvent, this);
  // open() can resolve the Bible's documents first, which is seconds of SD on
  // the first search after boot, so it waits for this frame.
  enterState(State::Opening, /*fullRefresh=*/true);
}

void BibleSearchActivity::onExit() {
  if (indexer) {
    indexer->cancel();
    indexer.reset();
  }
  BibleSearchStore::getInstance().close();
  reader = nullptr;
  UiListActivity::onExit();
}

// --- State machine -------------------------------------------------------------

void BibleSearchActivity::enterState(const State next, const bool fullRefresh) {
  {
    RenderLock lock;
    state = next;
    // Entering a busy state it has already shown (a second search) must still
    // wait for the new frame. AwaitingQuery is never busy.
    shownState.store(State::AwaitingQuery);
  }
  if (fullRefresh) fullRefreshPending.store(true);
  requestUpdate();
}

bool BibleSearchActivity::isBusy(const State s) const {
  return s == State::Opening || s == State::Starting || s == State::Saving || s == State::Finishing ||
         s == State::Ready || s == State::Searching;
}

void BibleSearchActivity::loop() {
  if (leaving) return;

  switch (state) {
    case State::Building:
      stepBuild();
      return;
    case State::Prompt:
    case State::Failed:
      handleDialogInput();
      return;
    case State::Results: {
      UiListActivity::loop();
      if (leaving || state != State::Results) return;
      // A build can pull the viewport onto rows the loop never loaded (the
      // list's follow correction); fill them in for the next paint.
      const int top = viewTop.load();
      if (top >= 0) ensureRows(top, viewRows.load());
      return;
    }
    case State::AwaitingQuery:
      return;
    case State::Opening:
    case State::Starting:
    case State::Saving:
    case State::Finishing:
    case State::Ready:
    case State::Searching:
      if (state == State::Starting) {
        if (mappedInput.wasReleased(MappedInputManager::Button::Back)) cancelRequested = true;
        routeTouch(mappedInput);
      }
      if (shownState.load() == state) runBusyWork();
      return;
  }
}

void BibleSearchActivity::runBusyWork() {
  if (homeRequested && (state == State::Opening || state == State::Ready || state == State::Searching)) {
    goHome();
    return;
  }
  switch (state) {
    case State::Opening:
    case State::Ready:
      openIndex();
      return;
    case State::Starting:
      // Cancelled before begin() ran: there is nothing to save.
      if (cancelRequested) {
        cancelBuild();
        return;
      }
      beginBuild();
      return;
    case State::Searching:
      runSearch();
      return;
    case State::Saving:
      cancelBuild();
      return;
    case State::Finishing:
      finishBuild();
      return;
    default:
      return;
  }
}

void BibleSearchActivity::openIndex() {
  auto& store = BibleSearchStore::getInstance();
  Status status = Status::Missing;
  reader = store.open(epub, &status);
  if (reader) {
    openKeyboard();
    return;
  }
  // open() reports only on bible.idx; a matching checkpoint beside it is a
  // build that will resume, which the prompt and the starting frame say.
  status = store.status(epub);
  showPrompt(status);
}

void BibleSearchActivity::showPrompt(const Status status) {
  {
    RenderLock lock;
    promptStatus = status;
    state = State::Prompt;
  }
  requestUpdate();
}

void BibleSearchActivity::showFailure(const Failure reason) {
  LOG_ERR(MODULE, "Build failed: %d", static_cast<int>(reason));
  {
    RenderLock lock;
    failure = reason;
    state = State::Failed;
  }
  fullRefreshPending.store(true);
  requestUpdate();
}

void BibleSearchActivity::beginBuild() {
  // begin() closes the store's reader: finish() replaces its file.
  reader = nullptr;
  indexer = makeUniqueNoThrow<BibleSearchIndexer>(epub);
  if (!indexer) {
    LOG_ERR(MODULE, "OOM: indexer");
    showFailure(Failure::OutOfMemory);
    return;
  }
  if (!indexer->begin()) {
    const Failure reason = indexer->failure();
    indexer.reset();
    LOG_ERR(MODULE, "begin() failed: %d", static_cast<int>(reason));
    // The user already asked to leave while begin() ran.
    if (cancelRequested) {
      cancelBuild();
      return;
    }
    showFailure(reason == Failure::None ? Failure::NotABible : reason);
    return;
  }

  buildStartMs = millis();
  buildStartDocs = indexer->docsDone();
  publishedPercent = -1;
  lastProgressPublishMs = 0;
  finishRetried = false;
  LOG_INF(MODULE, "Build starts at document %u of %u", static_cast<unsigned>(buildStartDocs),
          static_cast<unsigned>(indexer->totalDocs()));

  publishProgress(/*force=*/true);
  if (cancelRequested) {
    enterState(State::Saving);
    return;
  }
  enterState(indexer->allDocumentsIndexed() ? State::Finishing : State::Building);
}

void BibleSearchActivity::stepBuild() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) cancelRequested = true;
  routeTouch(mappedInput);
  if (cancelRequested) {
    enterState(State::Saving);
    return;
  }

  indexer->step();
  if (indexer->failed()) {
    const Failure reason = indexer->failure();
    indexer.reset();
    showFailure(reason);
    return;
  }
  if (indexer->allDocumentsIndexed()) {
    publishProgress(/*force=*/true);
    enterState(State::Finishing);
    return;
  }
  publishProgress(/*force=*/false);
}

void BibleSearchActivity::publishProgress(const bool force) {
  const uint32_t done = indexer->docsDone();
  const uint32_t total = indexer->totalDocs();
  const int percent = total > 0 ? static_cast<int>(static_cast<uint64_t>(done) * 100 / total) : 0;
  const unsigned long now = millis();
  if (!force && (percent <= publishedPercent || now - lastProgressPublishMs < PROGRESS_MIN_INTERVAL_MS)) return;

  int minutesLeft = -1;
  const uint32_t sessionDocs = done - buildStartDocs;
  if (total > 0 && done < total && sessionDocs > 0 &&
      static_cast<uint64_t>(sessionDocs) * 100 >= static_cast<uint64_t>(total) * ETA_MIN_PERCENT) {
    const uint64_t remainingMs = static_cast<uint64_t>(now - buildStartMs) * (total - done) / sessionDocs;
    minutesLeft = remainingMs < 60000 ? 0 : static_cast<int>((remainingMs + 30000) / 60000);
  }

  {
    RenderLock lock;
    progress.done = done;
    progress.total = total;
    progress.book = indexer->currentBook();
    progress.chapter = indexer->currentChapter();
    progress.minutesLeft = minutesLeft;
  }
  publishedPercent = percent;
  lastProgressPublishMs = now;
  requestUpdate();
}

void BibleSearchActivity::finishBuild() {
  if (!indexer->finish()) {
    const Failure reason = indexer->failure();
    if (!indexer->failed()) {
      // A card write failed and the finished build is still held. The retry
      // runs on the next pass: the Finishing frame is already up.
      if (!finishRetried) {
        finishRetried = true;
        LOG_ERR(MODULE, "Index write failed (%d); retrying once", static_cast<int>(reason));
        return;
      }
      // Kept as a checkpoint, so the next attempt only has to write.
      indexer->cancel();
    }
    indexer.reset();
    if (homeRequested) {
      LOG_ERR(MODULE, "Index not written (%d); going home as asked", static_cast<int>(reason));
      goHome();
      return;
    }
    showFailure(reason == Failure::None ? Failure::WriteFailed : reason);
    return;
  }
  indexer.reset();
  LOG_INF(MODULE, "Build finished in %lu s", (millis() - buildStartMs) / 1000);
  if (homeRequested) {
    goHome();
    return;
  }
  // Ready's work opens the new index, and its frame stays up while it does.
  enterState(State::Ready, /*fullRefresh=*/true);
}

void BibleSearchActivity::cancelBuild() {
  if (indexer) {
    indexer->cancel();
    indexer.reset();
  }
  if (goHomeAfterCancel) {
    goHome();
    return;
  }
  leave();
}

void BibleSearchActivity::goHome() {
  leaving = true;
  app.clearTapFlash();
  onGoHome();
}

void BibleSearchActivity::leave() {
  leaving = true;
  app.clearTapFlash();
  ActivityResult result;
  result.isCancelled = true;
  setResult(std::move(result));
  finish();
}

bool BibleSearchActivity::handleHomeGesture() {
  switch (state) {
    case State::Starting:
    case State::Building:
      cancelRequested = true;
      goHomeAfterCancel = true;
      return true;
    case State::Saving:
      goHomeAfterCancel = true;
      return true;
    case State::Opening:
    case State::Finishing:
    case State::Ready:
    case State::Searching:
      homeRequested = true;
      return true;
    default:
      return false;
  }
}

void BibleSearchActivity::handleDialogInput() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    leave();
    return;
  }
  if (state == State::Prompt && mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    prepareRequested = true;
  }
  routeTouch(mappedInput);

  if (prepareRequested && state == State::Prompt) {
    prepareRequested = false;
    // The first frame of the progress view: begin() can take seconds to
    // resolve the book map or replay a checkpoint, and runs once it lands.
    {
      RenderLock lock;
      progress = Progress{};
    }
    cancelRequested = false;
    goHomeAfterCancel = false;
    enterState(State::Starting, /*fullRefresh=*/true);
    return;
  }
  if (dismissRequested) {
    dismissRequested = false;
    leave();
  }
}

void BibleSearchActivity::onPrepareEvent(const fui::ActionEvent&, void* user) {
  auto* self = static_cast<BibleSearchActivity*>(user);
  self->app.clearTapFlash();
  self->prepareRequested = true;
}

void BibleSearchActivity::onCancelEvent(const fui::ActionEvent&, void* user) {
  auto* self = static_cast<BibleSearchActivity*>(user);
  self->app.clearTapFlash();
  self->cancelRequested = true;
}

void BibleSearchActivity::onDismissEvent(const fui::ActionEvent&, void* user) {
  auto* self = static_cast<BibleSearchActivity*>(user);
  self->app.clearTapFlash();
  self->dismissRequested = true;
}

// --- Query and results ------------------------------------------------------------

void BibleSearchActivity::openKeyboard() {
  {
    RenderLock lock;
    state = State::AwaitingQuery;
  }
  startActivityForResult(std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_SEARCH_BIBLE),
                                                                 std::string(query), MAX_QUERY_BYTES),
                         [this](const ActivityResult& result) {
                           const auto* entered =
                               result.isCancelled ? nullptr : std::get_if<KeyboardResult>(&result.data);
                           if (entered) {
                             onQueryEntered(entered->text.c_str());
                             return;
                           }
                           if (hasSearched) {
                             enterState(State::Results);
                           } else {
                             leave();
                           }
                         });
}

void BibleSearchActivity::onQueryEntered(const char* text) {
  const size_t length = strnlen(text, MAX_QUERY_BYTES);
  const int kept = utf8SafeTruncateBuffer(text, static_cast<int>(length));
  {
    RenderLock lock;
    memcpy(query, text, static_cast<size_t>(kept));
    query[kept] = '\0';
  }
  // runQuery and the first page's verse text wait for this frame.
  enterState(State::Searching);
}

void BibleSearchActivity::runSearch() {
  if (!reader) {
    showPrompt(Status::Unreadable);
    return;
  }

  [[maybe_unused]] const unsigned long started = millis();
  BibleSearch::QueryResult found = BibleSearch::runQuery(*reader, query);
  if (!found.ok) {
    LOG_ERR(MODULE, "Query '%s' could not read the index", query);
    BibleSearchStore::getInstance().close();
    reader = nullptr;
    showPrompt(Status::Unreadable);
    return;
  }
  LOG_INF(MODULE, "'%s' matched %u verse(s)%s in %lu ms", query, static_cast<unsigned>(found.verses.size()),
          found.truncated ? " (truncated)" : "", millis() - started);

  {
    RenderLock lock;
    results = std::move(found.verses);
    resultsTruncated = found.truncated;
    rowsFirst = 0;
    rowsCount = 0;
    rowsGeneration++;
    // Row 0 is Edit search; the selection starts on the first verse.
    nav.reset(results.empty() ? 0 : 1);
    hasSearched = true;
    state = State::Results;
  }
  viewTop.store(-1);
  const int knownRows = viewRows.load();
  ensureRows(0, knownRows > 0 ? knownRows : ROW_CACHE / 2);
  requestUpdate();
}

int BibleSearchActivity::listCount() const {
  return state == State::Results ? 1 + static_cast<int>(results.size()) : 0;
}

void BibleSearchActivity::ensureRows(const int listTop, const int visibleRows) {
  if (!reader) return;
  const int total = static_cast<int>(results.size());
  const int first = std::max(listTop, 1) - 1;
  if (first >= total) return;
  const int end = std::min({listTop + std::max(visibleRows, 1) - 1, total, first + ROW_CACHE});

  bool covered = first >= rowsFirst && end <= rowsFirst + rowsCount;
  for (int r = first; covered && r < end; r++) covered = rows[r - rowsFirst].textAttempted;
  if (covered) return;

  const int count = std::min(ROW_CACHE, total - first);
  for (int i = 0; i < count; i++) {
    const int r = first + i;
    Row& row = staging[i];
    if (r >= rowsFirst && r < rowsFirst + rowsCount) {
      row = rows[r - rowsFirst];
      continue;
    }
    clearRow(row);
    row.result = r;
    if (!reader->verse(results[r], row.entry)) {
      LOG_ERR(MODULE, "Cannot read verse %u from the index", static_cast<unsigned>(results[r]));
      row.textAttempted = true;
      continue;
    }
    const char* name = bookNames.forBook(row.entry.book);
    if (name[0] != '\0') {
      snprintf(row.reference, sizeof(row.reference), "%s %u:%u", name, static_cast<unsigned>(row.entry.chapter),
               static_cast<unsigned>(row.entry.verse));
    } else {
      snprintf(row.reference, sizeof(row.reference), "%u:%u", static_cast<unsigned>(row.entry.chapter),
               static_cast<unsigned>(row.entry.verse));
    }
  }

  // One streamed document serves every visible row it holds.
  const int textRows = end - first;
  for (int i = 0; i < textRows; i++) {
    if (staging[i].textAttempted) continue;
    const uint16_t spine = staging[i].entry.spine;
    size_t wanted = 0;
    for (int j = i; j < textRows; j++) {
      if (staging[j].textAttempted || staging[j].entry.spine != spine) continue;
      wantedEntries[wanted] = staging[j].entry;
      wantedRows[wanted] = static_cast<uint8_t>(j);
      wanted++;
      staging[j].textAttempted = true;
    }
    // A document that cannot be read leaves its rows showing the reference.
    if (!BibleSearchStore::verseTexts(*epub, spine, wantedEntries, wanted, &BibleSearchActivity::onVerseText, this)) {
      LOG_ERR(MODULE, "No verse text for spine %u", static_cast<unsigned>(spine));
    }
    resetTaskWatchdogIfSubscribed();
  }

  {
    RenderLock lock;
    for (int i = 0; i < count; i++) rows[i] = staging[i];
    rowsFirst = first;
    rowsCount = count;
    rowsGeneration++;
  }
  requestUpdate();
}

void BibleSearchActivity::onVerseText(void* ctx, const size_t wantedIndex, const std::string_view text) {
  auto* self = static_cast<BibleSearchActivity*>(ctx);
  char* const snippet = self->staging[self->wantedRows[wantedIndex]].snippet;
  copySnippet(snippet, sizeof(Row::snippet), text);
}

void BibleSearchActivity::moveTo(const int index) {
  int top = 0;
  int visible = 0;
  {
    RenderLock lock;
    nav.selected = index;
    nav.follow(listCount());
    top = nav.top;
    visible = nav.pageRows();
  }
  ensureRows(top, visible);
  requestUpdate();
}

void BibleSearchActivity::scrollPage(const int direction) {
  bool moved = false;
  int top = 0;
  int visible = 0;
  {
    RenderLock lock;
    moved = nav.scrollBy(direction * nav.pageRows(), listCount());
    top = nav.top;
    visible = nav.pageRows();
  }
  if (!moved) return;
  ensureRows(top, visible);
  requestUpdate();
}

bool BibleSearchActivity::handleCustomInput() {
  const auto swipe = mappedInput.wasSwipe();
  if (swipe != MappedInputManager::SwipeDir::Up && swipe != MappedInputManager::SwipeDir::Down) return false;
  // Consumed here rather than by the base, so the page's rows are loaded
  // before the render it requests.
  scrollPage(swipe == MappedInputManager::SwipeDir::Up ? 1 : -1);
  return true;
}

void BibleSearchActivity::navigateButtons() {
  const int count = listCount();
  buttonNavigator.onNextRelease([this, count] { moveTo(ButtonNavigator::nextIndex(nav.selected, count)); });
  buttonNavigator.onPreviousRelease([this, count] { moveTo(ButtonNavigator::previousIndex(nav.selected, count)); });
  buttonNavigator.onNextContinuous(
      [this, count] { moveTo(ButtonNavigator::nextPageIndex(nav.selected, count, nav.pageRows())); });
  buttonNavigator.onPreviousContinuous(
      [this, count] { moveTo(ButtonNavigator::previousPageIndex(nav.selected, count, nav.pageRows())); });
}

void BibleSearchActivity::onBackButton() { leave(); }

void BibleSearchActivity::activateIndex(const int index) {
  if (state != State::Results || index < 0 || index >= listCount()) return;
  if (index == 0) {
    app.clearTapFlash();
    openKeyboard();
    return;
  }
  finishWithVerse(index - 1);
}

void BibleSearchActivity::finishWithVerse(const int resultIndex) {
  BibleSearch::VerseEntry entry{};
  if (resultIndex >= rowsFirst && resultIndex < rowsFirst + rowsCount && rows[resultIndex - rowsFirst].result >= 0) {
    entry = rows[resultIndex - rowsFirst].entry;
  } else if (!reader || !reader->verse(results[resultIndex], entry)) {
    LOG_ERR(MODULE, "Cannot read verse %u from the index", static_cast<unsigned>(results[resultIndex]));
    return;
  }
  if (entry.book == 0) {
    LOG_ERR(MODULE, "Result %d has no verse entry", resultIndex);
    return;
  }
  leaving = true;
  app.clearTapFlash();
  // The verse grid's hand-off: VerseEntry.offset is the same VerseAnchors
  // visible-codepoint offset the grid passes.
  setResult(ChapterResult{static_cast<int>(entry.spine), "", entry.offset});
  finish();
}

// --- Rendering --------------------------------------------------------------------

void BibleSearchActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  screen.setContentMargin(fui::Insets{static_cast<int16_t>(safe.y + metrics.topPadding + metrics.headerHeight),
                                      static_cast<int16_t>(renderer.getScreenWidth() - (safe.x + safe.width)),
                                      static_cast<int16_t>(renderer.getScreenHeight() - (safe.y + safe.height)),
                                      static_cast<int16_t>(safe.x)});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));
  progressBarVisible = false;

  switch (state) {
    case State::Prompt:
      buildDialog(screen, tr(STR_BIBLE_SEARCH_PREPARE_TITLE), promptMessage(promptStatus), tr(STR_BIBLE_SEARCH_PREPARE),
                  ACTION_PREPARE, tr(STR_CANCEL), ACTION_DISMISS);
      return;
    case State::Failed:
      buildDialog(screen, tr(STR_BIBLE_SEARCH_FAILED), failureMessage(failure), tr(STR_BACK), ACTION_DISMISS, nullptr,
                  fui::NO_ACTION);
      return;
    case State::Starting:
    case State::Building:
    case State::Saving:
    case State::Finishing:
      buildProgress(screen);
      return;
    case State::Ready: {
      fui::TextStyle title = screen.theme().titleText;
      title.align = fui::TextAlign::Center;
      screen.centeredText(tr(STR_BIBLE_SEARCH_READY), title);
      return;
    }
    case State::Results:
      buildResults(screen);
      return;
    case State::Searching:
      screen.centeredText(tr(STR_BIBLE_SEARCH_SEARCHING), screen.theme().bodyText);
      return;
    case State::Opening:
    case State::AwaitingQuery:
      screen.centeredText(tr(STR_LOADING_POPUP), screen.theme().bodyText);
      return;
  }
}

void BibleSearchActivity::buildDialog(UiScreen& screen, const char* title, const char* message,
                                      const char* primaryLabel, const fui::ActionId primaryAction,
                                      const char* secondaryLabel, const fui::ActionId secondaryAction) {
  const auto& theme = screen.theme();
  fui::TextStyle titleStyle = theme.titleText;
  titleStyle.align = fui::TextAlign::Center;
  fui::TextStyle messageStyle = theme.bodyText;
  messageStyle.align = fui::TextAlign::Center;
  messageStyle.maxLines = DIALOG_MESSAGE_LINES;

  const int16_t titleHeight = screen.target().lineHeight(titleStyle.font);
  const auto messageHeight = static_cast<int16_t>(screen.target().lineHeight(messageStyle.font) * DIALOG_MESSAGE_LINES);
  const int16_t gap = theme.spaceLg;
  const int16_t buttonHeight = theme.rowHeight;
  const auto blockHeight = static_cast<int16_t>(titleHeight + messageHeight + buttonHeight + gap * 2);
  const fui::Rect body = screen.body();
  if (body.height > blockHeight) screen.spacer(static_cast<int16_t>((body.height - blockHeight) / 2));

  screen.target().text(screen.takeTop(titleHeight, gap), title, titleStyle);
  screen.target().text(screen.takeTop(messageHeight, gap), message, messageStyle);

  const fui::Rect area = screen.takeTop(buttonHeight);
  const auto buttonWidth = static_cast<int16_t>(area.width / 3);
  fui::ButtonProps primary;
  primary.label = primaryLabel;
  primary.action = primaryAction;
  if (!secondaryLabel) {
    screen.button(primary, fui::Rect{static_cast<int16_t>(area.x + (area.width - buttonWidth) / 2), area.y, buttonWidth,
                                     buttonHeight});
    return;
  }
  const auto pairLeft = static_cast<int16_t>(area.x + (area.width - buttonWidth * 2 - gap) / 2);
  fui::ButtonProps secondary;
  secondary.label = secondaryLabel;
  secondary.action = secondaryAction;
  screen.button(secondary, fui::Rect{pairLeft, area.y, buttonWidth, buttonHeight});
  screen.button(primary,
                fui::Rect{static_cast<int16_t>(pairLeft + buttonWidth + gap), area.y, buttonWidth, buttonHeight});
}

void BibleSearchActivity::buildProgress(UiScreen& screen) {
  const auto& theme = screen.theme();
  const auto& metrics = UITheme::getInstance().getMetrics();
  fui::TextStyle titleStyle = theme.titleText;
  titleStyle.align = fui::TextAlign::Center;
  fui::TextStyle noteStyle = theme.smallText;
  noteStyle.align = fui::TextAlign::Center;
  noteStyle.maxLines = 2;
  fui::TextStyle statusStyle = theme.bodyText;
  statusStyle.align = fui::TextAlign::Center;
  fui::TextStyle timeStyle = theme.smallText;
  timeStyle.align = fui::TextAlign::Center;

  const int16_t titleHeight = screen.target().lineHeight(titleStyle.font);
  const int16_t smallHeight = screen.target().lineHeight(noteStyle.font);
  const int16_t statusHeight = screen.target().lineHeight(statusStyle.font);
  // The theme prints the percentage under the bar, inside this band.
  const auto barBand = static_cast<int16_t>(metrics.progressBarHeight + smallHeight * 2);
  const int16_t gap = theme.spaceMd;
  const int16_t buttonHeight = theme.rowHeight;
  const auto blockHeight = static_cast<int16_t>(titleHeight + smallHeight * 2 + barBand + statusHeight + smallHeight +
                                                buttonHeight + gap * 6);
  const fui::Rect body = screen.body();
  if (body.height > blockHeight) screen.spacer(static_cast<int16_t>((body.height - blockHeight) / 2));

  screen.target().text(screen.takeTop(titleHeight, gap), tr(STR_BIBLE_SEARCH_PREPARING), titleStyle);
  screen.target().text(screen.takeTop(static_cast<int16_t>(smallHeight * 2), static_cast<int16_t>(gap * 2)),
                       tr(STR_BIBLE_SEARCH_PREPARING_NOTE), noteStyle);

  const fui::Rect bar = screen.takeTop(barBand, gap);
  progressBarRect = Rect{bar.x + metrics.contentSidePadding, bar.y, bar.width - metrics.contentSidePadding * 2,
                         metrics.progressBarHeight};
  progressBarVisible = true;

  statusLine[0] = '\0';
  if (state == State::Saving) {
    snprintf(statusLine, sizeof(statusLine), "%s", tr(STR_BIBLE_SEARCH_SAVING_PROGRESS));
  } else if (state == State::Finishing) {
    snprintf(statusLine, sizeof(statusLine), "%s", tr(STR_BIBLE_SEARCH_FINISHING));
  } else if (state == State::Starting) {
    snprintf(statusLine, sizeof(statusLine), "%s",
             promptStatus == Status::Incomplete ? tr(STR_BIBLE_SEARCH_RESUMING) : tr(STR_BIBLE_SEARCH_STARTING));
  } else if (state == State::Building && progress.book != 0) {
    const char* name = bookNames.forBook(progress.book);
    const int chapter = static_cast<int>(progress.chapter);
    if (name[0] != '\0' && chapter > 0) {
      snprintf(statusLine, sizeof(statusLine), tr(STR_BIBLE_SEARCH_STATUS), name, chapter);
    } else if (chapter > 0) {
      snprintf(statusLine, sizeof(statusLine), tr(STR_BIBLE_SEARCH_CHAPTER), chapter);
    } else {
      snprintf(statusLine, sizeof(statusLine), "%s", name);
    }
  }
  screen.target().text(screen.takeTop(statusHeight, gap), statusLine, statusStyle);

  timeLine[0] = '\0';
  if (state == State::Building && progress.minutesLeft == 0) {
    snprintf(timeLine, sizeof(timeLine), "%s", tr(STR_BIBLE_SEARCH_TIME_LEFT_SHORT));
  } else if (state == State::Building && progress.minutesLeft > 0) {
    snprintf(timeLine, sizeof(timeLine), tr(STR_BIBLE_SEARCH_TIME_LEFT), progress.minutesLeft);
  }
  screen.target().text(screen.takeTop(smallHeight, static_cast<int16_t>(gap * 2)), timeLine, timeStyle);

  // The band is reserved in every progress state so nothing moves when the
  // button comes and goes. While Starting, a tap is remembered and acted on as
  // soon as begin() returns.
  const fui::Rect area = screen.takeTop(buttonHeight);
  if (state != State::Building && state != State::Starting) return;
  const auto buttonWidth = static_cast<int16_t>(area.width / 3);
  fui::ButtonProps cancel;
  cancel.label = tr(STR_CANCEL);
  cancel.action = ACTION_CANCEL;
  screen.button(cancel, fui::Rect{static_cast<int16_t>(area.x + (area.width - buttonWidth) / 2), area.y, buttonWidth,
                                  buttonHeight});
}

void BibleSearchActivity::buildResults(UiScreen& screen) {
  const auto& theme = screen.theme();
  const int count = listCount();

  if (results.empty()) {
    fui::TextStyle empty = theme.bodyText;
    empty.align = fui::TextAlign::Center;
    empty.maxLines = 2;
    const auto height = static_cast<int16_t>(screen.target().lineHeight(empty.font) * 2);
    screen.target().text(screen.takeTop(height, theme.spaceLg), tr(STR_BIBLE_SEARCH_NO_RESULTS), empty);
  }

  fui::ListProps props;
  props.count = static_cast<uint16_t>(count);
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;  // physical buttons stay in loop()
  // Theme first: Screen::list only themes a style that is still unset, and a
  // maxLines of 2 marks it as set.
  props.subtitleText = theme.smallText;
  props.subtitleText.maxLines = 2;
  syncListViewport(screen, props, /*hasSubtitle=*/true);

  const int windowFirst = std::clamp(nav.top, 0, std::max(0, count - LIST_WINDOW));
  const int windowCount = std::min(LIST_WINDOW, count - windowFirst);
  for (int i = 0; i < windowCount; i++) {
    const int listIndex = windowFirst + i;
    fui::ListItem item;
    item.actionValue = static_cast<int16_t>(listIndex);
    if (listIndex == 0) {
      item.label = tr(STR_BIBLE_SEARCH_EDIT);
      if (query[0] != '\0') item.subtitle = query;
    } else {
      const int result = listIndex - 1;
      if (result >= rowsFirst && result < rowsFirst + rowsCount) {
        const Row& row = rows[result - rowsFirst];
        item.label = row.reference;
        if (row.snippet[0] != '\0') item.subtitle = row.snippet;
      } else {
        item.label = "";
      }
    }
    listItems[i] = item;
  }
  props.items = listItems;
  props.itemsWindowFirst = static_cast<uint16_t>(windowFirst);

  prewarmRows();
  screen.list(props);
  viewTop.store(nav.top);
  viewRows.store(nav.pageRows());
}

void BibleSearchActivity::prewarmRows() {
  if (prewarmedGeneration == rowsGeneration) return;
  prewarmedGeneration = rowsGeneration;

  // One SD pass for the page's fallback glyphs; otherwise, under heap
  // pressure, each repaint re-reads them row by row. Mirrors the book grid.
  struct PrewarmCtx {
    const Row* rows;
    int count;
    bool snippets;
  };
  const auto getter = [](const void* ctx, const uint32_t i) -> const char* {
    const auto* c = static_cast<const PrewarmCtx*>(ctx);
    if (i >= static_cast<uint32_t>(c->count)) return nullptr;
    return c->snippets ? c->rows[i].snippet : c->rows[i].reference;
  };
  const auto labelStyle =
      UITheme::getInstance().getMetrics().listTitleBold ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR;
  const PrewarmCtx references{rows, rowsCount, false};
  renderer.prewarmFallbackText(uiScaleSpec().bodyFontId, getter, &references, static_cast<uint32_t>(rowsCount),
                               labelStyle);
  const PrewarmCtx snippets{rows, rowsCount, true};
  renderer.prewarmFallbackText(uiScaleSpec().smallFontId, getter, &snippets, static_cast<uint32_t>(rowsCount));
}

void BibleSearchActivity::drawChrome() {
  const char* title = tr(STR_SEARCH_BIBLE);
  if (state == State::Results) {
    snprintf(headerTitle, sizeof(headerTitle),
             resultsTruncated ? tr(STR_BIBLE_SEARCH_RESULTS_MORE) : tr(STR_BIBLE_SEARCH_RESULTS),
             static_cast<int>(results.size()));
    title = headerTitle;
  }
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  GUI.drawHeader(renderer, Rect{safe.x, safe.y + metrics.topPadding, safe.width, metrics.headerHeight}, title);
}

void BibleSearchActivity::drawFooter() {
  switch (state) {
    case State::Results:
      UiListActivity::drawFooter();
      return;
    case State::Prompt: {
      const auto labels = mappedInput.mapLabels(tr(STR_CANCEL), tr(STR_BIBLE_SEARCH_PREPARE), "", "");
      GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
      return;
    }
    case State::Starting:
    case State::Building: {
      const auto labels = mappedInput.mapLabels(tr(STR_CANCEL), "", "", "");
      GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
      return;
    }
    case State::Failed: {
      const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
      GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
      return;
    }
    default:
      return;
  }
}

void BibleSearchActivity::render(RenderLock&&) {
  // Mirrors UiListActivity::render(), with the theme's progress bar drawn over
  // the app and the refresh mode chosen per frame: a full refresh on entering
  // the screen, the progress view and completion; fast ones in between.
  const State drawn = state;
  const bool full = fullRefreshPending.exchange(false);

  renderer.clearScreen();
  drawChrome();
  renderUi();
  for (int pass = 0; activeNav().consumeRebuildNeeded() && pass < 8; ++pass) {
    renderer.clearScreen();
    drawChrome();
    renderUi();
  }
  if (progressBarVisible) {
    // A total of 0 (before begin() has counted) still draws an empty bar.
    const size_t total = progress.total > 0 ? progress.total : 1;
    GUI.drawProgressBar(renderer, progressBarRect, progress.total > 0 ? progress.done : 0, total);
  }
  drawFooter();
  renderer.displayBuffer(full ? HalDisplay::HALF_REFRESH : HalDisplay::FAST_REFRESH);
  shownState.store(drawn);
  PostedMessage::drawNext(renderer);
}
