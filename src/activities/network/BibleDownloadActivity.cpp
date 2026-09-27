#include "BibleDownloadActivity.h"

#include <Arduino.h>
#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>
#include <WiFi.h>

#include <cstdio>
#include <string_view>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "WifiSelectionActivity.h"
#include "activities/ActivityManager.h"
#include "activities/PostedMessage.h"
#include "activities/launcher/LauncherBible.h"
#include "components/UITheme.h"
#include "network/PublicationDownloader.h"

namespace fui = freeink::ui;

namespace {

constexpr const char* MODULE = "BIBLEDL";

constexpr fui::ActionId ACTION_START = 1;
constexpr fui::ActionId ACTION_CHOOSE_FILE = 2;
constexpr fui::ActionId ACTION_DISMISS = 3;
constexpr fui::ActionId ACTION_CANCEL = 4;

constexpr int DOWNLOAD_PROGRESS_STEP_PERCENT = 5;
constexpr unsigned long DOWNLOAD_PROGRESS_MIN_UPDATE_MS = 5000;
constexpr uint8_t DIALOG_MESSAGE_LINES = 3;

// Measured 2026-09-27: 14,945,282 B (S) and 15,652,378 B (E). The exact size
// only arrives with the resolve, which needs Wi-Fi the user has not agreed to yet.
constexpr unsigned NWT_APPROX_MEGABYTES = 15;

// A literal rather than BIBLE_SYMBOL.data(): Request takes a C string, and a
// string_view promises no terminator.
constexpr const char* NWT_SYMBOL = "nwt";
static_assert(std::string_view(NWT_SYMBOL) == BIBLE_SYMBOL);

}  // namespace

BibleDownloadActivity::BibleDownloadActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : Activity("BibleDownload", renderer, mappedInput), UiAppHost(renderer) {}

void BibleDownloadActivity::onEnter() {
  Activity::onEnter();
  wifiSession.emplace();

  state = State::Confirm;
  downloadPending = false;
  startRequested = false;
  chooseFileRequested = false;
  dismissRequested = false;
  errorMessage = "";
  statusMessage.clear();
  currentFilename.clear();

  const char* language = SETTINGS.publicationLanguage == CrossPointSettings::PUB_LANG_ENGLISH ? tr(STR_LANG_ENGLISH)
                                                                                               : tr(STR_LANG_SPANISH);
  snprintf(promptMessage, sizeof(promptMessage), tr(STR_BIBLE_DOWNLOAD_PROMPT), language, NWT_APPROX_MEGABYTES);

  resetUi();
  app.on(ACTION_START, &BibleDownloadActivity::onStartEvent, this);
  app.on(ACTION_CHOOSE_FILE, &BibleDownloadActivity::onChooseFileEvent, this);
  app.on(ACTION_DISMISS, &BibleDownloadActivity::onDismissEvent, this);
  app.on(ACTION_CANCEL, &BibleDownloadActivity::onCancelEvent, this);
  app.setScreen(&BibleDownloadActivity::rootScreen, this);
  requestUpdate();
}

void BibleDownloadActivity::onStartEvent(const fui::ActionEvent&, void* user) {
  auto* self = static_cast<BibleDownloadActivity*>(user);
  self->app.clearTapFlash();
  self->startRequested = true;
}

void BibleDownloadActivity::onChooseFileEvent(const fui::ActionEvent&, void* user) {
  auto* self = static_cast<BibleDownloadActivity*>(user);
  self->app.clearTapFlash();
  self->chooseFileRequested = true;
}

void BibleDownloadActivity::onDismissEvent(const fui::ActionEvent&, void* user) {
  auto* self = static_cast<BibleDownloadActivity*>(user);
  self->app.clearTapFlash();
  self->dismissRequested = true;
}

void BibleDownloadActivity::onCancelEvent(const fui::ActionEvent&, void* user) {
  auto* self = static_cast<BibleDownloadActivity*>(user);
  if (self->state != State::Downloading) return;
  self->app.clearTapFlash();
  self->cancelDownload = true;
}

void BibleDownloadActivity::loop() {
  if (state == State::WifiSelection) return;

  if (downloadPending) {
    downloadPending = false;
    runDownload();
    return;
  }

  if (state == State::Resolving || state == State::Downloading) return;

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    finish();
    return;
  }
  routeTouch(mappedInput);

  if (chooseFileRequested && state == State::Confirm) {
    chooseFileRequested = false;
    // Replaces the whole stack, launcher included -- the same place the tile's
    // no-Bible tap led before this screen existed.
    activityManager.goToFileBrowser();
    return;
  }
  if (dismissRequested) {
    dismissRequested = false;
    finish();
    return;
  }
  if (startRequested) {
    startRequested = false;
    startDownload();
  }
}

