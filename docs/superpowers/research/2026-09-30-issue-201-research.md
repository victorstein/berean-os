# Issue #201 research — recent places store and reader entry intents

Branch `feature/201-recent-places`, at `a22af1d1` (= `main`, after #217 landed the
reader menu sheet). Every claim below was read at that commit.

## Tools and packages actually installed

| What | Evidence |
|---|---|
| PlatformIO Core 6.1.19 | `~/.platformio/penv/bin/pio --version` → `PlatformIO Core, version 6.1.19` (not on `PATH`; see `./bin/bootstrap`) |
| Platform pioarduino espressif32 55.03.37 | `platformio.ini:15` |
| ArduinoJson 7.4.2 | `platformio.ini:155` (`bblanchon/ArduinoJson @ 7.4.2`) |
| CMake 4.4.2 (host tests) | `cmake --version` |
| `.pio/` | absent in this worktree (`ls .pio` → no such file) — a first `pio run` must bootstrap |

## Who owns what today

### Nothing persists a Bible location

- `ReturnStack` is a 16-slot RAM ring of `SavedPosition{int spineIndex; int pageNumber}`
  (`src/activities/reader/ReturnStack.h:8-19`), a member of `EpubReaderActivity`
  (`EpubReaderActivity.h:69`). It is pushed only by `ReturnPolicy::Push`
  (`EpubReaderActivity.cpp:1670-1675`), i.e. citation jumps from `navigateToHref(…, true)`
  (`:1721-1722`). Its `static_assert(sizeof(SavedPosition) == 8)` (`ReturnStack.h:61-64`)
  documents that widening it is a deliberate RAM decision.
- `progress.bin` holds one position per book: spine, page, page count, visible-text offset,
  written by `saveProgress` (`EpubReaderActivity.cpp:1322-1330`) on every render whose
  spine/page changed (`:1266-1273`). That per-page write is the one #201 must **not** copy.
- `RecentBooksStore` lists books, not places: `{path, title, author}`, cap 10
  (`src/util/RecentBooksDoc.h:25`, `src/RecentBooksStore.h:12-57`).

### `goToReader` carries no target

- Signature: `void goToReader(std::string path, bool allowFastInitialRefresh = false)`
  (`src/activities/ActivityManager.h:85`), body `ActivityManager.cpp:208-230` → 
  `ReaderActivity::create(renderer, mappedInput, path, allowFast)` (`ReaderActivity.cpp:25-35`)
  → `EpubReaderActivity(renderer, mappedInput, bookPath, allowFast)` (`EpubReaderActivity.h:161-163`).
- Eight call sites, all `(path)` or `(path, bool)`: `main.cpp:584`, `main.cpp:598`,
  `Activity.cpp:15`, `ReaderActivity.cpp:97`, `LauncherActivity.cpp:422` (Resume tile),
  `LauncherActivity.cpp:431` (Bible tile, `openBible()`), `MeetingsActivity.cpp:408`,
  `PublicationsActivity.cpp:191`. A defaulted extra parameter leaves every one unchanged.
- Load order: `ReaderActivity::onEnter` → `loadBook()` (`ReaderActivity.cpp:53`) which restores
  `progress.bin` into `currentSpineIndex` / `nextPageNumber` / `cachedVisibleTextOffset`
  (`EpubReaderActivity.cpp:178-199`), then `requestUpdate()` (`ReaderActivity.cpp:61`). An intent
  applied after `loadBook()` returns overrides that restored position.

## Reader control flow — where a place is left

`navigateTo(NavTarget, ReturnPolicy)` is "the single choke point for moving the reader by
explicit user choice" (`EpubReaderActivity.h:143-145`, body `EpubReaderActivity.cpp:1656-1702`).
It takes `RenderLock` itself, so no caller may hold one. Callers:

| Site | Trigger | Section live at call time? |
|---|---|---|
| `:360` | Highlights list result | no — see below |
| `:687`, `:709` | progress-change result (bookmarks, go-to-%) | varies |
| `:741` | SELECT_CHAPTER result (book grid / TOC) | **no** — `releaseSectionKeepingPosition()` at `:723` |
| `:758` | SEARCH_BIBLE result | **no** — `releaseSectionKeepingPosition()` at `:750` |
| `:675` | `jumpToPercent` | yes |
| `:1721` | `navigateToHref` (citations, footnotes) | yes |
| `:1731` | `restoreSavedPosition` (Return swipe) | yes |

**Consequence for "record the place being left":** on the two paths that matter most (chapter
grid, search), `section` is already null when `navigateTo` runs. The left position then lives only
in `cachedSpineIndex`, `nextPageNumber` and `cachedVisibleTextOffset`, which
`releaseSectionKeepingPosition` fills (`EpubReaderActivity.cpp:249-258`) via
`rememberCurrentContentOffset` (`:1332-1337`). And `navigateTo` calls `clearDeferredReposition()`
(`:1679`), which resets `cachedVisibleTextOffset` (`:1317-1320`) — so the place must be captured
**before** that line.

