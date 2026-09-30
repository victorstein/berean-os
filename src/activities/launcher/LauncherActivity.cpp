#include "LauncherActivity.h"

#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>
#include <esp_heap_caps.h>

#include <algorithm>
#include <cstdio>
#include <optional>

#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "MappedInputManager.h"
#include "PlacesStore.h"
#include "RecentBooksStore.h"
#include "activities/PostedMessage.h"
#include "activities/boot_sleep/StudySleepScreen.h"
#include "activities/catalog/PublicationsActivity.h"
#include "activities/launcher/BibleFinder.h"
#include "activities/launcher/HomeVerse.h"
#include "activities/launcher/LauncherBible.h"
#include "activities/launcher/LauncherRefresh.h"
#include "activities/network/BibleDownloadActivity.h"
#include "activities/network/MeetingsActivity.h"
#include "activities/reader/ReaderEntryIntent.h"
#include "components/CoverBand.h"
#include "components/UITheme.h"
#include "components/icons/bookmark.h"
#include "components/icons/folder.h"
#include "components/icons/library.h"
#include "components/icons/search.h"
#include "components/icons/settings2.h"
#include "components/themes/BaseTheme.h"
#include "fontIds.h"
#include "network/MeetingLibrary.h"
#include "network/MeetingWeekCache.h"
#include "network/MeetingWeekTable.h"
#include "study/PubKeyRegistry.h"
#include "util/BookCacheUtils.h"
#include "util/CardBooks.h"
#include "util/LocalDate.h"
#include "util/PlacesDoc.h"
#include "util/WeekdayNames.h"

namespace {

constexpr const char* MODULE = "LAUNCH";

using HomeLayout::Box;
using HomeLayout::PAD;
using HomeLayout::RADIUS;

Rect toRect(const Box& box) { return Rect{box.x, box.y, box.width, box.height}; }

// Text vertically centred in a box, left-aligned at its x plus `inset`.
void drawTextIn(const GfxRenderer& renderer, const int font, const Box& box, const char* text, const int inset,
                const bool black, const EpdFontFamily::Style style = EpdFontFamily::REGULAR) {
  const std::string fitted = renderer.truncatedText(font, text, box.width - 2 * inset, style);
  const int y = box.y + (box.height - renderer.getLineHeight(font)) / 2;
  renderer.drawText(font, box.x + inset, y, fitted.c_str(), black, style);
}

void drawCentredIn(const GfxRenderer& renderer, const int font, const Box& box, const int y, const char* text,
                   const bool black, const EpdFontFamily::Style style = EpdFontFamily::REGULAR) {
  const std::string fitted = renderer.truncatedText(font, text, box.width - 2 * PAD, style);
  const int width = renderer.getTextWidth(font, fitted.c_str(), style);
  renderer.drawText(font, box.x + (box.width - width) / 2, y, fitted.c_str(), black, style);
}

}  // namespace

void LauncherActivity::onEnter() {
  Activity::onEnter();
  // Layout first: the cover thumbnail is generated at exactly the hero's size.
  computeLayout();
  resolveTargets();
  logMemory("on entry");
  // With the verse pending, loop()'s requestUpdateAndWait() is the first and only
  // paint before the scan, so no render runs while the scan measures text.
  if (!versePending) requestUpdate();
}

void LauncherActivity::computeLayout() {
  int marginTop = 0;
  int marginRight = 0;
  int marginBottom = 0;
  int marginLeft = 0;
  renderer.getOrientedViewableTRBL(&marginTop, &marginRight, &marginBottom, &marginLeft);
  const HomeLayout::LineHeights lines{renderer.getLineHeight(SMALL_FONT_ID), renderer.getLineHeight(UI_10_FONT_ID),
                                      renderer.getLineHeight(NOTOSERIF_12_FONT_ID),
                                      renderer.getLineHeight(NOTOSERIF_14_FONT_ID)};
  layout = HomeLayout::compute(renderer.getScreenWidth(), renderer.getScreenHeight(),
                               HomeLayout::Insets{marginTop, marginRight, marginBottom, marginLeft},
                               UITheme::getInstance().getMetrics(), lines);
}

