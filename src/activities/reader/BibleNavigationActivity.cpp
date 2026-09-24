#include "BibleNavigationActivity.h"

#include <Epub/BibleNavScanner.h>
#include <FontCacheManager.h>
#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>
#include <Utf8.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "BookGridLayout.h"
#include "MappedInputManager.h"
#include "SpineHtmlStream.h"
#include "components/UIScale.h"
#include "components/UITheme.h"

namespace fui = freeink::ui;

namespace {

void copyTruncated(char* dest, const size_t destBytes, const std::string& source) {
  const size_t fit = source.size() < destBytes - 1 ? source.size() : destBytes - 1;
  // A byte-cut would feed drawText an incomplete UTF-8 sequence, which renders
  // as a replacement character.
  const int safe = utf8SafeTruncateBuffer(source.data(), static_cast<int>(fit));
  memcpy(dest, source.data(), static_cast<size_t>(safe));
  dest[safe] = '\0';
}

bool feedNavScanner(void* ctx, const char* chunk, const size_t length, const bool isFinal) {
  return static_cast<BibleNav::Scanner*>(ctx)->feed(chunk, length, isFinal);
}

bool feedVerseScanner(void* ctx, const char* chunk, const size_t length, const bool isFinal) {
  return static_cast<VerseAnchors::Scanner*>(ctx)->feed(chunk, length, isFinal);
}

}  // namespace

BibleNavigationActivity::BibleNavigationActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                                 const std::shared_ptr<Epub>& epub)
    : UiListActivity("BibleNavigation", renderer, mappedInput, /*wantsTouchLongPress=*/false), epub(epub) {}

void BibleNavigationActivity::onEnter() {
  UiListActivity::onEnter();

  // The reader underneath pins its page-render glyph arenas while this overlay
  // is up; freeing them gives the grid labels room to keep their own fallback
  // glyphs resident. Mirrors EpubReaderChapterSelectionActivity.
  if (auto* fcm = renderer.getFontCacheManager()) {
    fcm->clearCache();
  }

  if (!loadBooks()) {
    LOG_ERR("BNV", "Failed to read the book list");
  }
}

bool BibleNavigationActivity::loadBooks() {
  bookCount = 0;
  if (!epub) return false;

  BibleNav::Scanner scanner;
  if (!scanner.valid()) {
    LOG_ERR("BNV", "OOM: nav scanner");
    return false;
  }
  if (!SpineHtmlStream::stream(epub, epub->getBibleBookNavSpineIndex(), renderer, feedNavScanner, &scanner))
    return false;

  BibleNav::BookNavPage page = scanner.takeBookNav();
  std::vector<std::string>& targets = page.targets;
  if (targets.empty()) return false;
  if (targets.size() > MAX_BOOKS) targets.resize(MAX_BOOKS);
  bookCount = static_cast<int>(targets.size());

  auto spineIndices = makeUniqueNoThrow<int[]>(static_cast<size_t>(bookCount));
  if (!spineIndices) {
    LOG_ERR("BNV", "OOM: %d spine indices", bookCount);
    bookCount = 0;
    return false;
  }
  epub->resolveFilenamesToSpineIndices(targets.data(), spineIndices.get(), bookCount);

  std::vector<std::string> names(bookCount);
  const int tocCount = epub->getTocItemsCount();
  for (int i = 0; i < tocCount; i++) {
    const auto tocItem = epub->getTocItem(i);
    const int match = BibleNav::findTargetByHref(targets.data(), bookCount, tocItem.href);
    if (match >= 0 && names[match].empty()) names[match] = tocItem.title;
  }

  for (int i = 0; i < bookCount; i++) {
    bookTargetSpine[i] = static_cast<int16_t>(spineIndices[i]);
    bookIsDirect[i] = !BibleNav::isChapterNav(targets[i]);
    copyTruncated(bookName[i], BOOK_NAME_BYTES, names[i]);
  }

  const int bodyFont = uiScaleSpec().bodyFontId;
  // Cell labels take the theme's bodyText, which is bold in some themes.
  const auto labelStyle =
      UITheme::getInstance().getMetrics().listTitleBold ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR;
  widestAbbrevPx = 0;
  for (int i = 0; i < bookCount; i++) {
    const bool hasAbbrev = i < static_cast<int>(page.labels.size()) && !page.labels[i].empty();
    if (hasAbbrev) {
      copyTruncated(bookAbbrev[i], BOOK_ABBREV_BYTES, page.labels[i]);
    } else {
      copyTruncated(bookAbbrev[i], BOOK_ABBREV_BYTES, names[i]);
    }
    widestAbbrevPx = std::max(widestAbbrevPx, renderer.getTextWidth(bodyFont, bookAbbrev[i], labelStyle));
  }

  sectionCount = 0;
  for (const auto& section : page.sections) {
    if (sectionCount == BookGrid::MAX_SECTIONS) break;
    copyTruncated(sectionTitle[sectionCount], BOOK_NAME_BYTES, section.title);
    sectionStart[sectionCount] = section.firstLink;
    sectionCount++;
  }
  return true;
}

