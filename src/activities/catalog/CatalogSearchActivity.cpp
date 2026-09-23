#include "CatalogSearchActivity.h"

#include "activities/PostedMessage.h"

// clang-format off
// HttpDownloader.h (through PublicationDownloader) pulls Arduino/SdFat, whose
// macros collide with lwip's ip4_addr.h unless seen first. Pin this order;
// clang-format would otherwise sort the local headers last and break the build.
#include "network/PublicationDownloader.h"
#include <Arduino.h>
#include <Catalog/CatalogIndex.h>
#include <Catalog/CatalogStamp.h>
#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>
#include <WiFi.h>
// clang-format on

#include <cstdio>

#include "MappedInputManager.h"
#include "SilentRestart.h"
#include "activities/network/WifiSelectionActivity.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "network/CatalogIndexStore.h"

namespace fui = freeink::ui;

namespace {

constexpr const char* MODULE = "BUSCAR";
constexpr int DOWNLOAD_PROGRESS_STEP_PERCENT = 5;
constexpr unsigned long DOWNLOAD_PROGRESS_MIN_UPDATE_MS = 5000;
constexpr size_t MAX_QUERY_LENGTH = 48;

// A query that is one whitespace-free token can be a publication symbol, which
// GETPUBMEDIALINKS resolves without the index. That path survives the index
// being stale, absent or discontinued, so it is offered whenever it applies.
bool looksLikeSymbol(const std::string& text) {
  if (text.empty() || text.size() > 16) return false;
  return text.find(' ') == std::string::npos;
}

// The identifying line under a result's title: what the user would have to type
// to reach the same publication without the index.
std::string describeHit(const std::string& symbol, const std::string& year, const std::string& issue) {
  std::string out = symbol;
  if (!year.empty()) out += "  " + year;
  if (!issue.empty()) out += "  " + issue;
  return out;
}

}  // namespace

CatalogSearchActivity::CatalogSearchActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : UiListActivity("CatalogSearch", renderer, mappedInput) {}

void CatalogSearchActivity::onEnter() {
  UiListActivity::onEnter();

  auto& store = CatalogIndexStore::getInstance();
  const auto status = store.load();
  indexUnavailable = status != CatalogIndexStore::Status::Ok;
  if (indexUnavailable && status != CatalogIndexStore::Status::NotHeld) {
    LOG_ERR(MODULE, "Cached index unusable: %d", static_cast<int>(status));
  }

  downloadFolder = publication::resolveDownloadFolder();
  refreshCatalogSubtitle();
  rowsDirty = true;
  requestUpdate();
}

void CatalogSearchActivity::onExit() {
  // The 217 KB working set is the whole reason the index is loaded lazily.
  CatalogIndexStore::getInstance().release();
  Activity::onExit();

  if (WiFi.getMode() != WIFI_MODE_NULL) {
    WiFi.disconnect(false);
    delay(30);
    silentRestart();
  }
}

// --- Catalog state -----------------------------------------------------------

void CatalogSearchActivity::refreshCatalogSubtitle() {
  const auto& store = CatalogIndexStore::getInstance();
  const catalog::Stamp& stamp = store.stamp();

  if (!stamp.valid()) {
    catalogSubtitle = tr(STR_CATALOG_NOT_DOWNLOADED);
    return;
  }

  char date[32] = "";
  catalog::formatIndexDate(stamp.builtOn, tr(STR_MONTHS_SHORT), date, sizeof(date));
  catalogSubtitle = std::string(tr(STR_CATALOG)) + ": " + date;
  if (store.hasStaged()) {
    catalogSubtitle += "  -  ";
    catalogSubtitle += tr(STR_CATALOG_UPDATE_AVAILABLE);
  }
}

