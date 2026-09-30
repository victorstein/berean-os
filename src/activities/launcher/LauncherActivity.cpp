#include "LauncherActivity.h"

#include <Epub.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>
#include <SdPaths.h>
#include <Utf8.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <optional>

#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "MappedInputManager.h"
#include "RecentBooksStore.h"
#include "StudyStore/PubKey.h"
#include "activities/PostedMessage.h"
#include "activities/catalog/PublicationsActivity.h"
#include "activities/launcher/LauncherBible.h"
#include "activities/launcher/LauncherRefresh.h"
#include "activities/network/BibleDownloadActivity.h"
#include "activities/network/MeetingsActivity.h"
#include "components/CoverBand.h"
#include "components/UITheme.h"
#include "components/icons/book.h"
#include "components/icons/library.h"
#include "components/icons/search.h"
#include "components/icons/settings2.h"
#include "components/themes/BaseTheme.h"
#include "fontIds.h"
#include "network/MeetingFilename.h"
#include "network/MeetingLibrary.h"
#include "network/MeetingWeekCache.h"
#include "study/PubKeyRegistry.h"
#include "util/CardBooks.h"
#include "util/CoverThumb.h"
#include "util/LocalDate.h"

namespace {

constexpr const char* MODULE = "LAUNCH";

constexpr int TILE_GAP = 10;
constexpr int TILE_PADDING = 8;
constexpr int TILE_ICON_SIZE = 32;
constexpr int TILE_RADIUS = 8;
// Below this a cover is a smudge and an icon is cramped, so the tile drops its
// art and centres the label instead.
constexpr int MIN_ART_HEIGHT = TILE_ICON_SIZE + 4;
// The Bible is the centre of the device, so its tile is taller than the pair
// beneath it rather than merely wider.
constexpr int BIBLE_TILE_WEIGHT = 2;

}  // namespace

void LauncherActivity::onEnter() {
  Activity::onEnter();
  // Layout first: a cover thumbnail is generated at exactly the height it will
  // be drawn at, so resolveTargets needs the tile geometry to ask for.
  computeLayout();
  resolveTargets();
  requestUpdate();
}