bool BibleNavigationActivity::loadChapters(const int bookIndex) {
  chapterCount = 0;
  if (!epub || bookIndex < 0 || bookIndex >= bookCount) return false;

  BibleNav::Scanner scanner;
  if (!scanner.valid()) {
    LOG_ERR("BNV", "OOM: nav scanner");
    return false;
  }
  if (!SpineHtmlStream::stream(epub, bookTargetSpine[bookIndex], renderer, feedNavScanner, &scanner)) return false;

  std::vector<std::string> targets = scanner.take();
  BibleNav::dropBookNavLinks(targets);
  if (targets.empty()) return false;
  if (targets.size() > MAX_CHAPTERS) targets.resize(MAX_CHAPTERS);
  chapterCount = static_cast<int>(targets.size());

  auto spineIndices = makeUniqueNoThrow<int[]>(static_cast<size_t>(chapterCount));
  if (!spineIndices) {
    LOG_ERR("BNV", "OOM: %d spine indices", chapterCount);
    chapterCount = 0;
    return false;
  }
  epub->resolveFilenamesToSpineIndices(targets.data(), spineIndices.get(), chapterCount);
  for (int i = 0; i < chapterCount; i++) {
    chapterSpine[i] = static_cast<int16_t>(spineIndices[i]);
  }
  return true;
}

bool BibleNavigationActivity::loadVerses(const int spineIndex) {
  verseAnchors.clear();
  verseSpine = spineIndex;

  VerseAnchors::Scanner scanner;
  if (!scanner.valid()) {
    LOG_ERR("BNV", "OOM: verse scanner");
    return false;
  }
  if (!SpineHtmlStream::stream(epub, spineIndex, renderer, feedVerseScanner, &scanner)) return false;

  verseAnchors = scanner.take();
  return !verseAnchors.empty();
}

int BibleNavigationActivity::listCount() const {
  switch (level) {
    case Level::Book:
      return bookCount;
    case Level::Chapter:
      return chapterCount;
    case Level::Verse:
      return static_cast<int>(verseAnchors.size());
  }
  return 0;
}

int BibleNavigationActivity::gridCellsPerPage() const {
  return level == Level::Book ? bookLayout.cols * bookLayout.rows : grid.cellsPerPage();
}

int BibleNavigationActivity::gridPageCount() const {
  return level == Level::Book ? bookLayout.pageCount : NumberGrid::pageCount(listCount(), grid.cellsPerPage());
}

int BibleNavigationActivity::gridPageOf(const int index) const {
  return level == Level::Book ? BookGrid::pageOf(bookLayout, index)
                              : NumberGrid::pageOfIndex(index, grid.cellsPerPage());
}

int BibleNavigationActivity::gridPageFirst(const int page) const {
  if (level != Level::Book) return NumberGrid::pageFirstCell(page, grid.cellsPerPage());
  if (page < 0 || page >= bookLayout.pageCount) return 0;
  return bookLayout.pages[page].first;
}

int BibleNavigationActivity::subHeaderHeight() const {
  return level == Level::Book && sectionCount > 0 ? UITheme::getInstance().getMetrics().tabBarHeight : 0;
}

void BibleNavigationActivity::enterLevel(const Level next, const int selected) {
  {
    // The render task reads level/nav mid-build, so the whole switch has to
    // land before it can see any part of it.
    RenderLock lock;
    level = next;
    nav.reset();
    nav.selected = selected < 0 || selected >= listCount() ? 0 : selected;
    // reset() leaves visibleRows at 1 and only syncToProps -- the list path,
    // which no level takes -- ever writes it, so follow(), scrollBy() and
    // pageRows() would treat a single cell as a whole viewport. One grid "row"
    // is one page; the first build corrects it once geometry is known.
    nav.visibleRows = gridCellsPerPage() > 0 ? gridCellsPerPage() : 1;
    nav.top = gridPageFirst(gridPageOf(nav.selected));
  }
  requestUpdate();
}

void BibleNavigationActivity::finishWith(const int spineIndex, const std::optional<uint32_t> offsetJump) {
  if (spineIndex < 0) {
    LOG_ERR("BNV", "Row resolved to no spine item");
    return;
  }
  app.clearTapFlash();
  setResult(ChapterResult{spineIndex, "", offsetJump});
  finish();
}