void LauncherActivity::resolveTargets() {
  RECENT_BOOKS.loadFromFile();

  // The registry knows every Buscar download and every Bible the reader has
  // opened; the card scan finds a copy that arrived under the CDN's own name.
  // Recents runs last because its title match also catches a non-Bible titled
  // "New World", and it covers a Bible that was opened without ever being
  // registered.
  biblePath.clear();
  bibleCoverPath.clear();
  const auto foundBible = BibleFinder::find();
  LOG_INF(MODULE, "Bible: %s (%s)", foundBible ? foundBible->path.c_str() : "(none found)",
          foundBible ? bibleLookupName(foundBible->by) : "-");
  if (foundBible) {
    biblePath = foundBible->path;
    bool generatedAny = false;
    bibleCoverPath = CoverBand::thumbPathFor(biblePath, layout.hero.width, layout.hero.height, generatedAny);
    if (generatedAny) LOG_INF(MODULE, "Generated a missing cover thumbnail");
    // The sleep screen paints this too, and it runs while the device is shutting
    // down -- far too late to search for the Bible or open it.
    if (APP_STATE.bibleCoverPath != bibleCoverPath) {
      APP_STATE.bibleCoverPath = bibleCoverPath;
      APP_STATE.saveToFileAtomic();
    }
  }

  const std::vector<Place>& places = PLACES.getPlaces();
  hasPlace = !places.empty();
  recentCount = 0;
  if (hasPlace) {
    newestPlace = places.front();
    // Places are one per (book, chapter), so skipping the newest's chapter skips
    // exactly the newest.
    recentCount =
        static_cast<uint8_t>(PlacesDoc::pickRecent(places, newestPlace.unit, recentPlaces, HomeLayout::RECENT_SLOTS));
    snprintf(continueLabel, sizeof(continueLabel), tr(STR_HOME_CONTINUE_AT), newestPlace.reference.c_str());
  } else {
    snprintf(continueLabel, sizeof(continueLabel), "%s", tr(STR_CONTINUE_READING));
  }

  hasDateLine = study_sleep_screen::formatDate(dateLine, sizeof(dateLine));
  resolveMeetings();

  // No Bible, nothing to open: the card stays empty and nothing is scanned.
  versePending = !biblePath.empty() && !HOME_VERSE.isCurrent();
}

// This week, from the clock: the strip, its range and the workbook's progress.
// The strip needs a date; a day is marked only with the local time as well,
// because a UTC date would mark the wrong day for part of every day.
void LauncherActivity::resolveMeetings() {
  hasWeek = false;
  rangeLine[0] = '\0';
  percentLine[0] = '\0';

  CivilDate today;
  bool todayIsLocal = false;
  IsoWeek week;
  CivilDate monday;
  if (!readLocalDate(today, todayIsLocal) || !isoWeekFromUtcDate(today.year, today.month, today.day, week) ||
      !mondayOfIsoWeek(week, monday)) {
    return;
  }
  hasWeek = true;
  strip =
      buildWeekStrip(monday, todayIsLocal ? &today : nullptr, SETTINGS.midweekMeetingDay, SETTINGS.weekendMeetingDay);
  for (size_t i = 0; i < strip.size(); ++i) {
    copyInitial(I18N.get(WEEKDAY_NAME_IDS[i]), stripLetters[i], sizeof(stripLetters[i]));
  }
  if (!formatWeekRange(monday, tr(STR_MEETING_WEEK_RANGE), tr(STR_MEETING_WEEK_RANGE_SPAN), tr(STR_MONTHS_LONG),
                       rangeLine, sizeof(rangeLine))) {
    LOG_ERR(MODULE, "Week range for %u/%02u did not fit", static_cast<unsigned>(week.year),
            static_cast<unsigned>(week.week));
  }

  MeetingWeekTable table;
  MeetingWeekCache::load(table);
  const MeetingWeekEntry* entry = table.find(meetingWeekKey(week));
  if (entry == nullptr || entry->workbook.empty()) return;
  const std::string workbookPath = MeetingLibrary::findPublication(MeetingPub::Workbook, entry->workbook);
  if (workbookPath.empty()) return;
  const std::optional<int> percent = readBookProgressPercent(workbookPath);
  if (percent) snprintf(percentLine, sizeof(percentLine), tr(STR_MEETING_PROGRESS), *percent);
}

