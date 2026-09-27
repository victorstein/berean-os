#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>

#include "activities/UiListActivity.h"
#include "activities/network/MeetingWeekView.h"
#include "network/WolWeekScan.h"

struct MeetingWeekEntry;

// The week's two meeting publications as a week card: the Monday-Sunday range,
// a strip marking today and the user's meeting days, and one card per
// publication with its cover and how far through it the reader is. A card opens
// what is on the card or downloads what is not.
//
// This is deliberately not MeetingDownloadActivity with a list bolted on. That
// activity is a state machine -- wifi selection, resolve, download, result --
// and UiListActivity's own contract says a state machine should not derive from
// it. It stays the download pipeline; this screen is the library in front of it
// and hands off to it for the work.
//
// It stays a UiListActivity although it draws cards: the cards and Refresh are
// one ordered set of activatable items with one selection, and all of it fits
// one page, so the base's buttons, taps and render skeleton apply unchanged.
//
// Which issues a week references is only knowable from wol.jw.org, so the cards
// come from MeetingWeekCache. When the cache does not cover the current week
// the screen still paints first -- last week's cards, or an empty state -- and
// only then resolves. A blank screen for the length of a page scrape is worse
// than a stale one that refreshes itself.
class MeetingsActivity final : public UiListActivity {
 public:
  explicit MeetingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;
  void loop() override;

 private:
  struct Card {
    MeetingPub pub = MeetingPub::Watchtower;
    // "" when no week is known.
    std::string issue;
    // "" when the issue is not on the card.
    std::string path;
    std::string coverPath;
    // The thumbnail's measured size when coverUsable, else the placeholder box.
    // Never 0x0: bookCard fills and frames exactly this box.
    int16_t coverWidth = 0;
    int16_t coverHeight = 0;
    bool coverUsable = false;
    // Empty when the metadata cache does not load; no bar is drawn then.
    std::optional<int> percent;
    std::string title;
    std::string status;
  };

  // bookCard's cover painter is a plain function pointer; this is its context.
  struct CoverPaint {
    const GfxRenderer* renderer = nullptr;
    const Card* card = nullptr;
  };

  // Absolute rects, fixed for the visit. The cover thumbnail is generated at
  // exactly coverHeight, so nothing refresh() learns -- the clock, the meeting
  // day settings, a one-card week -- may move them.
  struct Layout {
    freeink::ui::Rect range{};
    freeink::ui::Rect strip{};
    freeink::ui::Rect legend{};
    std::array<freeink::ui::Rect, 2> cards{};
    freeink::ui::Rect refresh{};
    int16_t letterHeight = 0;
    int16_t dayHeight = 0;
    int16_t dotSize = 0;
    int16_t coverHeight = 0;
    int16_t placeholderWidth = 0;
    int16_t cardContentWidth = 0;
  };

  // The cards plus the refresh row beneath them.
  int listCount() const override { return cardCount_ + 1; }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  const char* headerTitle() const override;
  bool handleCustomInput() override;

  // What the range band, strip and legend show. Built off to the side and
  // swapped in whole, like the cards.
  struct WeekHeader {
    // False when no week can be dated: the range, strip and legend bands stay blank.
    bool shown = false;
    char rangeLine[96] = "";
    WeekStrip strip{};
    bool anyMeetingDay = false;
    char letters[7][8] = {};
  };

  void computeLayout();
  // Rebuilds the week and the cards from the week cache and the card. Runs on
  // every return from a download.
  void refresh();
  void buildWeekHeader(const IsoWeek* currentWeek, const MeetingWeekEntry* entry, const CivilDate* localToday,
                       WeekHeader& out) const;
  int buildCards(const MeetingWeekEntry* entry, std::array<Card, 2>& out) const;
  void fillCard(Card& card) const;

  void drawRange(UiScreen& screen) const;
  void drawStrip(UiScreen& screen) const;
  void drawLegend(UiScreen& screen) const;
  void drawCard(UiScreen& screen, int index);
  void drawRefresh(UiScreen& screen);
  static bool paintCover(freeink::ui::DrawTarget& target, freeink::ui::Rect rect,
                         const freeink::ui::BookCardProps& props, void* user);

  void startDownload(bool force);

  Layout layout_{};
  std::array<Card, 2> cards_{};
  int cardCount_ = 0;
  std::array<CoverPaint, 2> coverPaint_{};
  freeink::ui::ListItem refreshItem_{};

  WeekHeader week_{};

  // Set when the cache does not cover the current ISO week. Consumed by the
  // first loop() pass, after the cached cards have been forced onto the panel.
  bool resolvePending_ = false;
  // One automatic resolve per visit, whether or not it succeeded.
  bool resolveAttempted_ = false;
};
