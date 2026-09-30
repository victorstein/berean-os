#include "MeetingsActivity.h"

#include <I18n.h>
#include <Logging.h>
#include <esp_heap_caps.h>

#include <algorithm>
#include <cstdio>
#include <vector>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "activities/network/MeetingDownloadActivity.h"
#include "components/CoverBandGeometry.h"
#include "components/Masthead.h"
#include "components/UIScale.h"
#include "components/UITheme.h"
#include "network/MeetingLibrary.h"
#include "network/MeetingWeekCache.h"
#include "network/MeetingWeekTable.h"
#include "util/BookCacheUtils.h"
#include "util/CoverThumb.h"
#include "util/LocalDate.h"
#include "util/WeekdayNames.h"

namespace fui = freeink::ui;

namespace {

constexpr const char* MODULE = "MEETINGS";

// A card without a cover gets a box this wide against the cover height: the top
// of the 0.6-0.75 range covers run to, so it lines up with the widest real one.
constexpr float PLACEHOLDER_COVER_ASPECT = 0.75f;
// A cover wider than this share of the card leaves the title too little room.
constexpr int MAX_COVER_SHARE_DIVISOR = 2;
constexpr int COVER_CORNER_RADIUS = 4;
constexpr uint8_t TODAY_RADIUS = 4;

const char* nameFor(const MeetingPub pub) {
  return pub == MeetingPub::Watchtower ? tr(STR_MEETING_WATCHTOWER) : tr(STR_MEETING_WORKBOOK);
}

fui::Rect rectOf(const int x, const int y, const int width, const int height) {
  return fui::Rect{static_cast<int16_t>(x), static_cast<int16_t>(y), static_cast<int16_t>(width),
                   static_cast<int16_t>(height)};
}

}  // namespace

MeetingsActivity::MeetingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : UiListActivity("Meetings", renderer, mappedInput) {}

const char* MeetingsActivity::headerTitle() const { return tr(STR_MEETINGS); }

void MeetingsActivity::drawChrome() {}

void MeetingsActivity::drawFooter() {
  UiListActivity::drawFooter();
  Masthead::draw(renderer, mastheadCover_, CoverBandGeometry::MAGAZINE_MASTHEAD_BAND, headerTitle());
}

void MeetingsActivity::onEnter() {
  UiListActivity::onEnter();
  computeLayout();
  refresh();
  // Entered from the launcher's cover tiles: one clean paint over them.
  halfRefreshPending.store(true);
  LOG_INF(MODULE, "Memory on entry: internal free %u (largest %u), PSRAM free %u",
          static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
          static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)),
          static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)));
}

void MeetingsActivity::computeLayout() {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  const int gap = metrics.verticalSpacing;
  const int left = safe.x + metrics.contentSidePadding;
  const int width = safe.width - 2 * metrics.contentSidePadding;
  // Always under the masthead band, cover or not: the card thumbnails are sized
  // from this layout and must not change size mid-visit.
  const int top = Masthead::contentTop(renderer) + gap;
  const int bottom = safe.y + safe.height - metrics.buttonHintsHeight;

  // The fonts the list and the theme draw with, so the Refresh band matches the row list() lays out.
  const UIScaleSpec fonts = uiScaleSpec();
  const int bodyLineHeight = renderer.getLineHeight(fonts.bodyFontId);
  const int smallLineHeight = renderer.getLineHeight(fonts.smallFontId);
  layout_.letterHeight = static_cast<int16_t>(smallLineHeight);
  layout_.dayHeight = static_cast<int16_t>(bodyLineHeight);
  layout_.dotSize = static_cast<int16_t>(std::max(4, smallLineHeight / 4));

  // Every band is reserved whether or not refresh() finds anything to put in
  // it, so the card height -- and the thumbnail size -- never follows the clock
  // or the meeting-day settings.
  int y = top;
  layout_.range = rectOf(left, y, width, bodyLineHeight);
  y += bodyLineHeight + gap;
  const int stripHeight = smallLineHeight + bodyLineHeight + 2 * layout_.dotSize;
  layout_.strip = rectOf(left, y, width, stripHeight);
  y += stripHeight + gap;
  layout_.legend = rectOf(left, y, width, smallLineHeight);
  y += smallLineHeight + gap;

  // list() grows a subtitle row to both fonts' line heights and drops a row
  // taller than its band; Classic's 50 px metric is shorter than that.
  const int refreshHeight = std::max(metrics.listWithSubtitleRowHeight, bodyLineHeight + smallLineHeight);
  layout_.refresh = rectOf(safe.x, bottom - refreshHeight, safe.width, refreshHeight);

  // Sized for two cards even when the week carries one, so the Memorial week
  // does not ask for a second thumbnail size.
  const int cardsBottom = bottom - refreshHeight - gap;
  const int cardHeight = std::max(0, (cardsBottom - y - gap) / 2);
  layout_.cards[0] = rectOf(left, y, width, cardHeight);
  layout_.cards[1] = rectOf(left, y + cardHeight + gap, width, cardHeight);

  const fui::BookCardProps cardDefaults;
  layout_.coverHeight =
      static_cast<int16_t>(std::max(0, cardHeight - cardDefaults.padding.top - cardDefaults.padding.bottom));
  layout_.placeholderWidth = static_cast<int16_t>(layout_.coverHeight * PLACEHOLDER_COVER_ASPECT);
  layout_.cardContentWidth = static_cast<int16_t>(width - cardDefaults.padding.left - cardDefaults.padding.right);
}

