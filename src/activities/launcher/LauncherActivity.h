#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "Place.h"
#include "RecentBooksStore.h"
#include "activities/Activity.h"
#include "activities/launcher/HomeLayout.h"
#include "activities/launcher/HomeTargets.h"
#include "activities/network/MeetingWeekView.h"
#include "util/ButtonNavigator.h"

// bereanOS's home screen (issue #203, Direction B v2): the Bible's cover with
// Continue and Go to on its plate, the recent places, a verse from the user's
// tags, this week's meetings, and Tags, Search, Publications and Settings.
//
// A launcher is a screen you pass THROUGH, and on e-ink every pass costs a full
// panel refresh, so getting back to a place is one tap. There is no Bible
// reading progress anywhere here: the Bible is a reference you return to, not a
// book you finish. Live state that cannot drift, such as this week's workbook
// progress, stays.
class LauncherActivity final : public Activity {
 public:
  explicit LauncherActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, bool cleanInitialRefresh = false)
      : Activity("Launcher", renderer, mappedInput), cleanInitialRefresh(cleanInitialRefresh) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool isHomeActivity() const override { return true; }

 private:
  using Target = HomeTargets::Target;

  void computeLayout();
  // Resolved on entry and after a sub-screen returns: the Bible, its cover, the
  // places, this week's meetings, the date and whether the verse needs a scan.
  void resolveTargets();
  void resolveMeetings();
  HomeTargets::State targetState() const;
  const HomeLayout::Box* boxFor(Target target) const;
  void activate(Target target);
  void openReader(const HomeTargets::Route& route, Target target);
  void openBibleDownload();
  void openMeetings();
  void openPublications();
  void logMemory(const char* when) const;

  void drawHero() const;
  void drawButton(const HomeLayout::Box& box, const char* label, bool inverted, bool selected) const;
  void drawRecent() const;
  void drawVerse() const;
  void drawMeetings() const;
  void drawIconTile(const HomeLayout::Box& box, const uint8_t* icon, const char* label, bool selected) const;
  bool isSelected(Target target) const;

  ButtonNavigator buttonNavigator;
  HomeLayout::Layout layout{};
  Target selected = Target::Continue;

  std::string biblePath;
  std::string bibleCoverPath;
  bool hasPlace = false;
  Place newestPlace;
  Place recentPlaces[HomeLayout::RECENT_SLOTS];
  uint8_t recentCount = 0;
  char continueLabel[96] = {};
  char dateLine[48] = {};
  bool hasDateLine = false;

  // This week's meetings card. The strip is drawn only with a date; a day is
  // marked only with the local time as well (MeetingsActivity's rule).
  bool hasWeek = false;
  WeekStrip strip{};
  char stripLetters[7][8] = {};
  char rangeLine[64] = {};
  char percentLine[48] = {};

  // Set on entry when the verse has to be scanned; the loop paints first.
  bool versePending = false;

  // Wake-path flag, consumed by the first paint only -- see LauncherRefresh.h.
  const bool cleanInitialRefresh;
  bool firstRenderDone = false;
};