HomeTargets::State LauncherActivity::targetState() const {
  return HomeTargets::State{!biblePath.empty(), hasPlace, recentCount, !versePending && HOME_VERSE.hasPick()};
}

const HomeLayout::Box* LauncherActivity::boxFor(const Target target) const {
  switch (target) {
    case Target::Continue:
      // With no Bible the one button spans the row.
      return biblePath.empty() ? &layout.buttonRow : &layout.continueButton;
    case Target::GoTo:
      return &layout.goToButton;
    case Target::Recent0:
      return &layout.recent[0];
    case Target::Recent1:
      return &layout.recent[1];
    case Target::Recent2:
      return &layout.recent[2];
    case Target::Verse:
      return &layout.verseCard;
    case Target::Meetings:
      return &layout.meetings;
    case Target::Tags:
      return &layout.icons[0];
    case Target::Search:
      return &layout.icons[1];
    case Target::Publications:
      return &layout.icons[2];
    case Target::Settings:
      return &layout.icons[3];
    case Target::COUNT:
      break;
  }
  return nullptr;
}

bool LauncherActivity::isSelected(const Target target) const { return selected == target; }

void LauncherActivity::logMemory(const char* when) const {
  LOG_INF(MODULE, "Memory %s: internal free %u (largest %u), PSRAM free %u", when,
          static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
          static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)),
          static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)));
}

void LauncherActivity::drawButton(const Box& box, const char* label, const bool inverted, const bool selected) const {
  if (inverted) {
    renderer.fillRoundedRect(box.x, box.y, box.width, box.height, RADIUS / 2, Color::Black);
    // Selection on a filled button is a white ring inside the fill.
    if (selected) renderer.drawRoundedRect(box.x + 3, box.y + 3, box.width - 6, box.height - 6, 2, RADIUS / 2, false);
  } else {
    renderer.fillRoundedRect(box.x, box.y, box.width, box.height, RADIUS / 2, Color::White);
    renderer.drawRoundedRect(box.x, box.y, box.width, box.height, selected ? 3 : 1, RADIUS / 2, true);
  }
  drawCentredIn(renderer, UI_10_FONT_ID, box, box.y + (box.height - renderer.getLineHeight(UI_10_FONT_ID)) / 2, label,
                !inverted, EpdFontFamily::BOLD);
}

void LauncherActivity::drawHero() const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const CoverBand::Style style{CoverBandGeometry::BOOK_TITLE_BAND, RADIUS, layout.plate.height};
  const bool drawn = !bibleCoverPath.empty() && CoverBand::draw(renderer, bibleCoverPath, toRect(layout.hero), style);
  const char* subtitle = hasDateLine ? dateLine : nullptr;
  if (drawn) {
    GUI.drawHeader(renderer, toRect(layout.plateHeader), tr(STR_BIBLE), subtitle);
  } else {
    // CoverBand has left the band as paper, so the header never sits on dither.
    GUI.drawHeader(renderer, Rect{0, metrics.topPadding, renderer.getScreenWidth(), metrics.headerHeight},
                   tr(STR_BIBLE), subtitle);
  }
  // Outlined only around a drawn cover: the fallback header sits across the band's top rows, as Masthead's does.
  if (drawn) {
    renderer.drawRoundedRect(layout.hero.x, layout.hero.y, layout.hero.width, layout.hero.height, 1, RADIUS, true);
  }

  if (biblePath.empty()) {
    drawButton(layout.buttonRow, tr(STR_DOWNLOAD), /*inverted=*/true, isSelected(Target::Continue));
    return;
  }
  drawButton(layout.continueButton, continueLabel, /*inverted=*/true, isSelected(Target::Continue));
  drawButton(layout.goToButton, tr(STR_GO_TO), /*inverted=*/false, isSelected(Target::GoTo));
}