void LauncherActivity::resolveTargets() {
  RECENT_BOOKS.loadFromFile();
  bool generatedAny = false;
  const auto& recents = RECENT_BOOKS.getBooks();
  hasResume = !recents.empty();
  if (hasResume) {
    resumePath = recents[0].path;
    resumeTitle = utf8SafeSummary(recents[0].title, 48);
  }

  // The registry knows every Buscar download and every Bible the reader has
  // opened; the card scan finds a copy that arrived under the CDN's own name.
  // Recents runs last because its title match also catches a non-Bible titled
  // "New World", and it covers a Bible that was opened without ever being
  // registered.
  biblePath.clear();
  bibleSubtitle = tr(STR_BIBLE_SUBTITLE_NONE);
  BibleLookup foundBy = BibleLookup::Registry;
  auto foundBible = resolveBible([&](const BibleLookup step) -> std::optional<std::string> {
    foundBy = step;
    switch (step) {
      case BibleLookup::Registry:
        return PubKeyRegistry::findBySymbol({BIBLE_SYMBOL});
      case BibleLookup::CardScan:
        return findBibleOnCard();
      case BibleLookup::Recents:
        return findBibleInRecents(recents);
    }
    return std::nullopt;
  });
  LOG_INF(MODULE, "Bible: %s (%s)", foundBible ? foundBible->c_str() : "(none found)",
          foundBible ? bibleLookupName(foundBy) : "-");
  if (foundBible) {
    biblePath = std::move(*foundBible);
    bibleSubtitle = bibleTitleFor(biblePath, recents);
    const TileRect& bibleTile = rects[static_cast<size_t>(Tile::Bible)];
    bibleCoverPath = CoverBand::thumbPathFor(biblePath, bibleTile.w, bibleTile.h, generatedAny);
    // The sleep screen paints this too, and it runs while the device is shutting
    // down -- far too late to search for the Bible or open it.
    if (APP_STATE.bibleCoverPath != bibleCoverPath) {
      APP_STATE.bibleCoverPath = bibleCoverPath;
      APP_STATE.saveToFileAtomic();
    }
  }

  // The meeting tile shows THIS WEEK's publication when the week cache knows
  // which issue that is, preferring the Watchtower. Falling back to "whichever
  // meeting publication is on the card" is what this used to do on its own, and
  // it happily showed a months-old issue.
  //
  // Neither fallback is redundant. The registry only knows downloads made since
  // it existed; the card scan reads the dated filename, which is all that
  // survives for a download older than that. Recents is no use to either: it
  // holds books that have been OPENED, and a publication downloaded and not yet
  // read is precisely what this tile exists to advertise.
  auto meetingPath = thisWeeksMeetingPublication();
  if (!meetingPath) meetingPath = PubKeyRegistry::findBySymbol({"w", "mwb"});
  if (!meetingPath) meetingPath = findMeetingPublicationOnCard();
  LOG_INF(MODULE, "Meeting publication: %s", meetingPath ? meetingPath->c_str() : "(none found)");
  if (meetingPath) {
    const TileRect& meetingsTile = rects[static_cast<size_t>(Tile::Meetings)];
    meetingsCoverPath = CoverBand::thumbPathFor(*meetingPath, meetingsTile.w, meetingsTile.h, generatedAny);
    const auto opened =
        std::find_if(recents.begin(), recents.end(), [&](const RecentBook& book) { return book.path == *meetingPath; });
    if (opened != recents.end()) meetingsSubtitle = utf8SafeSummary(opened->title, 30);
  }

  // Building a thumbnail leaves the popup's pixels in the framebuffer, and the
  // launcher paints over a cleared screen anyway -- but the panel still shows
  // the popup until the first render lands, which is the point of drawing it.
  if (generatedAny) LOG_INF(MODULE, "Generated a missing cover thumbnail");
}

// The current week's Watchtower, or its workbook when no Watchtower is held.
// Empty when the clock is unset or the cache does not cover this week -- the
// meetings screen is what fills that cache, and it is one tap away.
std::optional<std::string> LauncherActivity::thisWeeksMeetingPublication() {
  CivilDate today;
  bool todayIsLocal = false;
  IsoWeek week;
  if (!readLocalDate(today, todayIsLocal) || !isoWeekFromUtcDate(today.year, today.month, today.day, week)) {
    return std::nullopt;
  }

  MeetingWeekTable table;
  MeetingWeekCache::load(table);
  const MeetingWeekEntry* entry = table.find(meetingWeekKey(week));
  if (entry == nullptr) return std::nullopt;

  for (const MeetingPub pub : {MeetingPub::Watchtower, MeetingPub::Workbook}) {
    const std::string& issue = pub == MeetingPub::Watchtower ? entry->watchtower : entry->workbook;
    const std::string path = MeetingLibrary::findPublication(pub, issue);
    if (!path.empty()) return path;
  }
  return std::nullopt;
}

// A Bible the registry does not know is one that did not come through Buscar,
// and the only name it can be recognised by is the CDN's. When several
// languages are on the card the download folder's copy wins, then the root's.
std::optional<std::string> LauncherActivity::findBibleOnCard() {
  const std::vector<std::string> books = CardBooks::list();
  const auto bible = std::find_if(books.begin(), books.end(),
                                  [](const std::string& path) { return isCdnNamedCopyOf(path, BIBLE_SYMBOL); });
  if (bible == books.end()) return std::nullopt;
  return *bible;
}

// recent.json can still list a deleted file; the existence check keeps that
// from putting a Bible on the tile that opens nothing.
std::optional<std::string> LauncherActivity::findBibleInRecents(const std::vector<RecentBook>& recents) {
  for (const RecentBook& book : recents) {
    if (!looksLikeBibleInRecents(book.path, book.title)) continue;
    if (!Storage.exists(book.path.c_str())) continue;
    return book.path;
  }
  return std::nullopt;
}

