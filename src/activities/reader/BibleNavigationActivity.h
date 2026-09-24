#pragma once
#include <Epub.h>
#include <Epub/VerseAnchors.h>

#include <atomic>
#include <memory>
#include <optional>
#include <vector>

#include "BibleBookNameTable.h"
#include "BookGridLayout.h"
#include "NumberGridLayout.h"
#include "activities/UiListActivity.h"

// Book -> chapter -> verse drill-down for NWT-shaped Bible publications, whose
// chapters run past 50 verses while a page holds four to six: reaching a known
// reference through the flat TOC costs a dozen page turns.
//
// One activity walks all three levels rather than three nested ones. Nesting
// would keep three activities and three row-buffer sets resident and would
// hand-propagate the verse result up two intermediate handlers.
//
// Every level is a paged grid. The book level shows the publication's own
// abbreviations from biblebooknav.xhtml, one page per testament heading; the
// chapter and verse levels are number grids, so a high reference costs pages
// instead of screens. Tap or Confirm a chapter to list its verses.
class BibleNavigationActivity final : public UiListActivity {
 public:
  BibleNavigationActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, const std::shared_ptr<Epub>& epub);
  void onEnter() override;

 private:
  enum class Level : uint8_t { Book, Chapter, Verse };

  static constexpr int MAX_BOOKS = BibleBookNameTable::MAX_BOOKS;
  static constexpr int MAX_CHAPTERS = 150;  // Psalms
  static constexpr int BOOK_NAME_BYTES = BibleBookNameTable::NAME_BYTES;
  // The publication's abbreviation for each book, cell labels at the book level.
  static constexpr int BOOK_ABBREV_BYTES = 16;
  // Room for a full book name, a space and a chapter number.
  static constexpr int HEADER_TITLE_BYTES = BOOK_NAME_BYTES + 8;
  static constexpr int MAX_GRID_CELLS = NumberGrid::MAX_CELLS;
  // "176" plus its NUL: no chapter or verse number reaches four digits.
  static constexpr int CELL_LABEL_BYTES = 4;

  std::shared_ptr<Epub> epub;
  Level level = Level::Book;

  // Only the display name and the resolved spine target are kept: chapter rows
  // are literally 1..N, so no hrefs need storing past the one sweep that
  // resolved them.
  BibleBookNameTable bookNames;
  int16_t bookTargetSpine[MAX_BOOKS] = {};
  // The five single-chapter books (Obadiah, Philemon, 2-3 John, Jude) have no
  // chapter-nav page; their row points straight at the chapter spine item.
  bool bookIsDirect[MAX_BOOKS] = {};
  int bookCount = 0;
  char bookAbbrev[MAX_BOOKS][BOOK_ABBREV_BYTES] = {};
  char sectionTitle[BookGrid::MAX_SECTIONS][BOOK_NAME_BYTES] = {};
  int sectionStart[BookGrid::MAX_SECTIONS] = {};
  int sectionCount = 0;
  // Written by the render task only when its inputs change; the loop task
  // reads it under RenderLock to page and to step by a row.
  BookGrid::Layout bookLayout{};
  int bookLayoutWidth = 0;
  int bookLayoutHeight = 0;
  int bookLayoutBooks = 0;
  uint8_t bookLayoutGeneration = 0;
  // Bumped as loadBooks() finishes. loadBooks() runs on the loop task after
  // the first render is already requested, and holding RenderLock across its
  // SD reads is not an option, so a render can catch it halfway; keying the
  // layout cache on this rebuilds anything built from partial data. Atomic so
  // the bump is ordered after the book data it publishes.
  std::atomic<uint8_t> booksLoadedGeneration{0};
  char headerTitle[HEADER_TITLE_BYTES] = {};
  int selectedBook = -1;

  int16_t chapterSpine[MAX_CHAPTERS] = {};
  int chapterCount = 0;
  // Row the verse list was opened from, or -1 when it came straight off a
  // single-chapter book -- which is also what Back from the verse level reads
  // to know whether a chapter level sits underneath it.
  int selectedChapterRow = -1;

  std::vector<VerseAnchors::VerseAnchor> verseAnchors;
  int verseSpine = -1;

  // `grid` carries the last number-grid build's geometry, which the loop task
  // reads to page and to step the selection by a row.
  freeink::ui::KeyGridKey cells[MAX_GRID_CELLS] = {};
  char cellLabels[MAX_GRID_CELLS][CELL_LABEL_BYTES] = {};
  NumberGrid::Geometry grid{};
  void buildGrid(UiScreen& screen);
  // Page arithmetic that differs by level: the book level pages by section
  // (bookLayout), the number levels by NumberGrid's uniform pages.
  int gridPageCount() const;
  int gridPageOf(int index) const;
  int gridPageFirst(int page) const;
  int gridCellsPerPage() const;
  const char* cellLabel(int row, int cell);
  // Grid top inset: the book level adds a section sub-header below the title.
  int subHeaderHeight() const;
  void rebuildBookLayout(int width, int height);
  // The selection moves below always bring their page with them. The base
  // moveSelectionTo pulls a sliding row window instead, which would leave
  // nav.top off a page boundary.
  //
  // The page `delta` pages away, optionally wrapping at either end.
  void moveGridPage(int delta, bool wrap);
  // One row down (direction > 0) or up, keeping the column across a page.
  void moveGridRow(int direction);
  // Held-button paging at the number levels: ButtonNavigator's wrapping pages,
  // or single cells when everything fits on one page.
  void moveNumberPage(int direction);
  // Caller holds RenderLock: renderingMutex is not re-entrant, so nothing
  // called from here may lock again.
  void placeSelectionLocked(int index);
  int lastSelectableIndex() const;

  bool loadBooks();
  bool loadChapters(int bookIndex);
  bool loadVerses(int spineIndex);
  // Enter `next`, placing the selection on `selected` (clamped into that
  // level's rows) and its page in view.
  void enterLevel(Level next, int selected);
  void openVerseList(int spineIndex, int chapterRow);
  void finishWith(int spineIndex, std::optional<uint32_t> offsetJump);
  void cancel();

  int listCount() const override;
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  // Swipes page the grid by a whole page; the base scrolls by rows.
  bool handleCustomInput() override;
  // Every level steps the selection by a grid row and pages on a held button;
  // the base steps by one list row either way.
  void navigateButtons() override;
  void onBackButton() override;
  // Header is drawn inside the safe area (not full-width like the base).
  void drawChrome() override;
  // Also paints the book level's section sub-header. The base draws chrome
  // before the build, so only here is this render's bookLayout guaranteed
  // current -- on the very first render drawChrome would see no layout at all.
  void drawFooter() override;
};