void MeetingsActivity::refresh() {
  CivilDate today;
  bool todayIsLocal = false;
  const bool haveDate = readLocalDate(today, todayIsLocal);
  IsoWeek week;
  const bool haveWeek = haveDate && isoWeekFromUtcDate(today.year, today.month, today.day, week);

  MeetingWeekTable table;
  MeetingWeekCache::load(table);

  const MeetingWeekEntry* entry = nullptr;
  if (haveWeek) {
    const std::string key = meetingWeekKey(week);
    entry = table.find(key);
    // At most one automatic resolve per visit. refresh() also runs when the
    // download returns, and a resolve that failed -- no wifi, nothing on the
    // page -- leaves the week exactly as absent as it was, so without this the
    // screen would relaunch the download forever.
    resolvePending_ = entry == nullptr && !resolveAttempted_;
    if (entry == nullptr) entry = table.newest();
  } else {
    // Without the clock there is no current week to be stale against, so a
    // resolve could only guess at which week to ask for.
    LOG_ERR(MODULE, "No usable date; showing the newest week held");
    entry = table.newest();
  }

  // All the SD work -- cover thumbnails, possibly book.bin -- happens before the
  // lock. A render queued while the download was on screen can run meanwhile,
  // and it must only ever see a whole old screen or a whole new one.
  WeekHeader header;
  buildWeekHeader(haveWeek ? &week : nullptr, entry, haveDate && todayIsLocal ? &today : nullptr, header);
  std::array<Card, 2> cards{};
  const int cardCount = buildCards(entry, cards);
  std::vector<std::string> coverCandidates;
  coverCandidates.reserve(cards.size());
  for (int i = 0; i < cardCount; ++i) {
    if (!cards[static_cast<size_t>(i)].path.empty()) coverCandidates.push_back(cards[static_cast<size_t>(i)].path);
  }
  std::string mastheadCover = Masthead::pickCover(renderer, coverCandidates);

  RenderLock lock(*this);
  week_ = header;
  cards_ = std::move(cards);
  mastheadCover_ = std::move(mastheadCover);
  cardCount_ = cardCount;
  // A resolve can turn three items into two; a selection past the end would
  // leave nothing highlighted and Confirm doing nothing.
  auto& selection = activeNav();
  const int last = listCount() - 1;
  if (selection.selected > last) selection.selected = last;
  if (selection.selected < 0) selection.selected = 0;
}

