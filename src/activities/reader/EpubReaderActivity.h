#pragma once

#include <Epub.h>
#include <Epub/FootnoteEntry.h>
#include <Epub/HighlightDoc.h>
#include <Epub/Section.h>

#include <array>
#include <atomic>
#include <memory>
#include <optional>
#include <vector>

#include "AutoPageTurn.h"
#include "BibleBookIndex.h"
#include "EpubReaderMenuActivity.h"
#include "Place.h"
#include "ProgressMapper.h"
#include "ReaderActivity.h"
#include "ReaderBookmarks.h"
#include "ReaderEntryIntent.h"
#include "ReturnStack.h"

class EpubReaderActivity final : public ReaderActivity {
  std::shared_ptr<Epub> epub;
  std::unique_ptr<Section> section = nullptr;
  int currentSpineIndex = 0;
  int nextPageNumber = 0;
  std::optional<uint16_t> pendingPageJump;
  std::string pendingAnchor;
  int cachedSpineIndex = 0;
  int cachedChapterTotalPageCount = 0;
  std::optional<uint32_t> cachedVisibleTextOffset;
  std::optional<uint32_t> currentPageVisibleOffset;
  std::optional<uint32_t> pendingOffsetJump;
  unsigned long lastPageTurnTime = 0UL;
  int8_t pendingManualTurn = 0;
  bool pendingPercentJump = false;
  float pendingSpineProgress = 0.0f;
  bool pendingScreenshot = false;
  uint8_t pageLoadRetryCount = 0;
  static constexpr uint8_t MAX_PAGE_LOAD_RETRIES = 3;
  bool skipNextButtonCheck = false;
  int idlePrewarmSpine = -1;
  int idlePrewarmPage = -1;
  unsigned long lastRenderCompleteMs = 0;
  ReaderBookmarks bookmarks;
  // What the Bible navigator resolved, kept for this reader's life. Allocated on the first Go to in
  // a Bible, so other books and readers that never use Go to pay nothing.
  std::shared_ptr<BibleNavCache> navCache;
  AutoPageTurn autoTurn;
  bool recentsEntryRemoved = false;
  bool pendingReadFolderMove = false;
  // Consumed once by onBookLoaded(); None thereafter.
  ReaderEntryIntent entryIntent;
  // Set by the render task once a page has been drawn this session, read only under RenderLock.
  // Nothing is recorded before it: an intent's navigateTo would otherwise record the progress.bin
  // position the reader never saw.
  bool pageShown = false;
  // Whether the last render drew a reading page, unlike the session-long pageShown. Cleared by
  // reopenReaderMenu before its render, set by renderBook, both under RenderLock.
  bool pageRendered = false;

  // Gated on BOARD_HAS_PSRAM in loadBook(): a resident passage document plus
  // two live JsonDocuments are a real risk against the C3's ~50KB free heap
  // during a reading session, so non-PSRAM boards never open the study store.
  //
  // The save latch lives in StudyStore, not here: a store that failed to load
  // may still hold the user's data, and that is a property of the session
  // rather than of one book.
  bool highlightsLoaded = false;
  // Passages painted on the last rendered page's spine document. Written on
  // the render task, read by openReaderMenu under RenderLock: the menu's
  // "Tags here" count, without a StudyStore call at menu open.
  int chapterPassageCount = 0;

  // Where a long press anchored the pending selection, or -1. Lives here rather
  // than as a parameter because openHighlightPassage is also reached from the
  // reader menu and the Home-key hold, which carry no touch point.
  int pendingSelectionAnchorX = -1;
  int pendingSelectionAnchorY = -1;

  // The Bible that a non-Bible publication's tag actions open, found on first
  // use and kept for this reader; empty when there is none. Never looked up
  // inside the Bible.
  std::string bibleForTagsPath;
  bool bibleForTagsResolved = false;
  bool bibleReachableForTags();
  ReaderMenuModel::TagTarget tagTarget();
  void openBibleTags();
  // The long-press Tag command: tag here in the Bible, open the Bible's tags elsewhere.
  void runTagCommand();

  // Footnote support
  std::vector<FootnoteEntry> currentPageFootnotes;
  ReturnStack returnStack;

  uint16_t buildViewportWidth = 0;
  uint16_t buildViewportHeight = 0;
  bool partialRebuildStartFailed = false;

  // A Bible's TOC names only the book -- the chapter is the spine item, and its
  // number lives solely in the file's verse markers. Resolved once per section
  // change and kept with the spine it was read from; -1 means unknown, which is
  // also every non-Bible book.
  int bibleChapterNumber = -1;
  int bibleChapterNumberSpine = -1;
  void resolveBibleChapterNumber();
  // bibleChapterNumber when it was resolved for the current spine, else -1.
  int currentBibleChapter() const;
  // The reference ("Isaiah 40") in a Bible, the publication's title otherwise.
  bool isBible() const { return epub && epub->getBibleBookNavSpineIndex() >= 0; }
  std::string readerMenuTitle() const;

  int lastSavedSpineIndex = -1;
  int lastSavedPage = -1;
  int lastSavedPageCount = -1;

