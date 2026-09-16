#include "LauncherActivity.h"

#include <Bitmap.h>
#include <Epub.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalClock.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>
#include <Utf8.h>

#include <algorithm>
#include <cctype>
#include <optional>

#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "MappedInputManager.h"
#include "RecentBooksStore.h"
#include "activities/catalog/PublicationsActivity.h"
#include "activities/network/MeetingsActivity.h"
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
#include "study/BookPathIndex.h"
#include "study/PubKeyRegistry.h"

namespace {

constexpr const char* MODULE = "LAUNCH";

constexpr int TILE_GAP = 10;
constexpr int TILE_PADDING = 8;
constexpr int TILE_ICON_SIZE = 32;
constexpr int TILE_RADIUS = 8;
// A cover must be at least as large as the tile in both axes to fill it without
// upscaling. Covers run roughly 0.6-0.75 wide-to-tall, so asking for a
// thumbnail this many times the tile's width in height clears the tile's width
// for anything in that range; a narrower cover is declined rather than blown up.
constexpr float NARROWEST_COVER_ASPECT = 0.6f;
// Where a cover's identifying mark sits, as a fraction of its height -- what the
// crop aims to put in the middle of the visible artwork. A book prints its title
// a little below the top edge; a magazine's masthead runs right along the top,
// so it wants 0 and the crop simply starts at the first row.
constexpr float BOOK_TITLE_BAND = 0.25f;
constexpr float MAGAZINE_MASTHEAD_BAND = 0.0f;
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

  // The Bible tile opens whatever Bible is on the card. BookPathIndex already
  // walks it for the migration, so this reuses that rather than inventing a
  // second notion of "where the books are".
  biblePath.clear();
  bibleSubtitle = tr(STR_BIBLE_SUBTITLE_NONE);

