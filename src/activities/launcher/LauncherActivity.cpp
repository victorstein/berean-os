#include "LauncherActivity.h"

#include <Bitmap.h>
#include <Epub.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <Utf8.h>

#include <algorithm>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "RecentBooksStore.h"
#include "activities/network/MeetingDownloadActivity.h"
#include "components/UITheme.h"
#include "components/icons/book.h"
#include "components/icons/library.h"
#include "components/icons/search.h"
#include "components/icons/settings2.h"
#include "components/themes/BaseTheme.h"
#include "fontIds.h"
#include "study/BookPathIndex.h"
#include "study/PubKeyRegistry.h"

namespace {

constexpr const char* MODULE = "LAUNCH";

constexpr int TILE_GAP = 10;
constexpr int TILE_PADDING = 8;
constexpr int TILE_ICON_SIZE = 32;
// Below this a cover is a smudge and an icon is cramped, so the tile drops its
// art and centres the label instead.
constexpr int MIN_ART_HEIGHT = TILE_ICON_SIZE + 4;
// The Bible is the centre of the device, so its tile is taller than the pair
// beneath it rather than merely wider.
constexpr int BIBLE_TILE_WEIGHT = 2;

}  // namespace

void LauncherActivity::onEnter() {
  Activity::onEnter();
  resolveTargets();
  computeLayout();
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
    bibleCoverPath = coverThumbFor(*found, generatedAny);
  }

  // The meeting tile shows the cover of whichever weekly publication is on the
  // card. The symbol comes from the registry the downloader writes, not from the
  // filename: meetingPublicationFilename names the file after the publication's
  // own title ("La Atalaya (estudio) 2026-09.epub"), so there is no symbol in it
  // to match on. w = Watchtower study edition, mwb = Meeting Workbook.
  const auto looksLikeAMeetingPub = [](const RecentBook& book) {
    const auto registered = PubKeyRegistry::lookup(book.path);
    return registered && (registered->symbol == "w" || registered->symbol == "mwb");
  };
  const auto meeting = std::find_if(recents.begin(), recents.end(), looksLikeAMeetingPub);
  if (meeting != recents.end()) {
    meetingsSubtitle = utf8SafeSummary(meeting->title, 30);
    meetingsCoverPath = coverThumbFor(*meeting, generatedAny);
  }

  // Building a thumbnail leaves the popup's pixels in the framebuffer, and the
  // launcher paints over a cleared screen anyway -- but the panel still shows
  // the popup until the first render lands, which is the point of drawing it.
  if (generatedAny) LOG_INF(MODULE, "Generated a missing cover thumbnail");
}

// Thumbnails are cached on the card at one fixed height, shared with the cover
// the old home screen drew, so an existing one is reused rather than rebuilt.
// Generating one opens the EPUB, which for the Bible is slow enough to be worth
// a popup -- but it happens once per book, not once per visit.
std::string LauncherActivity::coverThumbFor(const RecentBook& book, bool& generatedAny) {
  if (book.coverBmpPath.empty()) return {};
  std::string path = UITheme::getCoverThumbPath(book.coverBmpPath, BaseMetrics::values.homeCoverHeight);
  if (Storage.exists(path.c_str())) return path;

  if (!FsHelpers::hasEpubExtension(book.path)) return {};
  generatedAny = true;
  Epub epub(book.path, "/.crosspoint");
  epub.load(false, true);
  if (!epub.generateThumbBmp(BaseMetrics::values.homeCoverHeight)) {
    // Drop the cover reference so the next visit falls straight through to the
    // icon instead of reopening the book to fail the same way.
    RECENT_BOOKS.updateBook(book.path, book.title, book.author, "");
    return {};
  }
  return Storage.exists(path.c_str()) ? path : std::string{};
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

void LauncherActivity::drawTileArt(const int x, const int y, const int w, const int h, const std::string& coverPath,
                                   const uint8_t* icon) const {
  if (!coverPath.empty()) {
    HalFile file;
    if (Storage.openFileForRead(MODULE, coverPath, file)) {
      Bitmap bitmap(file);
      if (bitmap.parseHeaders() == BmpReaderError::Ok && bitmap.getWidth() > 0 && bitmap.getHeight() > 0) {
        const int maxWidth = w - 2 * TILE_PADDING;
        // drawBitmap scales down to fit but never up, so mirror that clamp here
        // or the border below would be drawn around empty space.
        const float fit = std::min({static_cast<float>(maxWidth) / static_cast<float>(bitmap.getWidth()),
                                    static_cast<float>(h) / static_cast<float>(bitmap.getHeight()), 1.0f});
        const int drawWidth = static_cast<int>(static_cast<float>(bitmap.getWidth()) * fit);
        const int drawHeight = static_cast<int>(static_cast<float>(bitmap.getHeight()) * fit);
        if (drawWidth > 0 && drawHeight > 0) {
          const int drawX = x + (w - drawWidth) / 2;
          const int drawY = y + (h - drawHeight) / 2;
          renderer.drawBitmap(bitmap, drawX, drawY, drawWidth, drawHeight);
          renderer.drawRect(drawX, drawY, drawWidth, drawHeight, 1, true);
          return;
        }
      }
    }
  }

  if (icon == nullptr) return;
  renderer.drawIcon(icon, x + (w - TILE_ICON_SIZE) / 2, y + (h - TILE_ICON_SIZE) / 2, TILE_ICON_SIZE);
}

void LauncherActivity::drawTile(const TileRect& rect, const char* title, const char* subtitle, const bool selected,
                                const bool emphasised, const std::string& coverPath, const uint8_t* icon) const {
  // Selection is a thicker border rather than an inversion: a full-tile inversion
  // on a 1-bit panel costs a visibly slower redraw for the same information.
  const int borderWidth = selected ? 3 : 1;
  renderer.drawRect(rect.x, rect.y, rect.w, rect.h, borderWidth, true);

  const int titleFont = emphasised ? UI_10_FONT_ID : SMALL_FONT_ID;
  const bool hasSubtitle = subtitle != nullptr && subtitle[0] != '\0';
  const int innerWidth = rect.w - 2 * TILE_PADDING;
  const int textHeight = tileTextHeight(titleFont, hasSubtitle);

  // Centre the text block as a whole. Offsetting each line from the tile's
  // midpoint by a constant overflows a short tile, and drawText anchors on the
  // line box's top, so the overflow lands off the bottom of the panel.
  int textTop = rect.y + (rect.h - textHeight) / 2;

  const int artHeight = rect.h - textHeight - 3 * TILE_PADDING;
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

  drawTile(rects[0], tr(STR_BIBLE), bibleSubtitle.c_str(), selected == 0, true, bibleCoverPath, BookIcon);
  drawTile(rects[1], tr(STR_MEETINGS), meetingsSubtitle.empty() ? nullptr : meetingsSubtitle.c_str(), selected == 1,
           false, meetingsCoverPath, LibraryIcon);
  drawTile(rects[2], tr(STR_SEARCH), tr(STR_COMING_SOON), selected == 2, false, {}, SearchIcon);
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
      // Buscar lands in phase 3. The tile is drawn rather than hidden so the
      // shape of the device is honest about what is coming.
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
  startActivityForResult(std::make_unique<MeetingDownloadActivity>(renderer, mappedInput),
                         [this](const ActivityResult&) { requestUpdate(); });
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