Chapter changes that do **not** go through `navigateTo`:

- `pageTurn` forward past the last page / back past the first sets `currentSpineIndex±1` and
  `section.reset()` directly (`EpubReaderActivity.cpp:907-947`).
- `skipPages` (long-press chapter skip) does the same (`:949-970`).

These are the "leaving a chapter" writes. They run once per chapter boundary, not per page.

Exit paths:

- Home: `onGoHome()` → `activityManager.goHome()` (`Activity.cpp:13`), reached from the menu
  (`EpubReaderActivity.cpp:826-828`), end-of-book (`ReaderActivity.cpp:100,123`) and Back
  (`ReaderActivity.cpp:75-78`).
- Sleep: `enterDeepSleep` → `activityManager.goToSleep` (`main.cpp:242-259`) →
  `replaceActivity` → next `ActivityManager::loop()` → `exitActivity`, which calls
  `onExit()` then destroys the activity (`ActivityManager.cpp:175-181`, `:220-223`).
- `EpubReaderActivity` has **no `onExit` override**; `ReaderActivity::onExit`
  (`ReaderActivity.cpp:64-73`) runs, then `~EpubReaderActivity` (`EpubReaderActivity.cpp:126-143`),
  which already writes a final progress for the return-stack origin. `section` and `epub` are
  still alive at the top of the destructor (reset at `:133` and `:138/140`). All of these run on
  the Arduino loop task.

## Resolving a verse: `UnitIndexCache` and `StudyStore`

- `study::Unit` is `{UnitKind kind; uint8_t book; uint16_t major; uint16_t minor; uint32_t offset}`
  with `book`/`major` set only for `Verse` (`lib/StudyStore/StudyStore/Unit.h:23-33`); compact
  text form `unitToCompact` / `unitFromCompact` ("v:19:119:145:3", `Unit.h:43-47`).
- `study::resolve(const DocumentUnits&, uint32_t documentOffset)` maps a visible-text offset to
  its unit, falling back to a `DocumentOffset` unit (`UnitAnchors.h:58-61`). Inverse:
  `documentOffsetOf` (`:63-65`).
- `UnitIndexCache::unitsFor(spine)` (`src/study/UnitIndexCache.cpp:225-247`) returns a one-entry
  cache hit when `cachedSpine_ == spine`; otherwise it reads the index entry and, if absent,
  **builds the document** (streams the spine). If `!ready_` it returns an empty placeholder
  (`:228`). So it is only cheap for the spine last looked up.
- The render path already calls `STUDY.passagesInDocument(currentSpineIndex)`
  (`EpubReaderActivity.cpp:1401`), which calls `unitsFor(spineIndex)`
  (`src/study/StudyStore.cpp:215`) — so when `highlightsLoaded`, the spine on screen is the warm
  cached entry at the moment the reader leaves it.
- `UnitIndexCache` is reachable only through `StudyStore`'s **private** `units_`
  (`src/study/StudyStore.h:165`). No public "unit at (spine, offset)" exists; `isOpen()` /
  `bookFor` are not exposed either (`bookFor` is on `UnitIndexCache.h:42`). #201 needs a small
  public accessor on `StudyStore`.
- `STUDY.openPublication` is compiled only `#if BOARD_HAS_PSRAM` (`EpubReaderActivity.cpp:211-229`),
  always true on this board.

## Display reference

- `readerMenuTitle()` already builds "Isaiah 40" for a Bible from the TOC book title and
  `currentBibleChapter()` (`EpubReaderActivity.cpp:1615-1622`, helper
  `src/activities/reader/BibleReference.h:8`). `currentBibleChapter()` is `-1` until
  `resolveBibleChapterNumber()` has run for the spine (`:1591-1613`).
- The 48-byte reference cap is `study::PassageDoc::MAX_REFERENCE_BYTES`
  (`lib/StudyStore/StudyStore/PassageDoc.h:44`), applied with `utf8SafeSummary`
  (`PassageDoc.cpp:150`).

## Menu sheet (#200/#217) — where a Recent row would go

- Items come from `ReaderMenuModel::build` (`src/activities/reader/ReaderMenuModel.h:75-…`):
  up to 4 quick tiles then ≤14 rows. `ReaderMenuAction` values are cast through an int, so new
  actions are **appended** (`ReaderMenuModel.h:5-25`).
- Geometry is `ReaderMenuSheetLayout::compute` (`ReaderMenuSheetLayout.h:63-…`): title, tiles
  band, two row columns; host-tested in `test/ui_layout`. A chip row is a new band here.