void LauncherActivity::drawRecent() const {
  renderer.drawText(SMALL_FONT_ID, layout.recentLabel.x + PAD, layout.recentLabel.y, tr(STR_RECENT), true,
                    EpdFontFamily::BOLD);
  if (biblePath.empty()) return;
  for (uint8_t i = 0; i < recentCount; ++i) {
    const Box& row = layout.recent[i];
    const auto target = static_cast<Target>(static_cast<uint8_t>(Target::Recent0) + i);
    drawTextIn(renderer, UI_10_FONT_ID, row, recentPlaces[i].reference.c_str(), PAD, true);
    const int rule = isSelected(target) ? 3 : 1;
    renderer.fillRect(row.x, row.y + row.height - rule, row.width, rule, true);
  }
}

void LauncherActivity::drawVerse() const {
  const Box& card = layout.verseCard;
  renderer.drawRoundedRect(card.x, card.y, card.width, card.height, isSelected(Target::Verse) ? 3 : 1, RADIUS, true);
  renderer.drawText(SMALL_FONT_ID, layout.verseLabel.x, layout.verseLabel.y, tr(STR_FROM_YOUR_TAGS), true,
                    EpdFontFamily::BOLD);
  // Pending: label only, filled once the loop's scan lands. No Bible: nothing to open.
  if (versePending || biblePath.empty()) return;

  if (!HOME_VERSE.hasPick()) {
    const char* hint =
        HOME_VERSE.empty() == home_verse::Empty::TooLong ? tr(STR_HOME_TAGS_TOO_LONG) : tr(STR_HOME_TAGS_EMPTY);
    drawTextIn(renderer, SMALL_FONT_ID, layout.verseText, hint, 0, true);
    return;
  }

  const home_verse::Pick& pick = HOME_VERSE.pick();
  const int font = HomeVerse::FONT_IDS[pick.rung];
  const EpdFontFamily::Style style = HomeVerse::FONT_STYLES[pick.rung];
  const int lineHeight = renderer.getLineHeight(font);
  for (uint8_t i = 0; i < pick.lineCount; ++i) {
    renderer.drawText(font, layout.verseText.x, layout.verseText.y + i * lineHeight, pick.line(i), true, style);
  }
  const int referenceWidth = renderer.getTextWidth(SMALL_FONT_ID, pick.reference, EpdFontFamily::BOLD);
  renderer.drawText(SMALL_FONT_ID, layout.verseReference.x + layout.verseReference.width - referenceWidth,
                    layout.verseReference.y, pick.reference, true, EpdFontFamily::BOLD);
}