void MeetingsActivity::buildWeekHeader(const IsoWeek* currentWeek, const MeetingWeekEntry* entry,
                                       const CivilDate* localToday, WeekHeader& out) const {
  out = WeekHeader{};

  // The header dates the week whose publications are on the cards: a stale
  // entry is dated from its own key, never labelled with this week's dates.
  IsoWeek shown;
  if (entry != nullptr) {
    if (!isoWeekFromKey(entry->key, shown)) {
      LOG_ERR(MODULE, "Week key %s does not parse; no dates shown", entry->key.c_str());
      return;
    }
  } else if (currentWeek != nullptr) {
    shown = *currentWeek;
  } else {
    return;
  }

  CivilDate monday;
  if (!mondayOfIsoWeek(shown, monday)) {
    LOG_ERR(MODULE, "No Monday for week %u/%02u", static_cast<unsigned>(shown.year), static_cast<unsigned>(shown.week));
    return;
  }
  if (!formatWeekRange(monday, tr(STR_MEETING_WEEK_RANGE), tr(STR_MEETING_WEEK_RANGE_SPAN), tr(STR_MONTHS_LONG),
                       out.rangeLine, sizeof(out.rangeLine))) {
    LOG_ERR(MODULE, "Week range for %u/%02u did not fit", static_cast<unsigned>(shown.year),
            static_cast<unsigned>(shown.week));
  }

  // Null when the clock's time could not be read: a UTC date would mark the
  // wrong day for part of every day, so no day is marked instead.
  out.strip = buildWeekStrip(monday, localToday, SETTINGS.midweekMeetingDay, SETTINGS.weekendMeetingDay);
  out.anyMeetingDay =
      std::any_of(out.strip.begin(), out.strip.end(), [](const WeekStripCell& cell) { return cell.meeting; });
  for (size_t i = 0; i < out.strip.size(); ++i) {
    copyInitial(I18N.get(WEEKDAY_NAME_IDS[i]), out.letters[i], sizeof(out.letters[i]));
  }
  out.shown = true;
}

int MeetingsActivity::buildCards(const MeetingWeekEntry* entry, std::array<Card, 2>& out) const {
  int count = 0;
  for (const MeetingPub pub : {MeetingPub::Watchtower, MeetingPub::Workbook}) {
    std::string issue;
    if (entry != nullptr) {
      issue = pub == MeetingPub::Watchtower ? entry->watchtower : entry->workbook;
      // The Memorial week carries a Watchtower and no workbook: that card is
      // simply absent.
      if (issue.empty()) continue;
    }
    Card& card = out[static_cast<size_t>(count)];
    ++count;
    card = Card{};
    card.pub = pub;
    card.issue = std::move(issue);
    fillCard(card);
  }
  return count;
}

void MeetingsActivity::fillCard(Card& card) const {
  card.title = nameFor(card.pub);
  if (!card.issue.empty()) card.title += "  " + card.issue;
  card.path = MeetingLibrary::findPublication(card.pub, card.issue);
  card.coverWidth = layout_.placeholderWidth;
  card.coverHeight = layout_.coverHeight;

  if (card.path.empty()) {
    // With no week known, tapping the card is what starts a resolve.
    card.status = card.issue.empty() ? tr(STR_MEETING_WEEK_UNKNOWN) : tr(STR_DOWNLOAD);
    return;
  }

  bool generated = false;
  card.coverPath = CoverThumb::pathFor(card.path, layout_.coverHeight, generated);
  if (generated) LOG_INF(MODULE, "Generated a card-sized cover for %s", card.path.c_str());
  int width = 0;
  int height = 0;
  if (!card.coverPath.empty() && !CoverThumb::sizeOf(card.coverPath, width, height)) {
    LOG_DBG(MODULE, "Cover header unreadable: %s; using the placeholder", card.coverPath.c_str());
  } else if (!card.coverPath.empty()) {
    // The converters scale to cover their target box and keep the whole image,
    // so the width follows the cover's own aspect. Taller than the card (a cover
    // narrower than 0.6) or too wide to leave the title room: placeholder.
    const int maxWidth = layout_.cardContentWidth / MAX_COVER_SHARE_DIVISOR;
    if (height <= layout_.coverHeight && width <= maxWidth) {
      card.coverWidth = static_cast<int16_t>(width);
      card.coverHeight = static_cast<int16_t>(height);
      card.coverUsable = true;
    } else {
      LOG_DBG(MODULE, "Cover %dx%d does not fit %dx%d; using the placeholder", width, height, maxWidth,
              static_cast<int>(layout_.coverHeight));
    }
  }

  card.percent = readBookProgressPercent(card.path);
  if (card.percent) {
    char line[48];
    snprintf(line, sizeof(line), tr(STR_MEETING_PROGRESS), *card.percent);
    card.status = line;
  } else {
    card.status = tr(STR_OPEN);
  }
}