void CatalogSearchActivity::runCatalogAction() {
  auto& store = CatalogIndexStore::getInstance();

  if (store.hasStaged()) {
    const auto applied = store.applyStaged();
    if (applied != CatalogIndexStore::Status::Ok) {
      LOG_ERR(MODULE, "Applying the staged index failed: %d", static_cast<int>(applied));
      fail(tr(STR_CATALOG_FETCH_FAILED));
      return;
    }
    indexUnavailable = false;
    refreshCatalogSubtitle();
    runSearch();
    state = State::BROWSING;
    rowsDirty = true;
    requestUpdate();
    return;
  }

  state = State::FETCHING_INDEX;
  statusMessage = tr(STR_CATALOG_DOWNLOADING);
  downloadProgress = 0;
  downloadTotal = 0;
  lastRenderedPercent = -1;
  lastProgressUpdateMs = 0;
  cancelDownload = false;
  requestUpdateAndWait();

  const bool hadIndex = store.held();
  const auto status = store.checkRemote(&CatalogSearchActivity::onIndexProgress, this, &cancelDownload);

  if (status == CatalogIndexStore::Status::Cancelled) {
    state = State::BROWSING;
    rowsDirty = true;
    requestUpdate();
    return;
  }
  if (status == CatalogIndexStore::Status::UpToDate) {
    refreshCatalogSubtitle();
    catalogSubtitle += "  -  ";
    catalogSubtitle += tr(STR_CATALOG_UP_TO_DATE);
    state = State::BROWSING;
    rowsDirty = true;
    requestUpdate();
    return;
  }
  if (status != CatalogIndexStore::Status::Ok) {
    // A failed fetch leaves the previous index in place and says so.
    LOG_ERR(MODULE, "Index fetch failed: %d", static_cast<int>(status));
    fail(tr(STR_CATALOG_FETCH_FAILED));
    return;
  }

  // Nothing was held, so there is no "available update" to report -- install it
  // rather than asking the user to tap the same row twice.
  if (!hadIndex) {
    const auto applied = store.applyStaged();
    if (applied != CatalogIndexStore::Status::Ok) {
      LOG_ERR(MODULE, "Installing the first index failed: %d", static_cast<int>(applied));
      fail(tr(STR_CATALOG_FETCH_FAILED));
      return;
    }
    indexUnavailable = false;
    runSearch();
  }

  refreshCatalogSubtitle();
  state = State::BROWSING;
  rowsDirty = true;
  requestUpdate();
}

// --- Search ------------------------------------------------------------------

void CatalogSearchActivity::runSearch() {
  hits.clear();
  resultsTruncated = false;
  if (query.empty()) return;

  const auto& store = CatalogIndexStore::getInstance();
  if (!store.held()) return;

  const std::string_view index = store.view();
  hits.reserve(MAX_RESULTS);

  size_t cursor = catalog::recordsBegin(index);
  catalog::Entry entry;
  while (catalog::nextEntry(index, cursor, entry)) {
    if (!catalog::matches(entry, query)) continue;
    if (hits.size() >= MAX_RESULTS) {
      resultsTruncated = true;
      break;
    }
    hits.push_back(
        Hit{std::string(entry.symbol), std::string(entry.issue), std::string(entry.title), std::string(entry.year)});
  }

  LOG_INF(MODULE, "'%s' matched %u row(s)%s", query.c_str(), static_cast<unsigned>(hits.size()),
          resultsTruncated ? " (truncated)" : "");
}

bool CatalogSearchActivity::hasSymbolRow() const { return looksLikeSymbol(query); }

// --- Rows --------------------------------------------------------------------

int CatalogSearchActivity::listCount() const { return firstHitRow() + static_cast<int>(hits.size()); }

void CatalogSearchActivity::rebuildRowItems() {
  const int rows = listCount();
  rowLabels.assign(rows, std::string());
  rowSubtitles.assign(rows, std::string());
  rowItems.clear();
  rowItems.reserve(rows);

  auto& store = CatalogIndexStore::getInstance();

  rowLabels[queryRow()] = query.empty() ? tr(STR_SEARCH_PROMPT) : query;
  if (query.empty()) {
    rowSubtitles[queryRow()] = tr(STR_SEARCH_HINT);
  } else if (hits.empty()) {
    rowSubtitles[queryRow()] = indexUnavailable ? tr(STR_CATALOG_NOT_DOWNLOADED) : tr(STR_NO_RESULTS);
  } else {
    char count[32];
    snprintf(count, sizeof(count), resultsTruncated ? "%u+" : "%u", static_cast<unsigned>(hits.size()));
    rowSubtitles[queryRow()] = std::string(count) + " " + tr(STR_SEARCH_MATCHES);
  }

  rowLabels[catalogRow()] = store.hasStaged()       ? tr(STR_CATALOG_INSTALL_UPDATE)
                            : store.stamp().valid() ? tr(STR_CATALOG_CHECK_UPDATE)
                                                    : tr(STR_CATALOG_DOWNLOAD);
  rowSubtitles[catalogRow()] = catalogSubtitle;

  if (hasSymbolRow()) {
    rowLabels[symbolRow()] = std::string(tr(STR_DOWNLOAD_SYMBOL)) + " \"" + query + "\"";
    rowSubtitles[symbolRow()] = tr(STR_DOWNLOAD_SYMBOL_HINT);
  }

  for (size_t i = 0; i < hits.size(); ++i) {
    const int row = firstHitRow() + static_cast<int>(i);
    rowLabels[row] = hits[i].title;
    rowSubtitles[row] = describeHit(hits[i].symbol, hits[i].year, hits[i].issue);
  }

  for (int i = 0; i < rows; ++i) {
    fui::ListItem item;
    item.label = rowLabels[i].c_str();
    if (!rowSubtitles[i].empty()) item.subtitle = rowSubtitles[i].c_str();
    item.actionValue = static_cast<int16_t>(i);
    rowItems.push_back(item);
  }
}

void CatalogSearchActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  screen.setContentMargin(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), 0,
                                      static_cast<int16_t>(metrics.buttonHintsHeight), 0});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  if (rowsDirty) {
    rebuildRowItems();
    rowsDirty = false;
  }

  fui::ListProps props;
  props.items = rowItems.data();
  props.count = static_cast<uint16_t>(rowItems.size());
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;  // physical buttons stay in loop()
  props.valueInset = 8;
  syncListViewport(screen, props, /*hasSubtitle=*/true);
  screen.list(props);
}

// --- Activation --------------------------------------------------------------

void CatalogSearchActivity::activateIndex(const int index) {
  if (state != State::BROWSING) return;
  nav.selected = index;
  app.clearTapFlash();

  if (index == queryRow()) {
    openKeyboard();
    return;
  }
  if (index == catalogRow()) {
    if (ensureWifi(Pending::CatalogAction)) runCatalogAction();
    return;
  }
  if (index == symbolRow()) {
    pendingHit = 0;
    if (ensureWifi(Pending::Download)) downloadBySymbol(query, "");
    return;
  }

  const int hitIndex = index - firstHitRow();
  if (hitIndex < 0 || hitIndex >= static_cast<int>(hits.size())) return;
  pendingHit = static_cast<size_t>(hitIndex);
  if (ensureWifi(Pending::Download)) downloadHit(pendingHit);
}

void CatalogSearchActivity::openKeyboard() {
  startActivityForResult(
      std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_SEARCH), query, MAX_QUERY_LENGTH),
      [this](const ActivityResult& result) {
        if (result.isCancelled) return;
        const auto* entered = std::get_if<KeyboardResult>(&result.data);
        if (entered) onQueryEntered(entered->text);
      });
}

void CatalogSearchActivity::onQueryEntered(const std::string& text) {
  query = text;
  runSearch();
  rowsDirty = true;
  moveSelectionTo(hits.empty() ? queryRow() : firstHitRow());
  requestUpdate();
}

// --- WiFi --------------------------------------------------------------------

bool CatalogSearchActivity::ensureWifi(const Pending action) {
  if (WiFi.status() == WL_CONNECTED) return true;

  state = State::WIFI_SELECTION;
  pendingAfterWifi = action;
  WiFi.mode(WIFI_STA);
  startActivityForResult(std::make_unique<WifiSelectionActivity>(renderer, mappedInput),
                         [this](const ActivityResult& result) { onWifiSelectionComplete(!result.isCancelled); });
  return false;
}

void CatalogSearchActivity::onWifiSelectionComplete(const bool connected) {
  if (!connected) {
    state = State::BROWSING;
    pendingAfterWifi = Pending::None;
    requestUpdate();
    return;
  }
  // The work blocks the loop task, so it is deferred to the next pass rather
  // than run inside the result handler.
  pending = pendingAfterWifi;
  pendingAfterWifi = Pending::None;
  state = State::BROWSING;
  requestUpdate();
}

void CatalogSearchActivity::runPending() {
  const Pending action = pending;
  pending = Pending::None;
  switch (action) {
    case Pending::CatalogAction:
      runCatalogAction();
      break;
    case Pending::Download:
      if (hasSymbolRow() && nav.selected == symbolRow()) {
        downloadBySymbol(query, "");
      } else {
        downloadHit(pendingHit);
      }
      break;
    case Pending::None:
      break;
  }
}