// Never opens the EPUB for this: its title lives in book.bin, and loading that
// reads the whole spine table -- nearly four thousand entries for the NWT.
// Buscar names the file after the publication, so the filename is the title;
// only a CDN name like "nwt_S" is not worth showing.
std::string LauncherActivity::bibleTitleFor(const std::string& path, const std::vector<RecentBook>& recents) {
  const auto opened =
      std::find_if(recents.begin(), recents.end(), [&](const RecentBook& book) { return book.path == path; });
  if (opened != recents.end() && !opened->title.empty()) return utf8SafeSummary(opened->title, 40);
  if (isCdnNamedCopyOf(path, BIBLE_SYMBOL)) return {};
  return utf8SafeSummary(CardBooks::displayStem(path), 40);
}

// Publications downloaded before PubKeyRegistry existed carry no symbol entry,
// so the card itself is the fallback. The Watchtower outranks the workbook
// outright rather than on issue date: it is the publication the meeting tile is
// recognisable as, and a newer workbook should not displace it.
std::optional<std::string> LauncherActivity::findMeetingPublicationOnCard() {
  std::string folder = SETTINGS.downloadFolder[0] != '\0' ? SETTINGS.downloadFolder : "/";
  while (folder.size() > 1 && folder.back() == '/') folder.pop_back();
  // Epub derives its cache directory from a hash of the path, so a stray double
  // slash here would key a DIFFERENT cache than the reader uses for the same
  // file -- the thumbnail would be built somewhere nothing else looks.
  const std::string prefix = folder == "/" ? "/" : folder + "/";

  std::string bestPath;
  std::string bestIssue;
  bool bestIsWatchtower = false;
  for (const String& entry : Storage.listFiles(folder.c_str(), 200)) {
    const std::string name = entry.c_str();
    const std::string issue = meetingIssueSuffixOf(name);
    if (issue.empty()) continue;

    const bool isWatchtower = name.find("talaya") != std::string::npos || name.find("atchtower") != std::string::npos;
    const bool better = bestPath.empty() || (isWatchtower && !bestIsWatchtower) ||
                        (isWatchtower == bestIsWatchtower && issue > bestIssue);
    if (!better) continue;

    bestIssue = issue;
    bestIsWatchtower = isWatchtower;
    bestPath = name.find('/') == std::string::npos ? prefix + name : name;
  }

  if (bestPath.empty()) return std::nullopt;
  return bestPath;
}

// The art band of a stacked tile: what is left once the label has its room.
int LauncherActivity::tileArtHeight(const TileRect& rect, const bool hasSubtitle) const {
  return rect.h - tileTextHeight(SMALL_FONT_ID, hasSubtitle) - 3 * TILE_PADDING;
}

int LauncherActivity::tileTextHeight(const int titleFont, const bool hasSubtitle) const {
  return renderer.getLineHeight(titleFont) + (hasSubtitle ? renderer.getLineHeight(SMALL_FONT_ID) : 0);
}

