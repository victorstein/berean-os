#pragma once

#include <array>
#include <string>

#include "activities/UiListActivity.h"
#include "network/WolWeekScan.h"

// The week's two meeting publications, each offering to open what is on the
// card or download what is not.
//
// This is deliberately not MeetingDownloadActivity with a list bolted on. That
// activity is a state machine -- wifi selection, resolve, download, result --
// and UiListActivity's own contract says a state machine should not derive from
// it. It stays the download pipeline; this screen is the library in front of it
// and hands off to it for the work.
//
// Which issues a week references is only knowable from wol.jw.org, so the rows
// come from MeetingWeekCache. When the cache does not cover the current week
// the screen still paints first -- last week's rows, or an empty state -- and
// only then resolves. A blank screen for the length of a page scrape is worse
// than a stale one that refreshes itself.
class MeetingsActivity final : public UiListActivity {
 public:
  explicit MeetingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;
  void loop() override;

 private:
  struct Row {
    MeetingPub pub = MeetingPub::Watchtower;
    // "" when the week does not carry this publication, which is the Memorial
    // week's workbook.
    std::string issue;
    // "" when the issue is not on the card.
    std::string path;
    std::string label;
    std::string subtitle;
  };

  int listCount() const override { return static_cast<int>(rows_.size()); }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  const char* headerTitle() const override;

  // Rebuilds the rows from the week cache and the card. Cheap enough to run on
  // every return from a download.
  void refresh();
  void startDownload();

  std::array<Row, 2> rows_;
  std::array<freeink::ui::ListItem, 2> rowItems_{};

  // Set when the cache does not cover the current ISO week. Consumed by the
  // first loop() pass, after the cached rows have been forced onto the panel.
  bool resolvePending_ = false;
  // One automatic resolve per visit, whether or not it succeeded.
  bool resolveAttempted_ = false;
};
