#include "LauncherActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <Utf8.h>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "RecentBooksStore.h"
#include "activities/network/MeetingDownloadActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "study/BookPathIndex.h"

namespace {

constexpr int TILE_GAP = 10;
constexpr int RESUME_HEIGHT = 44;
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
  for (const auto& book : recents) {
    if (book.path.find("nwt") != std::string::npos || book.title.find("Nuevo Mundo") != std::string::npos ||
        book.title.find("New World") != std::string::npos) {
      biblePath = book.path;
      bibleSubtitle = utf8SafeSummary(book.title, 40);
      break;
    }
  }
}

void LauncherActivity::computeLayout() {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();

  const int left = metrics.topPadding;
  const int right = pageWidth - metrics.topPadding;
  const int width = right - left;

  const int top = metrics.topPadding + metrics.headerHeight;
  const int bottom = pageHeight - metrics.topPadding - RESUME_HEIGHT - TILE_GAP;
  const int available = bottom - top;

  // Three rows: Bible (double weight), the Meetings/Search pair, then Tags.
  const int unitHeight = (available - 2 * TILE_GAP) / (BIBLE_TILE_WEIGHT + 2);
  const int bibleHeight = unitHeight * BIBLE_TILE_WEIGHT;
  const int halfWidth = (width - TILE_GAP) / 2;

  int y = top;
  rects[static_cast<size_t>(Tile::Bible)] = {left, y, width, bibleHeight};
  y += bibleHeight + TILE_GAP;
  rects[static_cast<size_t>(Tile::Meetings)] = {left, y, halfWidth, unitHeight};
  rects[static_cast<size_t>(Tile::Search)] = {left + halfWidth + TILE_GAP, y, halfWidth, unitHeight};
  y += unitHeight + TILE_GAP;
  rects[static_cast<size_t>(Tile::TagsAndSettings)] = {left, y, width, unitHeight};

  rects[static_cast<size_t>(Tile::Resume)] = {left, pageHeight - metrics.topPadding - RESUME_HEIGHT, width,
                                              RESUME_HEIGHT};
}

void LauncherActivity::drawTile(const TileRect& rect, const char* title, const char* subtitle, const bool selected,
                                const bool emphasised) const {
  // Selection is a thicker border rather than an inversion: a full-tile inversion
  // on a 1-bit panel costs a visibly slower redraw for the same information.
  const int borderWidth = selected ? 3 : 1;
  renderer.drawRect(rect.x, rect.y, rect.w, rect.h, borderWidth, true);

  const int titleFont = emphasised ? UI_10_FONT_ID : SMALL_FONT_ID;
  const bool hasSubtitle = subtitle != nullptr && subtitle[0] != '\0';
  const int titleY = hasSubtitle ? rect.y + rect.h / 2 - 6 : rect.y + rect.h / 2 + 4;

  // drawCenteredText centres on the SCREEN, so a tile that is not full width
  // needs its own centring.
  const int titleWidth = renderer.getTextWidth(titleFont, title, EpdFontFamily::BOLD);
  renderer.drawText(titleFont, rect.x + (rect.w - titleWidth) / 2, titleY, title, true, EpdFontFamily::BOLD);

  if (hasSubtitle) {
    const int subtitleWidth = renderer.getTextWidth(SMALL_FONT_ID, subtitle);
    renderer.drawText(SMALL_FONT_ID, rect.x + (rect.w - subtitleWidth) / 2, rect.y + rect.h / 2 + 16, subtitle);
  }
}

void LauncherActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();

  renderer.clearScreen();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_BEREAN));

  drawTile(rects[0], tr(STR_BIBLE), bibleSubtitle.c_str(), selected == 0, true);
  drawTile(rects[1], tr(STR_MEETINGS), nullptr, selected == 1, false);
  drawTile(rects[2], tr(STR_SEARCH), tr(STR_COMING_SOON), selected == 2, false);
  drawTile(rects[3], tr(STR_TAGS_AND_SETTINGS), nullptr, selected == 3, false);

  const TileRect& resume = rects[4];
  if (hasResume) {
    drawTile(resume, tr(STR_CONTINUE_READING), resumeTitle.c_str(), selected == 4, false);
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
  mappedInput.update();

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
