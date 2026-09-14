#include "MeetingDownloadActivity.h"

#include <Arduino.h>
#include <GfxRenderer.h>
#include <HalClock.h>
#include <I18n.h>
#include <Logging.h>
#include <WiFi.h>

#include <cstdio>

#include "MappedInputManager.h"
#include "SilentRestart.h"
#include "WifiSelectionActivity.h"
#include "components/UITheme.h"
#include "network/HttpDownloader.h"
#include "network/PublicationDownloader.h"

namespace fui = freeink::ui;

namespace {
constexpr fui::ActionId ACTION_CANCEL = 1;
constexpr int DOWNLOAD_PROGRESS_STEP_PERCENT = 5;
constexpr unsigned long DOWNLOAD_PROGRESS_MIN_UPDATE_MS = 5000;

// Language the EPUBs are fetched in, used both as the API's langwritten value
// and as the key to look for under "files" in its response. The week -> issue
// mapping is language-independent, so the meetings page stays English.
constexpr const char* DOWNLOAD_LANGUAGE = "S";

constexpr MeetingPub PUBLICATION_ORDER[] = {MeetingPub::Watchtower, MeetingPub::Workbook};
}  // namespace

MeetingDownloadActivity::MeetingDownloadActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : Activity("MeetingDownload", renderer, mappedInput), UiAppHost(renderer) {}

void MeetingDownloadActivity::onEnter() {
  Activity::onEnter();

  state = State::RESOLVING;
  statusMessage = tr(STR_RESOLVING_WEEK);
  errorMessage.clear();
  currentFilename.clear();
  phaseIndex = 0;
  phaseCount = 0;
  workbookUnavailable = false;
  downloadProgress = 0;
  downloadTotal = 0;
  cancelDownload = false;
  goHomeAfterCancel = false;
  sequencePending = false;

  resetUi();
  app.on(ACTION_CANCEL, &MeetingDownloadActivity::onCancelEvent, this);
  app.setScreen(&MeetingDownloadActivity::rootScreen, this);

  if (WiFi.status() == WL_CONNECTED) {
    sequencePending = true;
    requestUpdate();
    return;
  }

  state = State::WIFI_SELECTION;
  statusMessage = tr(STR_CONNECTING);
  requestUpdate();
  startActivityForResult(std::make_unique<WifiSelectionActivity>(renderer, mappedInput),
                         [this](const ActivityResult& result) { onWifiSelectionComplete(!result.isCancelled); });
}

void MeetingDownloadActivity::onExit() {
  Activity::onExit();

  if (WiFi.getMode() != WIFI_MODE_NULL) {
    WiFi.disconnect(false);
    delay(30);
    silentRestart();
  }
}

void MeetingDownloadActivity::onWifiSelectionComplete(const bool connected) {
  if (!connected) {
    finish();
    return;
  }
  state = State::RESOLVING;
  statusMessage = tr(STR_RESOLVING_WEEK);
  sequencePending = true;
  requestUpdate();
}

void MeetingDownloadActivity::onCancelEvent(const fui::ActionEvent&, void* user) {
  auto* self = static_cast<MeetingDownloadActivity*>(user);
  if (self->state != State::DOWNLOADING) return;
  self->app.clearTapFlash();
  self->cancelDownload = true;
}

void MeetingDownloadActivity::loop() {
  if (state == State::WIFI_SELECTION) return;

  if (sequencePending) {
    sequencePending = false;
    runSequence();
    return;
  }

  if (state == State::DOWNLOADING) return;

  int tapX = 0;
  int tapY = 0;
  if (mappedInput.wasReleased(MappedInputManager::Button::Back) || mappedInput.wasScreenTapped(tapX, tapY)) {
    finish();
  }
}

void MeetingDownloadActivity::fail(const char* message) {
  state = State::FAILED;
  errorMessage = message;
  requestUpdate();
}

void MeetingDownloadActivity::reportPhase(const char* message) {
  state = State::RESOLVING;
  statusMessage = message;
  requestUpdateAndWait();
}