// --- Download ----------------------------------------------------------------

void CatalogSearchActivity::downloadHit(const size_t hitIndex) {
  if (hitIndex >= hits.size()) return;
  downloadBySymbol(hits[hitIndex].symbol, hits[hitIndex].issue);
}

void CatalogSearchActivity::downloadBySymbol(const std::string& symbol, const std::string& issue) {
  if (symbol.empty()) return;

  state = State::DOWNLOADING;
  statusMessage = tr(STR_DOWNLOADING);
  currentFilename = symbol;
  downloadProgress = 0;
  downloadTotal = 0;
  lastRenderedPercent = -1;
  lastProgressUpdateMs = 0;
  cancelDownload = false;
  goHomeAfterCancel = false;
  requestUpdateAndWait();

  publication::Request request;
  request.symbol = symbol.c_str();
  request.issue = issue.c_str();
  request.language = CatalogIndexStore::language();
  request.folder = downloadFolder;

  publication::Hooks hooks;
  hooks.ctx = this;
  hooks.onResolved = &CatalogSearchActivity::onDownloadResolved;
  hooks.onProgress = &CatalogSearchActivity::onDownloadProgress;
  hooks.cancelFlag = &cancelDownload;

  std::string destPath;
  const auto result = publication::download(request, hooks, destPath);

  if (result == publication::Result::Cancelled) {
    // Set by onDownloadProgress, which publication::download calls between body
    // chunks through a function pointer. cppcheck does not follow the indirect
    // call, so it reads the flag as never written.
    // cppcheck-suppress knownConditionTrueFalse
    if (goHomeAfterCancel) {
      onGoHome();
      return;
    }
    state = State::BROWSING;
    rowsDirty = true;
    requestUpdate();
    return;
  }
  if (result != publication::Result::Ok && result != publication::Result::AlreadyOnCard) {
    fail(publication::failureMessage(result));
    return;
  }

  state = State::COMPLETE;
  statusMessage = result == publication::Result::AlreadyOnCard ? tr(STR_ALREADY_DOWNLOADED) : tr(STR_DONE);
  requestUpdate();
}

void CatalogSearchActivity::onDownloadResolved(void* ctx, const char* filename) {
  static_cast<CatalogSearchActivity*>(ctx)->currentFilename = filename;
}

void CatalogSearchActivity::onDownloadProgress(void* ctx, const size_t downloaded, const size_t total) {
  auto* self = static_cast<CatalogSearchActivity*>(ctx);

  // The loop task is blocked for the whole transfer; pump input here so Back and
  // the home gesture still work.
  self->mappedInput.update();
  if (self->mappedInput.wasReleased(MappedInputManager::Button::Back)) self->cancelDownload = true;
  if (self->mappedInput.wasHomeGesture()) {
    self->cancelDownload = true;
    self->goHomeAfterCancel = true;
  }
  self->throttledProgressRepaint(downloaded, total);
}

void CatalogSearchActivity::onIndexProgress(void* ctx, const size_t downloaded, const size_t total) {
  auto* self = static_cast<CatalogSearchActivity*>(ctx);
  self->mappedInput.update();
  if (self->mappedInput.wasReleased(MappedInputManager::Button::Back)) self->cancelDownload = true;
  self->throttledProgressRepaint(downloaded, total);
}

void CatalogSearchActivity::throttledProgressRepaint(const size_t downloaded, const size_t total) {
  downloadProgress = downloaded;
  downloadTotal = total;

  const int percent = total > 0 ? static_cast<int>(static_cast<uint64_t>(downloaded) * 100 / total) : 0;
  const unsigned long now = millis();
  if (percent >= 100 || lastRenderedPercent < 0 || percent >= lastRenderedPercent + DOWNLOAD_PROGRESS_STEP_PERCENT ||
      now - lastProgressUpdateMs >= DOWNLOAD_PROGRESS_MIN_UPDATE_MS) {
    lastRenderedPercent = percent;
    lastProgressUpdateMs = now;
    requestUpdate(true);
  }
}

void CatalogSearchActivity::fail(const char* message) {
  state = State::ERROR;
  errorMessage = message ? message : tr(STR_DOWNLOAD_FAILED);
  requestUpdate();
}