void LauncherActivity::computeLayout() {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();

  // The panel's viewable area is inset from its addressable area by a physical
  // bezel. Laying out against the raw screen size puts the bottom strip under
  // that bezel, which is where the resume subtitle used to disappear.
  int marginTop = 0;
  int marginRight = 0;
  int marginBottom = 0;
  int marginLeft = 0;
  renderer.getOrientedViewableTRBL(&marginTop, &marginRight, &marginBottom, &marginLeft);

  const int left = marginLeft + metrics.topPadding;
  const int right = pageWidth - marginRight - metrics.topPadding;
  const int width = right - left;

  const int top = std::max(metrics.topPadding + metrics.headerHeight, marginTop) + TILE_GAP;

  // Height from the fonts, not a constant: the strip carries a title over a
  // subtitle, and a fixed height silently clips both at a larger UI scale.
  const int resumeHeight = tileTextHeight(SMALL_FONT_ID, true) + 2 * TILE_PADDING;
  const int resumeTop = pageHeight - marginBottom - metrics.topPadding - resumeHeight;

  const int available = resumeTop - TILE_GAP - top;

  // Three rows: Bible (double weight), the Meetings/Search pair, then Tags.
  const int unitHeight = (available - 2 * TILE_GAP) / (BIBLE_TILE_WEIGHT + 2);
  // The Bible tile absorbs the division remainder so the rows fill the column
  // exactly rather than leaving a ragged gap above the resume strip.
  const int bibleHeight = available - 2 * TILE_GAP - 2 * unitHeight;
  const int halfWidth = (width - TILE_GAP) / 2;

  int y = top;
  rects[static_cast<size_t>(Tile::Bible)] = {left, y, width, bibleHeight};
  y += bibleHeight + TILE_GAP;
  rects[static_cast<size_t>(Tile::Meetings)] = {left, y, halfWidth, unitHeight};
  // Width from the remainder, not a second halfWidth: an odd column width would
  // otherwise leave this tile a pixel short of the ones above and below it.
  rects[static_cast<size_t>(Tile::Search)] = {left + halfWidth + TILE_GAP, y, width - halfWidth - TILE_GAP, unitHeight};
  y += unitHeight + TILE_GAP;
  rects[static_cast<size_t>(Tile::Settings)] = {left, y, width, unitHeight};

  rects[static_cast<size_t>(Tile::Resume)] = {left, resumeTop, width, resumeHeight};
}

void LauncherActivity::drawTileArt(const int x, const int y, const int w, const int h, const std::string& coverPath,
                                   const uint8_t* icon) const {
  if (CoverThumb::drawNative(renderer, coverPath, x + TILE_PADDING, y, w - 2 * TILE_PADDING, h, TILE_RADIUS / 2) > 0) {
    return;
  }
  if (icon == nullptr) return;
  renderer.drawIcon(icon, x + (w - TILE_ICON_SIZE) / 2, y + (h - TILE_ICON_SIZE) / 2, TILE_ICON_SIZE);
}

// Cover as the tile's background with the label over it. Drawing order is the
// z-order here, so the caption plate and its text simply go down last; the
// plate is opaque because a dithered cover underneath would otherwise shred the
// glyphs on a 1-bit panel. Falls back to the stacked icon-over-label tile when
// the card has no cover large enough to fill this one.
void LauncherActivity::drawCoverTile(const TileRect& rect, const std::string& coverPath, const char* title,
                                     const char* subtitle, const uint8_t* icon, const bool selected,
                                     const float focusBand) const {
  const bool hasSubtitle = subtitle != nullptr && subtitle[0] != '\0';
  const int plateHeight = tileTextHeight(UI_10_FONT_ID, hasSubtitle) + 2 * TILE_PADDING;
  const Rect band{rect.x, rect.y, rect.w, rect.h};
  const CoverBand::Style style{focusBand, TILE_RADIUS, plateHeight};

  if (!CoverBand::draw(renderer, coverPath, band, style)) {
    drawTile(rect, title, subtitle, selected, /*emphasised=*/true, {}, icon);
    return;
  }

  drawCenteredIn(rect.x, rect.w, CoverBand::plateRect(band, style).y + TILE_PADDING, title, subtitle);
  renderer.drawRoundedRect(rect.x, rect.y, rect.w, rect.h, selected ? 3 : 1, TILE_RADIUS, true);
}