void MeetingDownloadActivity::runSequence() {
  // The first paint has to land before the resolve phases block the loop task
  // for up to a minute each; fetchUrl takes neither a progress nor a cancel hook.
  requestUpdateAndWait();

  HalClock::Date today{};
  IsoWeek week;
  if (!halClock.getDate(today) || !isoWeekFromUtcDate(today.year, today.month, today.day, week)) {
    LOG_ERR("MEET", "RTC has no usable date");
    fail(tr(STR_CLOCK_NOT_SET));
    return;
  }
  LOG_INF("MEET", "Device date %04u-%02u-%02u -> ISO week %u/%02u", static_cast<unsigned>(today.year),
          static_cast<unsigned>(today.month), static_cast<unsigned>(today.day), static_cast<unsigned>(week.year),
          static_cast<unsigned>(week.week));

  downloadFolder = publication::resolveDownloadFolder();

  WolWeekScanner scanner;
  if (!scanWeek(week, scanner)) return;

  phaseCount = scanner.count();
  workbookUnavailable = !scanner.has(MeetingPub::Workbook);

  for (const MeetingPub pub : PUBLICATION_ORDER) {
    if (!scanner.has(pub)) continue;
    ++phaseIndex;
    if (!downloadPublication(pub, scanner.issue(pub))) return;
  }

  state = State::FINISHED;
  requestUpdate();
}

bool MeetingDownloadActivity::scanWeek(const IsoWeek& week, WolWeekScanner& scanner) {
  const std::string url = meetingsPageUrl(week);
  size_t bytes = 0;
  const bool fetched = HttpDownloader::fetchUrl(url, [&scanner, &bytes](const uint8_t* data, const size_t len) {
    bytes += len;
    scanner.feed(reinterpret_cast<const char*>(data), len);
    // Returning false is reported as FILE_ERROR, which is indistinguishable from
    // a transport failure, so the whole body is consumed even once both links
    // have been recovered.
    return true;
  });

  // The page is HTML, not an API: log the byte count so a markup change is
  // diagnosable from the serial log rather than just "not found".
  LOG_INF("MEET", "Week page %s: %zu bytes, %d publication(s)", url.c_str(), bytes, scanner.count());

  if (!fetched) {
    fail(tr(STR_DOWNLOAD_FAILED));
    return false;
  }
  if (scanner.count() == 0) {
    fail(tr(STR_NO_PUBLICATIONS_FOUND));
    return false;
  }
  return true;
}

void MeetingDownloadActivity::onDownloadPhase(void* ctx, const char* message) {
  static_cast<MeetingDownloadActivity*>(ctx)->reportPhase(message);
}

void MeetingDownloadActivity::onDownloadResolved(void* ctx, const char* filename) {
  auto* self = static_cast<MeetingDownloadActivity*>(ctx);
  self->currentFilename = filename;
}

void MeetingDownloadActivity::onDownloadProgress(void* ctx, const size_t downloaded, const size_t total) {
  auto* self = static_cast<MeetingDownloadActivity*>(ctx);
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

bool MeetingDownloadActivity::downloadPublication(const MeetingPub pub, const char* issue) {
  state = State::RESOLVING;
  statusMessage = tr(STR_RESOLVING_WEEK);
  currentFilename.clear();
  requestUpdateAndWait();

  publication::Request request;
  request.symbol = pub == MeetingPub::Watchtower ? "w" : "mwb";
  request.issue = issue;
  request.language = DOWNLOAD_LANGUAGE;
  request.folder = downloadFolder;

  publication::Hooks hooks;
  hooks.ctx = this;
  hooks.onPhase = &MeetingDownloadActivity::onDownloadPhase;
  hooks.onResolved = &MeetingDownloadActivity::onDownloadResolved;
  hooks.onProgress = &MeetingDownloadActivity::onDownloadProgress;
  hooks.cancelFlag = &cancelDownload;

  // The resolve happens inside publication::download, so the DOWNLOADING screen
  // is armed first and onDownloadProgress repaints into it once bytes arrive.
  state = State::DOWNLOADING;
  statusMessage = tr(STR_DOWNLOADING);
  downloadProgress = 0;
  downloadTotal = 0;
  lastRenderedPercent = -1;
  lastProgressUpdateMs = 0;
  requestUpdateAndWait();

  std::string destPath;
  const auto result = publication::download(request, hooks, destPath);

  if (result == publication::Result::Cancelled) {
    if (goHomeAfterCancel) {
      onGoHome();
    } else {
      finish();
    }
    return false;
  }
  if (result == publication::Result::AlreadyOnCard) {
    reportPhase(tr(STR_ALREADY_DOWNLOADED));
    return true;
  }
  if (result != publication::Result::Ok) {
    fail(publication::failureMessage(result));
    return false;
  }
  return true;
}
void MeetingDownloadActivity::rootScreen(UiScreen& screen, void* user) {
  auto* self = static_cast<MeetingDownloadActivity*>(user);
  self->screenHeader(screen);
  if (self->state == State::FINISHED || self->state == State::FAILED) {
    self->buildResultScreen(screen);
  } else {
    self->buildProgressScreen(screen);
  }
}

void MeetingDownloadActivity::screenHeader(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  screen.takeBottom(static_cast<int16_t>(metrics.buttonHintsHeight));
  screen.spacer(static_cast<int16_t>(metrics.topPadding));
  fui::HeaderProps header;
  header.title = tr(STR_MEETING_PUBLICATIONS);
  header.borderEdges = fui::EdgeBottom;
  screen.header(header);
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));
}