// --- Input -------------------------------------------------------------------

bool CatalogSearchActivity::handleCustomInput() {
  if (pending != Pending::None) {
    runPending();
    return true;
  }

  switch (state) {
    case State::BROWSING:
      // The base list protocol owns this state.
      return false;
    case State::WIFI_SELECTION:
    case State::FETCHING_INDEX:
    case State::DOWNLOADING:
      return true;
    case State::COMPLETE:
    case State::ERROR: {
      int x = 0;
      int y = 0;
      if (mappedInput.wasPressed(MappedInputManager::Button::Back) ||
          mappedInput.wasPressed(MappedInputManager::Button::Confirm) || mappedInput.wasScreenTapped(x, y)) {
        {
          RenderLock lock(*this);
          state = State::BROWSING;
          rowsDirty = true;
        }
        requestUpdate();
      }
      return true;
    }
  }
  return true;
}

void CatalogSearchActivity::onBackButton() {
  if (state != State::BROWSING) return;
  finish();
}

// --- Rendering ---------------------------------------------------------------

// drawCenteredText centres on the screen but neither wraps nor truncates, so a
// string wider than the panel runs off BOTH edges -- it is centred, so the
// overflow is split between them. Translated sentences and CDN filenames both
// exceed 480px at UI_10 routinely, so nothing user-supplied reaches it raw.
void CatalogSearchActivity::drawMessageLine(const char* text, const int y, const EpdFontFamily::Style style) const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int width = renderer.getScreenWidth() - 2 * metrics.contentSidePadding;
  renderer.drawCenteredText(UI_10_FONT_ID, y, renderer.truncatedText(UI_10_FONT_ID, text, width, style).c_str(), true,
                            style);
}

// Wrapped and centred as a block. Used where the message is a whole sentence
// and the layout has the room, rather than a label beside a progress bar.
void CatalogSearchActivity::drawMessageBlock(const char* text, const int centreY) const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int width = renderer.getScreenWidth() - 2 * metrics.contentSidePadding;
  const int lineHeight = renderer.getLineHeight(UI_10_FONT_ID);
  const auto lines = renderer.wrappedText(UI_10_FONT_ID, text, width, 4, EpdFontFamily::BOLD);

  int y = centreY - (static_cast<int>(lines.size()) - 1) * lineHeight / 2;
  for (const auto& line : lines) {
    renderer.drawCenteredText(UI_10_FONT_ID, y, line.c_str(), true, EpdFontFamily::BOLD);
    y += lineHeight;
  }
}

void CatalogSearchActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  renderer.clearScreen();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_SEARCH));

  const auto lineHeight = renderer.getLineHeight(UI_10_FONT_ID);
  const auto centerY = (pageHeight - lineHeight) / 2;

  if (state == State::BROWSING || state == State::WIFI_SELECTION) {
    renderUi();
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  } else if (state == State::FETCHING_INDEX || state == State::DOWNLOADING) {
    drawMessageLine(statusMessage.c_str(), centerY - lineHeight, EpdFontFamily::REGULAR);
    if (!currentFilename.empty() && state == State::DOWNLOADING) {
      drawMessageLine(currentFilename.c_str(), centerY, EpdFontFamily::REGULAR);
    }
    const int percent =
        downloadTotal > 0 ? static_cast<int>(static_cast<uint64_t>(downloadProgress) * 100 / downloadTotal) : 0;
    GUI.drawProgressBar(renderer,
                        Rect{metrics.contentSidePadding, centerY + lineHeight + metrics.verticalSpacing,
                             pageWidth - metrics.contentSidePadding * 2, metrics.progressBarHeight},
                        percent, 100);
    const auto labels = mappedInput.mapLabels(tr(STR_CANCEL), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  } else if (state == State::COMPLETE) {
    drawMessageLine(statusMessage.c_str(), centerY - lineHeight, EpdFontFamily::BOLD);
    if (!currentFilename.empty()) {
      drawMessageLine(currentFilename.c_str(), centerY + metrics.verticalSpacing, EpdFontFamily::REGULAR);
    }
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  } else {
    drawMessageBlock(errorMessage.c_str(), centerY);
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  }

  renderer.displayBuffer();
  PostedMessage::drawNext(renderer);
}