void MeetingsActivity::loop() {
  if (resolvePending_) {
    resolvePending_ = false;
    // Force the cached cards onto the panel before handing off. The resolve
    // fetches and scrapes a page with no progress or cancel hook, so whatever
    // is on screen when it starts is what the user looks at until it returns.
    requestUpdateAndWait();
    startDownload(/*force=*/false);
    return;
  }
  UiListActivity::loop();
}

bool MeetingsActivity::handleCustomInput() {
  // Everything fits one page, so a vertical swipe has nothing to scroll; the
  // base would move an unused viewport and pay a full refresh for it.
  const auto swipe = mappedInput.wasSwipe();
  return swipe == MappedInputManager::SwipeDir::Up || swipe == MappedInputManager::SwipeDir::Down;
}

void MeetingsActivity::buildScreen(UiScreen& screen) {
  // The content area is exactly the Refresh band, so the list lands where
  // computeLayout put it; everything else is drawn into Layout's own rects.
  const fui::Rect& band = layout_.refresh;
  screen.setContentMargin(fui::Insets{band.y, static_cast<int16_t>(renderer.getScreenWidth() - band.right()),
                                      static_cast<int16_t>(renderer.getScreenHeight() - band.bottom()), band.x});
  drawRange(screen);
  drawStrip(screen);
  drawLegend(screen);
  for (int i = 0; i < cardCount_; ++i) drawCard(screen, i);
  drawRefresh(screen);
}

void MeetingsActivity::drawRange(UiScreen& screen) const {
  if (!week_.shown || week_.rangeLine[0] == '\0') return;
  screen.target().text(layout_.range, week_.rangeLine, screen.theme().bodyText);
}

void MeetingsActivity::drawStrip(UiScreen& screen) const {
  if (!week_.shown) return;
  const fui::Rect& band = layout_.strip;
  const auto cellWidth = static_cast<int16_t>(band.width / static_cast<int>(week_.strip.size()));
  for (size_t i = 0; i < week_.strip.size(); ++i) {
    const WeekStripCell& cell = week_.strip[i];
    const auto x = static_cast<int16_t>(band.x + cellWidth * static_cast<int>(i));
    const fui::Rect letterRect{x, band.y, cellWidth, layout_.letterHeight};
    const fui::Rect dayRect{x, static_cast<int16_t>(band.y + layout_.letterHeight), cellWidth, layout_.dayHeight};

    fui::TextStyle letterStyle = screen.theme().smallText;
    letterStyle.align = fui::TextAlign::Center;
    fui::TextStyle dayStyle = screen.theme().bodyText;
    dayStyle.align = fui::TextAlign::Center;
    if (cell.today) {
      const fui::Rect highlight{static_cast<int16_t>(x + 1), band.y, static_cast<int16_t>(cellWidth - 2),
                                static_cast<int16_t>(layout_.letterHeight + layout_.dayHeight)};
      screen.target().fill(highlight, fui::Paint::solid(fui::Color::Black), TODAY_RADIUS);
      letterStyle.color = fui::Color::White;
      dayStyle.color = fui::Color::White;
    }
    screen.target().text(letterRect, week_.letters[i], letterStyle);
    char day[4];
    snprintf(day, sizeof(day), "%u", static_cast<unsigned>(cell.day));
    screen.target().text(dayRect, day, dayStyle);

    if (cell.meeting) {
      const fui::Rect dot{static_cast<int16_t>(x + (cellWidth - layout_.dotSize) / 2),
                          static_cast<int16_t>(dayRect.bottom() + layout_.dotSize / 2), layout_.dotSize,
                          layout_.dotSize};
      screen.target().fill(dot, fui::Paint::solid(fui::Color::Black), static_cast<uint8_t>(layout_.dotSize / 2));
    }
  }
}