void BibleDownloadActivity::startDownload() {
  if (WiFi.status() == WL_CONNECTED) {
    enterResolving();
    return;
  }

  state = State::WifiSelection;
  statusMessage = tr(STR_CONNECTING);
  requestUpdate();
  startActivityForResult(std::make_unique<WifiSelectionActivity>(renderer, mappedInput, /*autoConnect=*/true,
                                                                 /*meetingPrefetch=*/false),
                         [this](const ActivityResult& result) { onWifiSelectionComplete(!result.isCancelled); });
}

// The picker cannot tell "no network in range" from "the user backed out", and
// either way there is no link: both land on a failure the user can retry.
void BibleDownloadActivity::onWifiSelectionComplete(const bool connected) {
  if (!connected) {
    fail(tr(STR_NO_WIFI_CONNECTION));
    return;
  }
  enterResolving();
}

void BibleDownloadActivity::enterResolving() {
  state = State::Resolving;
  statusMessage = tr(STR_CONNECTING);
  currentFilename.clear();
  downloadPending = true;
  requestUpdate();
}

void BibleDownloadActivity::fail(const char* message) {
  state = State::Failed;
  errorMessage = message;
  requestUpdate();
}

void BibleDownloadActivity::onDownloadPhase(void* ctx, const char* message) {
  auto* self = static_cast<BibleDownloadActivity*>(ctx);
  self->statusMessage = message;
  self->requestUpdateAndWait();
}

// The resolve is over once the filename is known; only now can a Cancel act.
void BibleDownloadActivity::onDownloadResolved(void* ctx, const char* filename) {
  auto* self = static_cast<BibleDownloadActivity*>(ctx);
  self->currentFilename = filename;
  self->statusMessage = tr(STR_DOWNLOADING);
  self->state = State::Downloading;
  self->requestUpdateAndWait();
}

void BibleDownloadActivity::onDownloadProgress(void* ctx, const size_t downloaded, const size_t total) {
  auto* self = static_cast<BibleDownloadActivity*>(ctx);
  self->downloadProgress = downloaded;
  self->downloadTotal = total;

  // The loop task is blocked for the whole transfer; pump input here so Cancel,
  // Back and the home gesture still work.
  self->mappedInput.update();
  if (self->mappedInput.wasReleased(MappedInputManager::Button::Back)) self->cancelDownload = true;
  if (self->mappedInput.wasHomeGesture()) {
    self->cancelDownload = true;
    self->goHomeAfterCancel = true;
  }
  self->routeTouch(self->mappedInput);

  const int percent = total > 0 ? static_cast<int>(static_cast<uint64_t>(downloaded) * 100 / total) : 0;
  const unsigned long now = millis();
  if (percent >= 100 || self->lastRenderedPercent < 0 ||
      percent >= self->lastRenderedPercent + DOWNLOAD_PROGRESS_STEP_PERCENT ||
      now - self->lastProgressUpdateMs >= DOWNLOAD_PROGRESS_MIN_UPDATE_MS) {
    self->lastRenderedPercent = percent;
    self->lastProgressUpdateMs = now;
    self->requestUpdate(true);
  }
}

void BibleDownloadActivity::runDownload() {
  // The Resolving frame has to land before the resolve blocks the loop task.
  requestUpdateAndWait();

  publication::Request request;
  request.symbol = NWT_SYMBOL;
  request.issue = "";
  request.language = CrossPointSettings::langWritten(SETTINGS.publicationLanguage);
  request.folder = publication::resolveDownloadFolder();

  publication::Hooks hooks;
  hooks.ctx = this;
  hooks.onPhase = &BibleDownloadActivity::onDownloadPhase;
  hooks.onResolved = &BibleDownloadActivity::onDownloadResolved;
  hooks.onProgress = &BibleDownloadActivity::onDownloadProgress;
  hooks.cancelFlag = &cancelDownload;

  downloadProgress = 0;
  downloadTotal = 0;
  lastRenderedPercent = -1;
  lastProgressUpdateMs = 0;
  cancelDownload = false;
  goHomeAfterCancel = false;

  std::string destPath;
  const auto result = publication::download(request, hooks, destPath);

  switch (result) {
    case publication::Result::Ok:
    case publication::Result::AlreadyOnCard:
      LOG_INF(MODULE, "Bible on the card: %s", destPath.c_str());
      finish();
      return;
    case publication::Result::Cancelled:
      PostedMessage::post(tr(STR_DOWNLOAD_CANCELLED));
      if (goHomeAfterCancel) {
        onGoHome();
      } else {
        finish();
      }
      return;
    case publication::Result::ChecksumMismatch:
      fail(publication::failureMessage(result));
      return;
    // NoMediaLink also covers a resolve fetch that never reached jw.org, and
    // both languages the setting offers do publish the NWT -- so for this
    // screen it means "could not download", not "not published".
    case publication::Result::NoMediaLink:
    case publication::Result::DownloadFailed:
    case publication::Result::OutOfMemory:
      fail(tr(STR_BIBLE_DOWNLOAD_FAILED_HINT));
      return;
  }
  fail(tr(STR_BIBLE_DOWNLOAD_FAILED_HINT));
}