void LauncherActivity::drawMeetings() const {
  const Box& card = layout.meetings;
  renderer.drawRoundedRect(card.x, card.y, card.width, card.height, isSelected(Target::Meetings) ? 3 : 1, RADIUS, true);
  renderer.drawIcon(LibraryIcon, layout.meetingsIcon.x, layout.meetingsIcon.y, HomeLayout::ICON);
  renderer.drawText(UI_10_FONT_ID, layout.meetingsTitle.x, layout.meetingsTitle.y, tr(STR_MEETINGS), true,
                    EpdFontFamily::BOLD);
  if (percentLine[0] != '\0') {
    drawTextIn(renderer, SMALL_FONT_ID, layout.meetingsPercent, percentLine, 0, true);
  }
  if (!hasWeek) return;

  if (rangeLine[0] != '\0') drawTextIn(renderer, SMALL_FONT_ID, layout.meetingsRange, rangeLine, 0, true);

  const int letterHeight = renderer.getLineHeight(SMALL_FONT_ID);
  const int dayHeight = renderer.getLineHeight(UI_10_FONT_ID);
  for (size_t i = 0; i < strip.size(); ++i) {
    const WeekStripCell& cell = strip[i];
    const Box cellBox{layout.strip.x + static_cast<int>(i) * HomeLayout::STRIP_CELL, layout.strip.y,
                      HomeLayout::STRIP_CELL, letterHeight + dayHeight};
    const bool black = !cell.today;
    if (cell.today) {
      renderer.fillRoundedRect(cellBox.x + 1, cellBox.y, cellBox.width - 2, cellBox.height, RADIUS / 2, Color::Black);
    }
    drawCentredIn(renderer, SMALL_FONT_ID, Box{cellBox.x - PAD, cellBox.y, cellBox.width + 2 * PAD, letterHeight},
                  cellBox.y, stripLetters[i], black);
    char day[4];
    snprintf(day, sizeof(day), "%u", static_cast<unsigned>(cell.day));
    drawCentredIn(renderer, UI_10_FONT_ID, Box{cellBox.x - PAD, cellBox.y, cellBox.width + 2 * PAD, dayHeight},
                  cellBox.y + letterHeight, day, black);
    if (cell.meeting) {
      const int dotX = cellBox.x + (cellBox.width - HomeLayout::STRIP_DOT) / 2;
      const int dotY = cellBox.y + cellBox.height + (PAD - HomeLayout::STRIP_DOT) / 2;
      renderer.fillRoundedRect(dotX, dotY, HomeLayout::STRIP_DOT, HomeLayout::STRIP_DOT, HomeLayout::STRIP_DOT / 2,
                               Color::Black);
    }
  }
}

void LauncherActivity::drawIconTile(const Box& box, const uint8_t* icon, const char* label, const bool selected) const {
  renderer.drawRoundedRect(box.x, box.y, box.width, box.height, selected ? 3 : 1, RADIUS, true);
  renderer.drawIcon(icon, box.x + (box.width - HomeLayout::ICON) / 2, box.y + PAD, HomeLayout::ICON);
  drawCentredIn(renderer, SMALL_FONT_ID, box, box.y + 2 * PAD + HomeLayout::ICON, label, true, EpdFontFamily::BOLD);
}

void LauncherActivity::render(RenderLock&&) {
  renderer.clearScreen();
  drawHero();
  drawRecent();
  drawVerse();
  drawMeetings();
  drawIconTile(layout.icons[0], BookmarkIcon, tr(STR_TAGS), isSelected(Target::Tags));
  drawIconTile(layout.icons[1], SearchIcon, tr(STR_SEARCH), isSelected(Target::Search));
  drawIconTile(layout.icons[2], FolderIcon, tr(STR_PUBLICATIONS), isSelected(Target::Publications));
  drawIconTile(layout.icons[3], Settings2Icon, tr(STR_SETTINGS_TITLE), isSelected(Target::Settings));

  const bool cleanPaint = launcherNeedsCleanPaint(cleanInitialRefresh, firstRenderDone);
  const auto mode = cleanPaint ? HalDisplay::HALF_REFRESH : HalDisplay::FAST_REFRESH;
  LOG_DBG(MODULE, "Paint: clean=%d firstPaint=%d mode=%s", cleanInitialRefresh ? 1 : 0, firstRenderDone ? 0 : 1,
          cleanPaint ? "HALF" : "FAST");
  renderer.displayBuffer(mode);
  PostedMessage::drawNext(renderer);
  firstRenderDone = true;
}

void LauncherActivity::activate(const Target target) {
  const HomeTargets::Route route = HomeTargets::route(target, targetState());
  switch (route.action) {
    case HomeTargets::Action::None:
      return;
    case HomeTargets::Action::OpenReader:
      openReader(route, target);
      return;
    case HomeTargets::Action::DownloadBible:
      openBibleDownload();
      return;
    case HomeTargets::Action::OpenMeetings:
      openMeetings();
      return;
    case HomeTargets::Action::OpenPublications:
      openPublications();
      return;
    case HomeTargets::Action::OpenSettings:
      activityManager.goToSettings();
      return;
  }
}