void MeetingDownloadActivity::buildProgressScreen(UiScreen& screen) {
  const auto& theme = screen.theme();
  fui::TextStyle centered = theme.bodyText;
  centered.align = fui::TextAlign::Center;
  const int16_t lineHeight = screen.target().lineHeight(centered.font);
  const int16_t gap = theme.spaceMd;
  const int16_t barHeight = 16;
  const int16_t buttonHeight = theme.rowHeight;
  const bool downloading = state == State::DOWNLOADING;

  if (!downloading) {
    screen.centeredText(statusMessage.c_str(), centered);
    return;
  }

  char phase[32] = "";
  if (phaseCount > 0) snprintf(phase, sizeof(phase), "%d / %d", phaseIndex, phaseCount);

  const int16_t blockHeight = static_cast<int16_t>(lineHeight * 3 + barHeight + buttonHeight + gap * 4);
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
  screen.target().text(screen.takeTop(lineHeight, gap), phase, centered);

  const fui::Rect buttonArea = screen.takeTop(buttonHeight);
  const auto buttonWidth = static_cast<int16_t>(buttonArea.width / 3);
  fui::ButtonProps cancel;
  cancel.label = tr(STR_CANCEL);
  cancel.action = ACTION_CANCEL;
  screen.button(cancel, fui::Rect{static_cast<int16_t>(buttonArea.x + (buttonArea.width - buttonWidth) / 2),
                                  buttonArea.y, buttonWidth, buttonHeight});
}

void MeetingDownloadActivity::buildResultScreen(UiScreen& screen) {
  const auto& theme = screen.theme();
  fui::TextStyle centered = theme.bodyText;
  centered.align = fui::TextAlign::Center;
  const int16_t lineHeight = screen.target().lineHeight(centered.font);
  const int16_t gap = theme.spaceMd;

  const bool failed = state == State::FAILED;
  // A missing workbook is an informational note beside the completed downloads,
  // not a failure.
  const bool withNote = !failed && workbookUnavailable;
  const int16_t blockHeight = static_cast<int16_t>(lineHeight * (withNote ? 2 : 1) + (withNote ? gap : 0));
  const fui::Rect body = screen.body();
  if (body.height > blockHeight) screen.spacer(static_cast<int16_t>((body.height - blockHeight) / 2));

  screen.target().text(screen.takeTop(lineHeight, gap), failed ? errorMessage.c_str() : tr(STR_DONE), centered);
  if (withNote) screen.target().text(screen.takeTop(lineHeight), tr(STR_WORKBOOK_UNAVAILABLE), centered);
}

void MeetingDownloadActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const MappedInputManager::Labels labels = state == State::DOWNLOADING
                                                ? mappedInput.mapLabels(tr(STR_CANCEL), "", "", "")
                                                : mappedInput.mapLabels(tr(STR_BACK), "", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderUi();
  renderer.displayBuffer();
}