  static constexpr int BUILD_PAGES_PER_CHUNK = 8;
  static constexpr int BACKGROUND_BUILD_PAGES_PER_TICK = 2;
  static constexpr size_t BACKGROUND_BUILD_MIN_FREE_HEAP = 32 * 1024;
  static constexpr size_t BACKGROUND_BUILD_MIN_MAX_ALLOC = 16 * 1024;
  bool buildTickHeapGate();
  bool buildHeapPaused = false;
  static constexpr size_t RENDER_MIN_FREE_HEAP = 24 * 1024;
  static constexpr int BUILD_WINDOW_AHEAD = 5;
  static constexpr int PARTIAL_REBUILD_START_MARGIN = 15;
  static constexpr int BUILD_POPUP_PAGE_THRESHOLD = 20;
  static constexpr size_t BUILD_POPUP_BYTE_THRESHOLD = 96 * 1024;
  static constexpr unsigned long BUILD_POPUP_DEADLINE_MS = 1000;
  bool buildPopupPending = false;
  void showBuildPopup(GfxRenderer& renderer, int& pagesUntilFullRefresh);
  bool applyDeferredReposition();
  void clearDeferredReposition();
  void rememberCurrentContentOffset();
  // Frees the section while an overlay is up, remembering the page so the
  // cached-position rebuild restores it when the overlay is cancelled.
  void releaseSectionKeepingPosition();
  bool saveProgress(int spineIndex, int currentPage, int pageCount);
  // The Bible place the reader is on, or nullopt. The caller holds RenderLock: the unit cache is
  // shared with the render task.
  std::optional<Place> captureLeftPlace();
  // Persists a captured place. Called after the lock that captured it is released, except on exit.
  static void recordPlace(std::optional<Place> place);

  // The places behind the Recent chips of the menu that is open, in chip order.
  std::array<Place, ReaderMenuSheetLayout::MAX_RECENT_CHIPS> recentShown;
  int recentShownCount = 0;
  // Fills recentShown from PLACES, skipping the chapter on screen, and returns the chip labels.
  ReaderMenuSheetLayout::RecentChipLabels collectRecentChips(const std::optional<Place>& onScreen);
  void openRecentPlace(int index);
  void jumpToPercent(int percent);
  void onReaderMenuConfirm(const MenuResult& menu);
  // pageOnScreen: the framebuffer holds the reading page, so the sheet may be
  // drawn over it. A sub-screen's cancel decides it through reopenReaderMenu.
  void openReaderMenu(bool pageOnScreen);
  // Cancel from a sub-screen the sheet opened: redraw the page, then reopen the
  // sheet over it. Loop task only, with no RenderLock held (requestUpdateAndWait).
  void reopenReaderMenu();
  void openHighlightPassage();
  // Long-press a word to anchor a selection there. Suppressed inside the centre
  // menu zone, where a long contact would be ambiguous with the menu tap.
  void openHighlightPassageAt(int touchX, int touchY);
  void toggleAutoPageTurn(uint8_t selectedPageTurnOption);
  void addBookmark();

  // What a navigation does to the return stack. Clear is the default so a new
  // navigation feature cannot strand Back on a position the user has left.
  enum class ReturnPolicy : uint8_t { Clear, Push, Preserve };
  // ReuseIfSameSpine keeps a live section and only moves its page. It is legal
  // only without an anchor or a percent jump, which are consumed during a
  // section build and would otherwise leak into the next chapter's.
  enum class SectionMode : uint8_t { Reset, ReuseIfSameSpine };

  // Where cancelling a picker returns: the menu it was opened from, or the page (an entry intent).
  enum class CancelTo : uint8_t { Menu, Page };
  void openChapterPicker(CancelTo cancelTo);
  void openBibleSearch(CancelTo cancelTo);
  // With a spine, only that chapter's passages ("Tags here").
  void openHighlights(CancelTo cancelTo, std::optional<uint16_t> spineFilter = std::nullopt);
  void onBookLoaded() override;

  struct NavTarget {
    int spineIndex;
    int pageNumber = 0;
    std::optional<uint32_t> offsetJump;
    std::string anchor;
    std::optional<float> spineProgress;
    SectionMode sectionMode = SectionMode::Reset;
  };

  // The single choke point for moving the reader by explicit user choice.
  // Takes RenderLock itself, so no caller may hold one.
  void navigateTo(NavTarget target, ReturnPolicy policy = ReturnPolicy::Clear);
  void navigateToHref(const std::string& href, bool savePosition = false);
  void restoreSavedPosition();

  void renderContents(std::unique_ptr<Page> page, int orientedMarginTop, int orientedMarginRight,
                      int orientedMarginBottom, int orientedMarginLeft);
  void renderStatusBar() const;
  void applyOrientation(uint8_t orientation);

  bool loadBook() override;
  std::string getBookTitle() const override { return epub ? epub->getTitle() : ""; }
  std::string getBookAuthor() const override { return epub ? epub->getAuthor() : ""; }
  void renderBook() override;
  void onEndOfBookRendered() override;

 public:
  explicit EpubReaderActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string bookPath,
                              bool allowFastInitialRefresh, const ReaderEntryIntent& intent = {})
      : ReaderActivity("EpubReader", renderer, mappedInput, std::move(bookPath), allowFastInitialRefresh),
        entryIntent(intent) {}
  ~EpubReaderActivity() override;

  void loop() override;
  void onExit() override;

  bool pageTurn(bool isForward) override;
  bool skipPages(int amount) override;
  bool isAtEndOfBook() const override;
  void onReturnFromEndOfBook() override;

  bool skipLoopDelay() override;

  ScreenshotInfo getScreenshotInfo() const override;
  CrossPointPosition getCurrentPosition() const;
};