void LauncherActivity::openReader(const HomeTargets::Route& route, const Target target) {
  ReaderEntryIntent intent = ReaderEntryIntent::of(route.intent);
  if (route.intent == ReaderEntryIntent::Kind::OpenAt) {
    if (target == Target::Continue) {
      intent = ReaderEntryIntent::openAt(newestPlace);
    } else if (target == Target::Verse) {
      // The passage's own spine is the hint StudyStore::locate passes too: only a
      // Verse unit can be found without it.
      intent.unit = HOME_VERSE.pick().start;
      intent.spineHint = HOME_VERSE.pick().spine;
    } else {
      const uint8_t slot = static_cast<uint8_t>(target) - static_cast<uint8_t>(Target::Recent0);
      intent = ReaderEntryIntent::openAt(recentPlaces[slot]);
    }
  }
  activityManager.goToReader(biblePath, route.allowFastInitialRefresh, intent);
}

void LauncherActivity::openBibleDownload() {
  auto download = makeUniqueNoThrow<BibleDownloadActivity>(renderer, mappedInput);
  if (!download) {
    LOG_ERR(MODULE, "OOM: Bible download activity");
    return;
  }
  // Back to Home rather than into the Bible: Home then shows what arrived, and a
  // first open's indexing popup does not follow straight on from a screen the
  // user just watched download.
  startActivityForResult(std::move(download), [this](const ActivityResult&) { resolveTargets(); });
}

void LauncherActivity::openMeetings() {
  startActivityForResult(std::make_unique<MeetingsActivity>(renderer, mappedInput),
                         [this](const ActivityResult&) { resolveMeetings(); });
}

void LauncherActivity::openPublications() {
  // A download changes what the Bible and its cover can offer.
  startActivityForResult(std::make_unique<PublicationsActivity>(renderer, mappedInput),
                         [this](const ActivityResult&) { resolveTargets(); });
}

void LauncherActivity::loop() {
  if (versePending) {
    // Home goes on the panel before the scan, so the first tap never waits on it.
    requestUpdateAndWait();
    HOME_VERSE.ensure(renderer, layout.verseText.width, layout.verseText.height);
    versePending = false;
    logMemory("after the verse");
    requestUpdate();
    return;
  }

  // No mappedInput.update() here. main.cpp's loop already ticked HalGPIO this
  // frame, and InputManager::update() clears every one-shot edge it latched
  // (InputManager.cpp:434-442) -- a second tick wipes the tap and the button
  // release before this function can read them.
  const HomeTargets::State state = targetState();
  int touchX = 0;
  int touchY = 0;
  if (mappedInput.wasScreenTapped(touchX, touchY)) {
    for (uint8_t i = 0; i < static_cast<uint8_t>(Target::COUNT); ++i) {
      const auto target = static_cast<Target>(i);
      const Box* box = boxFor(target);
      if (box == nullptr || !HomeTargets::isActive(target, state) || !HomeLayout::contains(*box, touchX, touchY)) {
        continue;
      }
      selected = target;
      activate(target);
      return;
    }
    return;
  }

  Target ring[static_cast<size_t>(Target::COUNT)];
  const int count = static_cast<int>(HomeTargets::ring(state, ring, static_cast<size_t>(Target::COUNT)));
  if (count == 0) return;
  int position = 0;
  for (int i = 0; i < count; ++i) {
    if (ring[i] == selected) position = i;
  }
  buttonNavigator.onNextRelease([this, &ring, position, count] {
    selected = ring[ButtonNavigator::nextIndex(position, count)];
    requestUpdate();
  });
  buttonNavigator.onPreviousRelease([this, &ring, position, count] {
    selected = ring[ButtonNavigator::previousIndex(position, count)];
    requestUpdate();
  });

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) activate(ring[position]);
}