void MeetingsActivity::drawLegend(UiScreen& screen) const {
  if (!week_.shown || !week_.anyMeetingDay) return;
  const fui::Rect& band = layout_.legend;
  const int16_t dotSize = layout_.dotSize;
  const fui::Rect dot{band.x, static_cast<int16_t>(band.y + (band.height - dotSize) / 2), dotSize, dotSize};
  screen.target().fill(dot, fui::Paint::solid(fui::Color::Black), static_cast<uint8_t>(dotSize / 2));
  const auto textX = static_cast<int16_t>(band.x + dotSize + screen.theme().spaceSm);
  screen.target().text(fui::Rect{textX, band.y, static_cast<int16_t>(band.right() - textX), band.height},
                       tr(STR_MEETING_DAY_LEGEND), screen.theme().smallText);
}

void MeetingsActivity::drawCard(UiScreen& screen, const int index) {
  const auto slot = static_cast<size_t>(index);
  const Card& card = cards_[slot];
  coverPaint_[slot] = CoverPaint{&renderer, &card};

  fui::BookCardProps props;
  props.title = card.title.c_str();
  props.meta = card.status.c_str();
  props.titleText = screen.theme().bodyText;
  props.titleText.maxLines = 2;
  props.metaText = screen.theme().smallText;
  props.coverSize = fui::Size{card.coverWidth, card.coverHeight};
  props.coverPainter = &MeetingsActivity::paintCover;
  props.coverPainterUserData = &coverPaint_[slot];
  // progressMax 0 is what hides the bar; bookCard draws one for any positive max.
  props.progress = card.percent.value_or(0);
  props.progressMax = card.percent ? 100 : 0;
  props.progressHeight = screen.theme().progressHeight;
  props.action = ACTION_ROW;
  props.value = static_cast<int16_t>(index);
  props.state = activeNav().selected == index ? fui::StateSelected : fui::StateNormal;
  // Frame the cover rather than invert the card: the bar's fill is fixed black
  // and would vanish into an inverted card.
  props.selectionIndicator = fui::BookCardSelectionIndicator::CoverFrame;
  fui::bookCard(screen.frame(), layout_.cards[slot], props);
}

bool MeetingsActivity::paintCover(fui::DrawTarget&, const fui::Rect rect, const fui::BookCardProps&, void* user) {
  const auto* paint = static_cast<const CoverPaint*>(user);
  if (paint == nullptr || paint->renderer == nullptr || paint->card == nullptr || !paint->card->coverUsable) {
    return false;
  }
  return CoverThumb::drawNative(*paint->renderer, paint->card->coverPath, rect.x, rect.y, rect.width, rect.height,
                                COVER_CORNER_RADIUS) > 0;
}

void MeetingsActivity::drawRefresh(UiScreen& screen) {
  refreshItem_ = fui::ListItem{};
  refreshItem_.label = tr(STR_MEETING_REFRESH);
  refreshItem_.subtitle = tr(STR_MEETING_REFRESH_HINT);
  refreshItem_.actionValue = static_cast<int16_t>(cardCount_);

  fui::ListProps props;
  props.items = &refreshItem_;
  props.count = 1;
  props.selectedIndex = static_cast<int16_t>(activeNav().selected == cardCount_ ? 0 : -1);
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;
  // Exactly the band computeLayout sized for this row, so list() cannot grow it
  // past the band and drop it.
  props.rowHeight = layout_.refresh.height;
  props.subtitleText = screen.theme().smallText;
  props.subtitleText.maxLines = 1;
  screen.list(props);
}

void MeetingsActivity::activateIndex(const int index) {
  if (index < 0 || index > cardCount_) return;

  if (index == cardCount_) {
    startDownload(/*force=*/true);
    return;
  }

  const Card& card = cards_[static_cast<size_t>(index)];
  if (!card.path.empty()) {
    activityManager.goToReader(card.path);
    return;
  }
  // Nothing to open and nothing known to fetch: the week itself is what is
  // missing, so resolving is the only useful thing this card can do.
  startDownload(/*force=*/false);
}

void MeetingsActivity::startDownload(const bool force) {
  resolveAttempted_ = true;
  startActivityForResult(std::make_unique<MeetingDownloadActivity>(renderer, mappedInput, force),
                         [this](const ActivityResult&) {
                           refresh();
                           requestUpdate();
                         });
}
