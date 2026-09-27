#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

#include "activities/Activity.h"
#include "components/UiAppHost.h"
#include "network/WifiSession.h"

// What the Bible tile opens when no Bible is on the card: asks first, then
// downloads the New World Translation in the publication language through
// publication::download, which also registers it so the launcher finds it.
//
//   Confirm --Download--> (WifiSelection) --> Resolving --> Downloading --> finish
//   Confirm --Choose a file--> file browser
//   WifiSelection cancelled, or a download failure --> Failed --Retry--> as Download
//
// Resolving has no Cancel button: the resolve is a fetchUrl that pumps no
// input, so a Cancel there would be visible but dead for up to a minute.
class BibleDownloadActivity final : public Activity, private UiAppHost {
 public:
  explicit BibleDownloadActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
  // Only while working: Confirm and Failed wait on the user indefinitely.
  bool preventAutoSleep() override { return state == State::Resolving || state == State::Downloading; }

 private:
  enum class State : uint8_t { Confirm, WifiSelection, Resolving, Downloading, Failed };

  static void rootScreen(UiScreen& screen, void* user);
  static void onStartEvent(const freeink::ui::ActionEvent& event, void* user);
  static void onChooseFileEvent(const freeink::ui::ActionEvent& event, void* user);
  static void onDismissEvent(const freeink::ui::ActionEvent& event, void* user);
  static void onCancelEvent(const freeink::ui::ActionEvent& event, void* user);

  void screenHeader(UiScreen& screen);
  void buildDialog(UiScreen& screen, const char* title, const char* message, const char* primaryLabel,
                   freeink::ui::ActionId primaryAction, const char* secondaryLabel,
                   freeink::ui::ActionId secondaryAction);
  void buildProgressScreen(UiScreen& screen);

  void startDownload();
  void enterResolving();
  void onWifiSelectionComplete(bool connected);
  void runDownload();
  void fail(const char* message);

  static void onDownloadPhase(void* ctx, const char* message);
  static void onDownloadResolved(void* ctx, const char* filename);
  static void onDownloadProgress(void* ctx, size_t downloaded, size_t total);

  State state = State::Confirm;
  // Set once the Resolving frame is requested, so the blocking download starts
  // from loop() after that frame rather than from the event that asked for it.
  bool downloadPending = false;
  bool startRequested = false;
  bool chooseFileRequested = false;
  bool dismissRequested = false;

  char promptMessage[192] = "";
  // Always a tr() string, which outlives the activity.
  const char* errorMessage = "";
  std::string statusMessage;
  std::string currentFilename;
  size_t downloadProgress = 0;
  size_t downloadTotal = 0;
  // Repaint throttle for the progress hook: a full e-ink refresh per chunk
  // would cost more than the transfer.
  int lastRenderedPercent = -1;
  unsigned long lastProgressUpdateMs = 0;

  // Read by HttpDownloader between chunks; set by Cancel, Back or the home
  // gesture, all pumped from the download's own progress hook.
  bool cancelDownload = false;
  bool goHomeAfterCancel = false;

  std::optional<WifiSession> wifiSession;
};