  // Matched on the symbol in the filename first, then the title in either
  // language, because a sideloaded copy may carry neither the CDN's name nor a
  // registry entry. Phase 3's catalog download will register a pubkey and make
  // this a lookup rather than a guess.
  const auto looksLikeABible = [](const RecentBook& book) {
    return book.path.find("nwt") != std::string::npos || book.title.find("Nuevo Mundo") != std::string::npos ||
           book.title.find("New World") != std::string::npos;
  };
  const auto found = std::find_if(recents.begin(), recents.end(), looksLikeABible);
  if (found != recents.end()) {
    biblePath = found->path;
    bibleSubtitle = utf8SafeSummary(found->title, 40);
    bibleCoverPath = coverThumbFor(found->path, coverFillHeight(rects[static_cast<size_t>(Tile::Bible)]), generatedAny);
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
    meetingsCoverPath =
        coverThumbFor(*meetingPath, coverFillHeight(rects[static_cast<size_t>(Tile::Meetings)]), generatedAny);
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
  HalClock::Date today{};
  IsoWeek week;
  if (!halClock.getDate(today) || !isoWeekFromUtcDate(today.year, today.month, today.day, week)) return std::nullopt;

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

// The thumbnail is requested at exactly the height it will be drawn at, and is
// never resampled afterwards. generateThumbBmp emits a DITHERED 1-bit image,
// and drawBitmap1Bit rescales by point-sampling -- picking every Nth pixel out
// of a pattern whose whole meaning is the local density of its pixels, which
// turns a cover into uniform static. Matching the sizes is the only way to
// render one honestly on a 1-bit panel.
//
// Keyed on the book path rather than a recents entry: Epub derives its cache
// path from the path alone, so this works for a publication that has been
// downloaded but never opened.
std::string LauncherActivity::coverThumbFor(const std::string& bookPath, const int height, bool& generatedAny) {
  if (bookPath.empty() || height <= 0 || !FsHelpers::hasEpubExtension(bookPath)) return {};

  Epub epub(bookPath, "/.crosspoint");
  const std::string path = epub.getThumbBmpPath(height);
  if (Storage.exists(path.c_str())) return path;

  generatedAny = true;
  // buildIfMissing, not the cached-only load the old home screen could rely on:
  // that one only ever saw books that had been opened, and this one has to cope
  // with a publication downloaded and never read, whose metadata cache does not
  // exist yet. Without it generateThumbBmp fails with "cache not loaded".
  epub.load(true, true);
  if (!epub.generateThumbBmp(height)) {
    LOG_DBG(MODULE, "No cover thumbnail for %s", bookPath.c_str());
    return {};
  }
  return Storage.exists(path.c_str()) ? path : std::string{};
}

// The art band of a stacked tile: what is left once the label has its room.
int LauncherActivity::tileArtHeight(const TileRect& rect, const bool hasSubtitle) const {
  return rect.h - tileTextHeight(SMALL_FONT_ID, hasSubtitle) - 3 * TILE_PADDING;
}

// The Bible tile puts its cover beside the label rather than above it, so the
// cover gets the tile's full height instead of the third left over under a
// centred caption.
// A cover used as a tile background must cover the tile on both axes before
// anything is cropped away. The tile's WIDTH is what binds: covers are portrait,
// so a thumbnail tall enough to fill the height is still far too narrow.
int LauncherActivity::coverFillHeight(const TileRect& tile) {
  return std::max(tile.h, static_cast<int>(static_cast<float>(tile.w) / NARROWEST_COVER_ASPECT));
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
  rects[static_cast<size_t>(Tile::TagsAndSettings)] = {left, y, width, unitHeight};

  rects[static_cast<size_t>(Tile::Resume)] = {left, resumeTop, width, resumeHeight};
}

int LauncherActivity::drawCoverNative(const std::string& coverPath, const int x, const int y, const int boxWidth,
                                      const int boxHeight) const {
  if (coverPath.empty()) return 0;
  HalFile file;
  if (!Storage.openFileForRead(MODULE, coverPath, file)) return 0;

  Bitmap bitmap(file);
  if (bitmap.parseHeaders() != BmpReaderError::Ok) return 0;
  const int width = bitmap.getWidth();
  const int height = bitmap.getHeight();
  // Refuse rather than rescale: see coverThumbFor. A cover that does not fit is
  // a layout change that outran its cached thumbnail, and the icon is the
  // honest fallback until the new size is generated.
  if (width <= 0 || height <= 0 || width > boxWidth || height > boxHeight) return 0;

  const int drawX = x + (boxWidth - width) / 2;
  const int drawY = y + (boxHeight - height) / 2;
  renderer.drawBitmap(bitmap, drawX, drawY, width, height);
  renderer.drawRoundedRect(drawX, drawY, width, height, 1, TILE_RADIUS / 2, true);
  return width;
}

void LauncherActivity::drawTileArt(const int x, const int y, const int w, const int h, const std::string& coverPath,
                                   const uint8_t* icon) const {
  if (drawCoverNative(coverPath, x + TILE_PADDING, y, w - 2 * TILE_PADDING, h) > 0) return;
  if (icon == nullptr) return;
  renderer.drawIcon(icon, x + (w - TILE_ICON_SIZE) / 2, y + (h - TILE_ICON_SIZE) / 2, TILE_ICON_SIZE);
}

// Blits the cover across the whole tile at 1:1, cropped rather than scaled:
// anchored to the cover's top so its own title art survives, centred
// horizontally, and clipped to the tile so it cannot bleed into its neighbours.
// GfxRenderer has no clip region and drawBitmap only ever scales DOWN, so the
// row walk is done here. Resampling is deliberately absent -- these thumbnails
// are dithered 1-bit and any resampling turns them into static.
bool LauncherActivity::drawCoverFilling(const std::string& coverPath, const TileRect& rect, const int visibleHeight,
                                        const float focusBand) const {
  if (coverPath.empty()) return false;
  HalFile file;
  if (!Storage.openFileForRead(MODULE, coverPath, file)) return false;

  Bitmap bitmap(file);
  if (bitmap.parseHeaders() != BmpReaderError::Ok) return false;
  const int width = bitmap.getWidth();
  const int height = bitmap.getHeight();
  if (width < rect.w || height < rect.h) return false;

  auto packedRow = makeUniqueNoThrow<uint8_t[]>((width + 3) / 4);
  auto rowScratch = makeUniqueNoThrow<uint8_t[]>(bitmap.getRowBytes());
  if (!packedRow || !rowScratch) {
    LOG_ERR(MODULE, "OOM: cover row buffers");
    return false;
  }

  const int xOffset = (width - rect.w) / 2;
  // Vertical crop is aimed rather than anchored. A book puts its title in the
  // upper part of the cover, so centring the whole cover buries the title above
  // the crop and anchoring at the top strands it down against the plate;
  // centring the title band on the artwork that is actually visible -- the tile
  // less the caption plate covering its foot -- keeps it where the eye lands.
  const int focusRow = static_cast<int>(static_cast<float>(height) * focusBand);
  const int yOffset = std::clamp(focusRow - visibleHeight / 2, 0, height - rect.h);

  for (int row = 0; row < height; ++row) {
    if (bitmap.readNextRow(packedRow.get(), rowScratch.get()) != BmpReaderError::Ok) return false;
    // Rows arrive in file order; a bottom-up BMP delivers the cover's last row
    // first, so the source row has to be resolved before it can be discarded.
    const int sourceRow = bitmap.isTopDown() ? row : height - 1 - row;
    if (sourceRow < yOffset || sourceRow >= yOffset + rect.h) continue;

    const int screenY = rect.y + sourceRow - yOffset;
    for (int column = 0; column < rect.w; ++column) {
      const int sourceColumn = column + xOffset;
      const uint8_t value = packedRow[sourceColumn / 4] >> (6 - ((sourceColumn * 2) % 8)) & 0x3;
      if (value < 3) renderer.drawPixel(rect.x + column, screenY, true);
    }
  }
  return true;
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

  if (!drawCoverFilling(coverPath, rect, rect.h - plateHeight, focusBand)) {
    drawTile(rect, title, subtitle, selected, /*emphasised=*/true, {}, icon);
    return;
  }

  // The cover was blitted as a rectangle, so its corners sit outside the rounded
  // border about to be drawn over them. Masking them back to paper first is what
  // stops the art poking out past the arc.
  renderer.maskRoundedRectOutsideCorners(rect.x, rect.y, rect.w, rect.h, TILE_RADIUS);

  const int plateTop = rect.y + rect.h - plateHeight;
  renderer.fillRoundedRect(rect.x, plateTop, rect.w, plateHeight, TILE_RADIUS, /*roundTopLeft=*/false,
                           /*roundTopRight=*/false, /*roundBottomLeft=*/true, /*roundBottomRight=*/true, Color::White);
  renderer.drawLine(rect.x, plateTop, rect.x + rect.w - 1, plateTop, true);
  drawCenteredIn(rect.x, rect.w, plateTop + TILE_PADDING, title, subtitle);
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
                BOOK_TITLE_BAND);
  drawCoverTile(rects[1], meetingsCoverPath, tr(STR_MEETINGS),
                meetingsSubtitle.empty() ? nullptr : meetingsSubtitle.c_str(), LibraryIcon, selected == 1,
                MAGAZINE_MASTHEAD_BAND);
  // Buscar has landed, so the tile no longer carries a "coming soon" subtitle.
  drawTile(rects[2], tr(STR_PUBLICATIONS), nullptr, selected == 2, false, {}, SearchIcon);
  drawTile(rects[3], tr(STR_TAGS_AND_SETTINGS), nullptr, selected == 3, false, {}, Settings2Icon);

  // The resume strip is deliberately a one-line label over its book's title:
  // it is the fast path out of the launcher, not another shelf.
  const TileRect& resume = rects[4];
  if (hasResume) {
    drawTile(resume, tr(STR_CONTINUE_READING), resumeTitle.c_str(), selected == 4, false, {}, nullptr);
  }

  renderer.displayBuffer();
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
    case Tile::TagsAndSettings:
      openTagsAndSettings();
      break;
    case Tile::Resume:
      if (hasResume) activityManager.goToReader(resumePath, /*allowFastInitialRefresh=*/true);
      break;
    default:
      break;
  }
}

void LauncherActivity::openBible() {
  if (biblePath.empty()) {
    // No Bible on the card yet. The file browser is the honest destination --
    // phase 3's catalog will replace it.
    activityManager.goToFileBrowser();
    return;
  }
  activityManager.goToReader(biblePath);
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

void LauncherActivity::openTagsAndSettings() { activityManager.goToSettings(); }

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