void LauncherActivity::drawCenteredIn(const int x, const int w, const int top, const char* title,
                                      const char* subtitle) const {
  const int innerWidth = w - 2 * TILE_PADDING;
  const std::string fittedTitle = renderer.truncatedText(UI_10_FONT_ID, title, innerWidth, EpdFontFamily::BOLD);
  const int titleWidth = renderer.getTextWidth(UI_10_FONT_ID, fittedTitle.c_str(), EpdFontFamily::BOLD);
  renderer.drawText(UI_10_FONT_ID, x + (w - titleWidth) / 2, top, fittedTitle.c_str(), true, EpdFontFamily::BOLD);

  if (subtitle == nullptr || subtitle[0] == '\0') return;
  const std::string fitted = renderer.truncatedText(SMALL_FONT_ID, subtitle, innerWidth);
  const int subtitleWidth = renderer.getTextWidth(SMALL_FONT_ID, fitted.c_str());
  renderer.drawText(SMALL_FONT_ID, x + (w - subtitleWidth) / 2, top + renderer.getLineHeight(UI_10_FONT_ID),
                    fitted.c_str());
}

void LauncherActivity::drawTile(const TileRect& rect, const char* title, const char* subtitle, const bool selected,
                                const bool emphasised, const std::string& coverPath, const uint8_t* icon) const {
  // Selection is a thicker border rather than an inversion: a full-tile inversion
  // on a 1-bit panel costs a visibly slower redraw for the same information.
  const int borderWidth = selected ? 3 : 1;
  renderer.drawRoundedRect(rect.x, rect.y, rect.w, rect.h, borderWidth, TILE_RADIUS, true);

  const int titleFont = emphasised ? UI_10_FONT_ID : SMALL_FONT_ID;
  const bool hasSubtitle = subtitle != nullptr && subtitle[0] != '\0';
  const int innerWidth = rect.w - 2 * TILE_PADDING;
  const int textHeight = tileTextHeight(titleFont, hasSubtitle);

  // Centre the text block as a whole. Offsetting each line from the tile's
  // midpoint by a constant overflows a short tile, and drawText anchors on the
  // line box's top, so the overflow lands off the bottom of the panel.
  int textTop = rect.y + (rect.h - textHeight) / 2;

  const int artHeight = tileArtHeight(rect, hasSubtitle);
  if (artHeight >= MIN_ART_HEIGHT) {
    drawTileArt(rect.x, rect.y + TILE_PADDING, rect.w, artHeight, coverPath, icon);
    textTop = rect.y + TILE_PADDING + artHeight + TILE_PADDING;
  }

  // drawCenteredText centres on the SCREEN, so a tile that is not full width
  // needs its own centring.
  const std::string fittedTitle = renderer.truncatedText(titleFont, title, innerWidth, EpdFontFamily::BOLD);
  const int titleWidth = renderer.getTextWidth(titleFont, fittedTitle.c_str(), EpdFontFamily::BOLD);
  renderer.drawText(titleFont, rect.x + (rect.w - titleWidth) / 2, textTop, fittedTitle.c_str(), true,
                    EpdFontFamily::BOLD);

  if (!hasSubtitle) return;
  const std::string fittedSubtitle = renderer.truncatedText(SMALL_FONT_ID, subtitle, innerWidth);
  const int subtitleWidth = renderer.getTextWidth(SMALL_FONT_ID, fittedSubtitle.c_str());
  renderer.drawText(SMALL_FONT_ID, rect.x + (rect.w - subtitleWidth) / 2, textTop + renderer.getLineHeight(titleFont),
                    fittedSubtitle.c_str());
}

void LauncherActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();

  renderer.clearScreen();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_BEREAN));

  drawCoverTile(rects[0], bibleCoverPath, tr(STR_BIBLE), bibleSubtitle.c_str(), BookIcon, selected == 0,
                CoverBandGeometry::BOOK_TITLE_BAND);
  drawCoverTile(rects[1], meetingsCoverPath, tr(STR_MEETINGS),
                meetingsSubtitle.empty() ? nullptr : meetingsSubtitle.c_str(), LibraryIcon, selected == 1,
                CoverBandGeometry::MAGAZINE_MASTHEAD_BAND);
  // Buscar has landed, so the tile no longer carries a "coming soon" subtitle.
  drawTile(rects[2], tr(STR_PUBLICATIONS), nullptr, selected == 2, false, {}, SearchIcon);
  drawTile(rects[3], tr(STR_SETTINGS_TITLE), nullptr, selected == 3, false, {}, Settings2Icon);

  // The resume strip is deliberately a one-line label over its book's title:
  // it is the fast path out of the launcher, not another shelf.
  const TileRect& resume = rects[4];
  if (hasResume) {
    drawTile(resume, tr(STR_CONTINUE_READING), resumeTitle.c_str(), selected == 4, false, {}, nullptr);
  }

  const bool cleanPaint = launcherNeedsCleanPaint(cleanInitialRefresh, firstRenderDone);
  const auto mode = cleanPaint ? HalDisplay::HALF_REFRESH : HalDisplay::FAST_REFRESH;
  LOG_DBG(MODULE, "Paint: clean=%d firstPaint=%d mode=%s", cleanInitialRefresh ? 1 : 0, firstRenderDone ? 0 : 1,
          cleanPaint ? "HALF" : "FAST");
  renderer.displayBuffer(mode);
  PostedMessage::drawNext(renderer);
  firstRenderDone = true;
}

void LauncherActivity::activate(const Tile tile) {
  switch (tile) {
    case Tile::Bible:
      openBible();
      break;
    case Tile::Meetings:
      openMeetings();
      break;
    case Tile::Search:
      openPublications();
      break;
    case Tile::Settings:
      openSettings();
      break;
    case Tile::Resume:
      if (hasResume) activityManager.goToReader(resumePath, /*allowFastInitialRefresh=*/true);
      break;
    default:
      break;
  }
}

void LauncherActivity::openBible() {
  if (!biblePath.empty()) {
    activityManager.goToReader(biblePath);
    return;
  }
  auto download = makeUniqueNoThrow<BibleDownloadActivity>(renderer, mappedInput);
  if (!download) {
    LOG_ERR(MODULE, "OOM: Bible download activity");
    return;
  }
  // Back to the launcher rather than into the Bible: the tile then shows what
  // arrived, and a first open's indexing popup does not follow straight on
  // from a screen the user just watched download.
  startActivityForResult(std::move(download), [this](const ActivityResult&) {
    resolveTargets();
    requestUpdate();
  });
}

void LauncherActivity::openMeetings() {
  startActivityForResult(std::make_unique<MeetingsActivity>(renderer, mappedInput),
                         [this](const ActivityResult&) { requestUpdate(); });
}

void LauncherActivity::openPublications() {
  startActivityForResult(std::make_unique<PublicationsActivity>(renderer, mappedInput), [this](const ActivityResult&) {
    // A download changes what the Bible and resume tiles can offer.
    resolveTargets();
    requestUpdate();
  });
}

void LauncherActivity::openSettings() { activityManager.goToSettings(); }

void LauncherActivity::loop() {
  // No mappedInput.update() here. main.cpp's loop already ticked HalGPIO this
  // frame, and InputManager::update() clears every one-shot edge it latched
  // (InputManager.cpp:434-442) -- a second tick wipes the tap and the button
  // release before this function can read them.
  int touchX = 0;
  int touchY = 0;
  if (mappedInput.wasScreenTapped(touchX, touchY)) {
    for (size_t i = 0; i < static_cast<size_t>(Tile::COUNT); ++i) {
      if (i == static_cast<size_t>(Tile::Resume) && !hasResume) continue;
      if (!rects[i].contains(touchX, touchY)) continue;
      selected = static_cast<int>(i);
      activate(static_cast<Tile>(i));
      return;
    }
    return;
  }

  const int tileCount = hasResume ? static_cast<int>(Tile::COUNT) : static_cast<int>(Tile::COUNT) - 1;
  buttonNavigator.onNextRelease([this, tileCount] {
    selected = ButtonNavigator::nextIndex(selected, tileCount);
    requestUpdate();
  });
  buttonNavigator.onPreviousRelease([this, tileCount] {
    selected = ButtonNavigator::previousIndex(selected, tileCount);
    requestUpdate();
  });

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    activate(static_cast<Tile>(selected));
  }
}