void BibleNavigationActivity::openVerseList(const int spineIndex, const int chapterRow) {
  if (!loadVerses(spineIndex)) {
    // Every chapter in these publications carries verse markers, so this is
    // defence rather than a known path: fall back to the top of the chapter.
    LOG_DBG("BNV", "No verse markers in spine %d", spineIndex);
    finishWith(spineIndex, std::nullopt);
    return;
  }
  selectedChapterRow = chapterRow;
  enterLevel(Level::Verse, 0);
}

void BibleNavigationActivity::activateIndex(const int index) {
  if (index < 0 || index >= listCount()) return;

  switch (level) {
    case Level::Book:
      selectedBook = index;
      // The five single-chapter books have no chapter level, so their row is
      // the only route to their verses.
      if (bookIsDirect[index]) {
        openVerseList(bookTargetSpine[index], -1);
        return;
      }
      if (!loadChapters(index)) {
        LOG_ERR("BNV", "Failed to read the chapter list for book %d", index);
        requestUpdate();
        return;
      }
      enterLevel(Level::Chapter, 0);
      return;
    case Level::Chapter:
      openVerseList(chapterSpine[index], index);
      return;
    case Level::Verse:
      finishWith(verseSpine, verseAnchors[index].offset);
      return;
  }
}

void BibleNavigationActivity::moveGridSelection(const int index) {
  const int count = listCount();
  if (count <= 0) return;
  const int clamped = std::clamp(index, 0, count - 1);
  {
    // Same nav-vs-render race moveSelectionTo guards: the render task reads the
    // selection and its page together mid-build.
    RenderLock lock;
    nav.selected = clamped;
    nav.top = gridPageFirst(gridPageOf(clamped));
  }
  requestUpdate();
}

bool BibleNavigationActivity::handleCustomInput() {
  if (gridCellsPerPage() <= 0) return false;

  const auto swipe = mappedInput.wasSwipe();
  if (swipe != MappedInputManager::SwipeDir::Up && swipe != MappedInputManager::SwipeDir::Down) return false;

  const int page = gridPageOf(nav.top);
  const int next = swipe == MappedInputManager::SwipeDir::Up ? page + 1 : page - 1;
  // Consumed either way: the base loop would otherwise scroll the viewport a
  // single cell off its page boundary.
  if (next >= 0 && next < gridPageCount()) moveGridSelection(gridPageFirst(next));
  return true;
}

void BibleNavigationActivity::navigateButtons() {
  const int cols = level == Level::Book ? bookLayout.cols : grid.cols;
  if (cols <= 0) {
    UiListActivity::navigateButtons();
    return;
  }
  buttonNavigator.onNextRelease([this, cols] { moveGridSelection(nav.selected + cols); });
  buttonNavigator.onPreviousRelease([this, cols] { moveGridSelection(nav.selected - cols); });
  buttonNavigator.onNextContinuous([this] {
    const int page = gridPageOf(nav.selected);
    moveGridSelection(page + 1 < gridPageCount() ? gridPageFirst(page + 1) : listCount() - 1);
  });
  buttonNavigator.onPreviousContinuous([this] {
    const int page = gridPageOf(nav.selected);
    moveGridSelection(page > 0 ? gridPageFirst(page - 1) : 0);
  });
}

void BibleNavigationActivity::cancel() {
  ActivityResult result;
  result.isCancelled = true;
  setResult(std::move(result));
  finish();
}

void BibleNavigationActivity::onBackButton() {
  switch (level) {
    case Level::Book:
      cancel();
      return;
    case Level::Chapter:
      enterLevel(Level::Book, selectedBook);
      return;
    case Level::Verse:
      // A verse list reached from a single-chapter book has no chapter level
      // underneath it.
      if (selectedChapterRow < 0) {
        enterLevel(Level::Book, selectedBook);
      } else {
        enterLevel(Level::Chapter, selectedChapterRow);
      }
      return;
  }
}

void BibleNavigationActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  // Content: the safe area minus the header band drawChrome paints the title in,
  // and the section band drawFooter paints below it at the book level.
  screen.setContentMargin(fui::Insets{
      static_cast<int16_t>(safe.y + metrics.topPadding + metrics.headerHeight + subHeaderHeight()),
      static_cast<int16_t>(renderer.getScreenWidth() - (safe.x + safe.width)),
      static_cast<int16_t>(renderer.getScreenHeight() - (safe.y + safe.height)), static_cast<int16_t>(safe.x)});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  if (listCount() == 0) {
    screen.centeredText(tr(STR_NO_CHAPTERS), screen.theme().bodyText);
    return;
  }

  buildGrid(screen);
}