- The menu returns `MenuResult{action, orientation, pageTurnOption}` to `openReaderMenu`'s
  handler (`EpubReaderActivity.cpp:277-289`). Carrying "which chip" needs either a new field or
  an action per chip.
- The sub-screens an intent maps onto already exist as menu actions: SELECT_CHAPTER →
  `BibleNavigationActivity` (book grid) (`:716-746`), SEARCH_BIBLE → `BibleSearchActivity`
  (`:747-761`), HIGHLIGHTS → `openHighlights()` (`:818-821`). Each one's cancel path reopens the
  menu (`openReaderMenu(false)`, e.g. `:737`, `:754`).

## Store discipline — the machinery to mirror

- `PersistableStore<T>` (`lib/Serialization/PersistableStore.h:157-248`): `saveToFileAtomic()`
  measures against `saveBudget()` (from `T::SAVE_BUDGET`, `:179-185`) then
  `writeDocToFileAtomic`; `loadFromFile()` uses `readDocFromFileAdopting` and sets
  `loadRefused` via `persist::loadRefusedAfter` (`:227-247`, `FormatVersion.h:22-33`), which blocks
  every later save (`:165-170`).
- `persist::isKnownFormatVersion(version, newest)` (`lib/Serialization/FormatVersion.h:14-16`):
  `doc["v"] | 0` refuses an absent version; `doc["v"] | FORMAT_VERSION` accepts legacy files.
  A new file has no legacy, so `| 0` is the right default for `places.json`.
- Fixed paths live in `lib/Serialization/SdPaths.h` with a `static_assert(isUnder(…))` each
  (`:22-57`); `/.berean/places.json` is not there yet. `test/sd_paths` exists.
- Existing per-store budgets: `CrossPointState` 2048 (`src/CrossPointState.h:41`),
  `CrossPointSettings` 4096 (`src/CrossPointSettings.h:444`), `WifiCredentialStore` 8192
  (`src/WifiCredentialStore.h:46`), `RecentBooksDoc::SAVE_BUDGET = worstCaseBytes()` derived from
  field caps (`src/util/RecentBooksDoc.h:59-69`).
- Boot loads are in `src/main.cpp:417-423` (`RECENT_BOOKS.loadFromFile()` at `:419`); a new store
  needs a line there — `main.cpp` is a shared file per `.claude/agents/data-dev.md`, so it is a
  PR hand-off, as are `test/CMakeLists.txt` and the translation YAMLs.
- Second writer: the web server protects dot-paths on its routes
  (`src/network/CrossPointWebServer.cpp:588`, `WebRouteUtils.cpp:9-10`,
  `WebDAVHandler.cpp:6`), so `/.berean/places.json` is written by the loop task only.

## Nearest existing example

**`RecentBooksStore` + `RecentBooksDoc`** is the pattern to mirror, file for file:

- `src/util/RecentBooksDoc.{h,cpp}` — Arduino-free format namespace: `FORMAT_VERSION`,
  entry cap, field caps, `worstCaseBytes()` → `SAVE_BUDGET`, `normalise`, `toJson`, `fromJson`
  that refuses an unknown version before touching the list (`RecentBooksDoc.cpp:54-76`).
- `src/RecentBooksStore.{h,cpp}` — the thin `PersistableStore<>` shell: move-to-front + trim
  (`RecentBooksStore.cpp:27-51`), `requestResave` on load normalisation (`:18-22`),
  `static_assert`s tying the budget to the doc (`:86-90`).
- `test/recent_books_doc/` — the host suite: version refusal (`RecentBooksDocTest.cpp:283-365`),
  budget measurement (`:30-50`, `:367-…`). `test/persistable_store/` shows the
  "refused file is never overwritten" test against the in-memory `HalStorageFake`
  (`PersistableStoreTest.cpp:64-131`), which is the shape the issue's `"v":2` criterion needs.

Differences from that example that the spec must settle:

1. The dedupe key is (book, chapter), which `Unit` only carries for `Verse`. A non-Bible book or a
   `DocumentOffset`/`Paragraph` document has `book == 0, major == 0`. Whether places are
   Bible-only is not stated by the issue beyond "Bible locations".
2. The place list is not per book, and a `Unit` is language-free, but `spineIndex` is a per-edition
   hint (the same caveat `StudyStore::locate` handles, `StudyStore.h:67-72`). Opening a place from
   Home needs the Bible's path, which the launcher already resolves (`LauncherActivity.cpp:94`).
3. Recording at `navigateTo` must read the left position from the cached fields when `section` is
   null, and before `clearDeferredReposition()`.

## Scope and tier

The work touches `src/` stores (data), `ActivityManager`/`goToReader` (a cross-activity contract),
`EpubReaderActivity` and the menu sheet (ui), plus shared files (`main.cpp`, `test/CMakeLists.txt`,
translations). That is a new on-disk format plus a contract change across surfaces — within
`heavy`, which is already the tier. No change to the tier.