void BibleDownloadActivity::rootScreen(UiScreen& screen, void* user) {
  auto* self = static_cast<BibleDownloadActivity*>(user);
  self->screenHeader(screen);
  switch (self->state) {
    case State::Confirm:
      self->buildDialog(screen, tr(STR_BIBLE_DOWNLOAD_TITLE), self->promptMessage, tr(STR_DOWNLOAD), ACTION_START,
                        tr(STR_CHOOSE_FILE), ACTION_CHOOSE_FILE);
      return;
    case State::Failed:
      self->buildDialog(screen, tr(STR_DOWNLOAD_FAILED), self->errorMessage, tr(STR_RETRY), ACTION_START,
                        tr(STR_CANCEL), ACTION_DISMISS);
      return;
    case State::WifiSelection:
    case State::Resolving:
    case State::Downloading:
      self->buildProgressScreen(screen);
      return;
  }
}

void BibleDownloadActivity::screenHeader(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  screen.takeBottom(static_cast<int16_t>(metrics.buttonHintsHeight));
  screen.spacer(static_cast<int16_t>(metrics.topPadding));
  fui::HeaderProps header;
  header.title = tr(STR_BIBLE);
  header.borderEdges = fui::EdgeBottom;
  screen.header(header);
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));
}

void BibleDownloadActivity::buildDialog(UiScreen& screen, const char* title, const char* message,
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
  const auto pairLeft = static_cast<int16_t>(area.x + (area.width - buttonWidth * 2 - gap) / 2);
  fui::ButtonProps secondary;
  secondary.label = secondaryLabel;
  secondary.action = secondaryAction;
  fui::ButtonProps primary;
  primary.label = primaryLabel;
  primary.action = primaryAction;
  screen.button(secondary, fui::Rect{pairLeft, area.y, buttonWidth, buttonHeight});
  screen.button(primary,
                fui::Rect{static_cast<int16_t>(pairLeft + buttonWidth + gap), area.y, buttonWidth, buttonHeight});
}

void BibleDownloadActivity::buildProgressScreen(UiScreen& screen) {
  const auto& theme = screen.theme();
  fui::TextStyle centered = theme.bodyText;
  centered.align = fui::TextAlign::Center;

  if (state != State::Downloading) {
    screen.centeredText(statusMessage.c_str(), centered);
    return;
  }

  const int16_t lineHeight = screen.target().lineHeight(centered.font);
  const int16_t gap = theme.spaceMd;
  const int16_t barHeight = 16;
  const int16_t buttonHeight = theme.rowHeight;
  const auto blockHeight = static_cast<int16_t>(lineHeight * 2 + barHeight + buttonHeight + gap * 3);
  const fui::Rect body = screen.body();
  if (body.height > blockHeight) screen.spacer(static_cast<int16_t>((body.height - blockHeight) / 2));

  screen.target().text(screen.takeTop(lineHeight, gap), statusMessage.c_str(), centered);
  screen.target().text(screen.takeTop(lineHeight, gap), currentFilename.c_str(), centered);

  const fui::Rect bar = screen.takeTop(barHeight, gap).inset(fui::Insets{0, 50, 0, 50});
  if (downloadTotal > 0) {
    fui::ProgressBarProps progress;
    progress.value = static_cast<int32_t>(downloadProgress);
    progress.max = static_cast<int32_t>(downloadTotal);
    progress.border = fui::Paint::solid(fui::Color::Black);
    progress.borderWidth = 1;
    fui::progressBar(screen.frame(), bar, progress);
  }

  const fui::Rect buttonArea = screen.takeTop(buttonHeight);
  const auto buttonWidth = static_cast<int16_t>(buttonArea.width / 3);
  fui::ButtonProps cancel;
  cancel.label = tr(STR_CANCEL);
  cancel.action = ACTION_CANCEL;
  screen.button(cancel, fui::Rect{static_cast<int16_t>(buttonArea.x + (buttonArea.width - buttonWidth) / 2),
                                  buttonArea.y, buttonWidth, buttonHeight});
}

void BibleDownloadActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const char* backLabel = "";
  if (state == State::Downloading) {
    backLabel = tr(STR_CANCEL);
  } else if (state == State::Confirm || state == State::Failed) {
    backLabel = tr(STR_BACK);
  }
  const MappedInputManager::Labels labels = mappedInput.mapLabels(backLabel, "", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderUi();
  renderer.displayBuffer();
  PostedMessage::drawNext(renderer);
}