void BibleNavigationActivity::buildGrid(UiScreen& screen) {
  const fui::Rect body = screen.body();
  const int count = listCount();

  int rows = 0;
  int cols = 0;
  int pageFirst = 0;
  int pageCells = 0;
  if (level == Level::Book) {
    bookLayout = BookGrid::layoutFor(bookCount, sectionStart, sectionCount, body.width, body.height, widestAbbrevPx);
    if (bookLayout.coveredBooks < bookCount && !bookLayoutShortLogged) {
      LOG_ERR("BNV", "Book grid covers %d of %d books in a %dx%d rect", bookLayout.coveredBooks, bookCount, body.width,
              body.height);
      bookLayoutShortLogged = true;
    }
    if (bookLayout.pageCount == 0) return;
    rows = bookLayout.rows;
    cols = bookLayout.cols;
    const int page = BookGrid::pageOf(bookLayout, nav.selected);
    pageFirst = bookLayout.pages[page].first;
    pageCells = bookLayout.pages[page].count;
  } else {
    grid = NumberGrid::geometryFor(body.width, body.height);
    rows = grid.rows;
    cols = grid.cols;
    pageFirst = NumberGrid::pageStartFor(nav.top, count, grid.cellsPerPage());
    pageCells = NumberGrid::cellsOnPage(count, pageFirst, grid.cellsPerPage());
  }
  const int cellsPerPage = rows * cols;

  // A geometry change re-pages around the selection rather than leaving
  // nav.top on a page the new geometry no longer has.
  nav.visibleRows = cellsPerPage;
  nav.top = pageFirst;

  for (int i = 0; i < cellsPerPage; i++) {
    const int row = pageFirst + i;
    fui::KeyGridKey cell;
    if (i < pageCells) {
      cell.label = cellLabel(row, i);
      // ACTION_ROW dispatch (onRowAction) indexes the level by this value, so
      // it is the absolute row, not the cell's place on the page.
      cell.value = static_cast<int16_t>(row);
    } else {
      // The page stays rectangular, with uniform cells across pages; a disabled
      // cell registers no interaction.
      cell.kind = fui::KeyKind::Disabled;
      cell.enabled = false;
    }
    cells[i] = cell;
  }

  fui::KeyGridProps props;
  props.keys = cells;
  props.rows = static_cast<uint8_t>(rows);
  props.cols = static_cast<uint8_t>(cols);
  // keyGrid compares this against a page-relative cell index, unlike the
  // absolute value each cell carries.
  props.selectedIndex = static_cast<int16_t>(NumberGrid::pageRelativeIndex(nav.selected, pageFirst, cellsPerPage));
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;
  props.gap = NumberGrid::GAP;
  // Above the cell size, ensureMinTouchRect would grow each hit rect past its
  // own cell and neighbouring cells would swallow each other's taps.
  props.minTouchSize =
      static_cast<int16_t>(NumberGrid::cellSizeFor(body.width, body.height, NumberGrid::Geometry{cols, rows}));
  props.labelText = screen.theme().bodyText;
  props.labelText.align = fui::TextAlign::Center;
  props.keyStyles = screen.theme().key;
  fui::keyGrid(screen.frame(), body, props);
}

const char* BibleNavigationActivity::cellLabel(const int row, const int cell) {
  if (level == Level::Book) return bookAbbrev[row];
  const unsigned number =
      level == Level::Verse ? static_cast<unsigned>(verseAnchors[row].verse) : static_cast<unsigned>(row + 1);
  snprintf(cellLabels[cell], CELL_LABEL_BYTES, "%u", number);
  return cellLabels[cell];
}

void BibleNavigationActivity::drawChrome() {
  const bool hasBook = selectedBook >= 0 && selectedBook < bookCount && bookName[selectedBook][0] != '\0';
  const char* title = tr(STR_SELECT_BOOK);
  if (level == Level::Chapter) {
    title = hasBook ? bookName[selectedBook] : tr(STR_SELECT_CHAPTER);
  } else if (level == Level::Verse) {
    if (hasBook) {
      const int chapter = selectedChapterRow >= 0 ? selectedChapterRow + 1 : 1;
      snprintf(headerTitle, sizeof(headerTitle), "%s %d", bookName[selectedBook], chapter);
      title = headerTitle;
    } else {
      title = tr(STR_SELECT_VERSE);
    }
  }

  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  GUI.drawHeader(renderer, Rect{safe.x, safe.y + metrics.topPadding, safe.width, metrics.headerHeight}, title);
}

void BibleNavigationActivity::drawFooter() {
  UiListActivity::drawFooter();

  if (subHeaderHeight() == 0 || bookLayout.pageCount == 0) return;
  const int page = BookGrid::pageOf(bookLayout, nav.selected);
  const int section = bookLayout.pages[page].section;
  char pageIndicator[12];
  snprintf(pageIndicator, sizeof(pageIndicator), "%d/%d", page + 1, bookLayout.pageCount);

  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  GUI.drawSubHeader(renderer,
                    Rect{safe.x, safe.y + metrics.topPadding + metrics.headerHeight, safe.width, subHeaderHeight()},
                    section >= 0 ? sectionTitle[section] : "", pageIndicator);
}
