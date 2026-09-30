# Issue #199 research: square 7×10 chapter grid with tag and bookmark markers

Branch `feature/199-chapter-grid-markers`, based on `67eaaf1e` (release 1.21.0, after #197's
compact metrics). Every claim below was read at that commit; line numbers are from this worktree.

## 1. Which files own the behaviour

| Concern | File | Lines |
|---|---|---|
| Number-grid geometry and paging (pure, host-tested) | `src/activities/reader/NumberGridLayout.h` | 12-21 constants, 34-41 `geometryFor`, 44-49 `cellSizeFor` |
| Book-level layout, which **also** reads `NumberGrid::MAX_CELLS` | `src/activities/reader/BookGridLayout.h` | 91 |
| The navigator (book → chapter → verse) | `src/activities/reader/BibleNavigationActivity.{h,cpp}` | `.h:46,96-98` cell buffers; `.cpp:417-490` `buildGrid` |
| Where the reader constructs the navigator | `src/activities/reader/EpubReaderActivity.cpp` | 710-739 (`SELECT_CHAPTER`, line 724) |
| Interaction-table capacity for every FUI screen | `src/components/UiAppHost.h` | 29-36 (`MAX_INTERACTIONS = 64` at 35) |
| Overflow log | `src/components/UiAppHost.cpp` | 17-27 |
| `keyGrid` and `KeyGridKey.secondaryLabel` | `freeink-sdk/libs/ui/FreeInkUI/include/components/keyboard/key-grid.h` | 24-33, 52-100 |
| Bookmarks resident in the reader | `src/activities/reader/ReaderBookmarks.h` | 39 `empty()`, 46 private `cachedBookmarks_` |
| Bookmark record | `src/activities/reader/BookmarkEntry.h` | 11 `computedSpineIndex`, 18-19 `visibleTextOffset` |
| Tagged passages resident in the reader | `src/study/StudyStore.h` | 88 `passages()`, 90 `isOpen()`, 203 `STUDY` |
| Passage address | `lib/StudyStore/StudyStore/Unit.h` | 25-33; `TaggedPassage.h:31-45` |
| Host tests | `test/number_grid/{NumberGridLayoutTest,BookGridLayoutTest,BibleEntryPositionTest}.cpp`, `test/number_grid/CMakeLists.txt` | — |

Note two path corrections to the issue text: `key-grid.h` lives under `components/keyboard/`, not
`components/`, and `MAX_INTERACTIONS` is `UiAppHost.h:35` (the rationale comment is 29-34).

## 2. Current control flow

### Opening the navigator
`EpubReaderActivity.cpp:710-727`: on `SELECT_CHAPTER` the reader copies `currentSpineIndex`,
calls `releaseSectionKeepingPosition()`, and, when `epub->getBibleBookNavSpineIndex() >= 0`,
constructs `BibleNavigationActivity(renderer, mappedInput, epub, spineIdx)` and starts it for a
result. The reader activity stays alive underneath, so its `bookmarks` member
(`EpubReaderActivity.h:43`, a `ReaderBookmarks`) and the `STUDY` singleton stay resident.
`bookmarks.load(...)` runs at `EpubReaderActivity.cpp:208` and `:674`; `STUDY.openPublication`
at `:215` (its return value is kept in `highlightsLoaded`, so the store can be closed/empty — use
`STUDY.isOpen()`, `StudyStore.h:90`).

The constructor only stores `epub` and `entrySpine` (`BibleNavigationActivity.cpp:33-37`). No
bookmark or passage data reaches the navigator today.

### Building a chapter or verse page
`buildScreen` (`.cpp:398-415`) sets the content margin to safe area + `topPadding` +
`headerHeight` (+ the book-level section band) and adds a `verticalSpacing` spacer, then
`buildGrid` (`.cpp:417-490`):

1. `grid = NumberGrid::geometryFor(body.width, body.height)` (`:439`).
2. Page arithmetic via `pageStartFor` / `cellsOnPage` (`:448-449`).
3. Fills `cells[0..cellsPerPage)` (`KeyGridKey`), label from `cellLabel()` (`:529-535`,
   `row + 1` for chapters, `verseAnchors[row].verse` for verses), `value` = absolute row,
   padding cells `Disabled` (no interaction registered).
4. `props.selectedIndex = pageRelativeIndex(nav.selected, …)` (`:478`) — **this is the
   "current chapter is inverted" behaviour**: `enterAtPosition()` (`:157-181`) selects the
   reader's chapter on entry.
5. `props.minTouchSize = cellSizeFor(body…)` (`:484-485`) so hit rects never outgrow the cell.
6. `fui::keyGrid(screen.frame(), body, props)` (`:489`) — **with the whole `body` rect**.

`keyGrid` (`key-grid.h:54-55`) computes `cellW` and `cellH` independently from the rect, so cells
stretch to fill it: the non-square slabs the issue describes. `NumberGrid::cellSizeFor` already
returns `min(cellW, cellH)` (`NumberGridLayout.h:44-49`), but it is only used for
`minTouchSize`, never to size the rect handed to `keyGrid`. Squaring and centring therefore needs
no SDK change: pass `keyGrid` a rect of `cols*cell + (cols-1)*GAP` by `rows*cell + (rows-1)*GAP`,
centred in `body`.

### Geometry today
`geometryFor` (`NumberGridLayout.h:34-41`): `stride = MIN_CELL(56) + GAP(8) = 64`;
`cols = clamp((W+8)/64, 4, 8)`; `rows = min(max((H+8)/64, 1), MAX_CELLS/cols)`. With
`MAX_CELLS = 48` and 7 columns, rows cap at 6 → 42 per page (the test fixture's
`PORTRAIT_W = 480`, `test/number_grid/NumberGridLayoutTest.cpp:15-16`).

The test's `PORTRAIT_H = 695` predates #197. Computed from current source (not measured on
device): Lyra `topPadding 5`, `headerHeight 44`, `verticalSpacing 8`
(`src/components/themes/lyra/LyraTheme.h:11-14`); on touch boards `buttonHintsHeight` is zeroed
(`src/components/UITheme.cpp:53-56`), and `getScreenSafeArea` subtracts it only in portrait with
front hints (`UITheme.cpp:90-93`). That puts the chapter body at roughly 480 × ~743. Any height
≥ 632 yields 10 rows at stride 64 (`(632+8)/64 = 10`), so 7 × 10 needs only the cap raised; the
exact body height is a device-log item for the spec.

Square cell at 7 × 10 (computed): `min((480-6*8)/7, (H-9*8)/10)` = `min(61, …)`; for H = 743
that is 61 px, above the 56 px `MIN_CELL` floor (`NumberGridLayout.h:20-21`). If H were as low
as 632 the cell would be exactly 56.

### Page counts (arithmetic, from `pageCount`, `NumberGridLayout.h:51-54`)

| | 48-cap (42/page at 7 cols) | 70/page |
|---|---|---|
| Genesis 50 | 2 | 1 |
| Isaiah 66 | 2 | 1 |
| Psalms 150 | 4 | 3 |
| Psalm 119 (176 verses) | 5 | 3 |

(The existing test `Psalm119TakesFourPages`, `NumberGridLayoutTest.cpp:89`, uses 48 per page,
the landscape-era cap, not the 42 the portrait device actually shows.)

### Why the cap exists
`NumberGridLayout.h:15-19`: past `UiAppHost::MAX_INTERACTIONS` FreeInkUI drops hit rects
silently; `UiAppHost.cpp:23-25` logs `Interaction table overflowed`. `UiAppHost.h:29-34`:
"double-buffered and an Interaction is 16 B, so each slot costs 32 B -- 2 KB per live host at 64".
64 → 96 is +32 slots × 32 B = **+1,024 B per live `UiAppHost`**. `UiApp` is a single shared
template instantiation (`UiAppHost.h:24-27,36`), so the flash cost is one re-instantiation, not
per screen. Every FUI screen pays the RAM (`UiListActivity : … protected UiAppHost`,
`src/activities/UiListActivity.h:17`).

Where that RAM lands: `UiApp app` is a by-value member of the heap-allocated activity.
`docs/superpowers/reviews/issue-28-spec-review-0.md:95-105` records the framework sdkconfig as
`CONFIG_SPIRAM_USE_MALLOC=y`, `CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=4096` (also cited in
`src/study/PsramJsonAllocator.h:9`), so any activity object over 4,096 B is routed to PSRAM by
`malloc`. `BibleNavigationActivity` alone carries `bookNames` (66 × 48 B,
`BibleBookNameTable.h:21,25,41`) plus `bookAbbrev` (66 × 16 B) — already past 4 KB — so its
+1 KB most likely lands in PSRAM, not internal SRAM. Smaller FUI activities under 4 KB would pay
internal SRAM. This is secondhand (from that review doc) and must be confirmed with the
acceptance criterion's internal-heap log.

### Side effect: the book grid shares the cap
`BookGridLayout.h:91`: `rows = max(1, min({rowsNeeded, rowsFit, NumberGrid::MAX_CELLS / cols}))`.
Raising `MAX_CELLS` to 70 changes book-level rows wherever the cap was the binding term.
`BookGridLayoutTest.cpp` test `CellCapLimitsRowsWhenManyColumnsFit` asserts `rows == 8`
(= 48/6); at 70 it becomes `min(11, 31, 11) = 11`, so that test must be updated, and the spec
must decide whether the book level is meant to change (the issue lists the book grid only as the
optional dot). The measured Hebrew-Scriptures case (`FourColumnHebrewScripturesNeedTheFullPortraitBody`)
has `rows = 10 ≤ 48/4 = 12`, so it is unaffected.

## 3. Marker data: what is available and how it maps

### Tagged passages
`STUDY.passages()` returns `const std::vector<study::TaggedPassage>&` (`StudyStore.h:88`).
Each has `Unit start, end` (`TaggedPassage.h:32-33`). A `Verse` unit carries
`book` = canonical 1-66 = **position in biblebooknav.xhtml** (`Unit.h:9-15,27`),
`major` = chapter, `minor` = verse (`Unit.h:28-29`). The navigator's book index comes from the
same page: `loadBooks()` streams `getBibleBookNavSpineIndex()` and indexes books in link order
(`BibleNavigationActivity.cpp:65-72`). So navigator book `i` ↔ `Unit.book == i + 1`, and chapter
row `r` ↔ `Unit.major == r + 1` (`cellLabel`, `.cpp:531-532`). Verse cell `i` ↔
`verseAnchors[i].verse` (`VerseAnchors.h:19-23`, which also carries `chapter`).

Non-Verse passages (`Paragraph`, `DocumentOffset`) carry `book = 0` (`Unit.h:27`) and never mark
a chapter. A passage whose `start` and `end` are in different chapters raises the question of
marking only the start chapter or every chapter in `[start, end]` — spec decision.

`STUDY` is a singleton accessed on whichever task calls it; the navigator's grid build runs on
the render task (`buildGrid` is called from `buildScreen`). Reading the vector there while a
sub-activity could mutate it is not a live risk here (the navigator has no editing path), but
the issue's "build the bitset when the grid is built, store nothing" should be settled against
threading in the spec — computing it on the loop task in `loadChapters()` / `loadVerses()` and
only reading it in `buildGrid` mirrors how `chapterSpine` is already produced.

### Bookmarks
`ReaderBookmarks::cachedBookmarks_` is private (`ReaderBookmarks.h:46`); the only public
accessors are `empty()` and `currentPageBookmarked()` (`:38-39`). Each `BookmarkEntry` has
`computedSpineIndex` (`BookmarkEntry.h:11`), always written on add
(`ReaderBookmarks.cpp:122`) and already trusted as a jump target
(`EpubReaderBookmarksActivity.cpp:102-104`).

- Chapter level: bookmark spine → the row `r` with `chapterSpine[r] == computedSpineIndex`
  (`BibleNavigationActivity.h:84`, filled by `loadChapters`, `.cpp:136-138`). This is the same
  lookup `BibleEntry::chapterRowFor` already performs for the entry position
  (`.cpp:174`, `BibleEntryPosition.h`).
- Verse level: `visibleTextOffset` (`BookmarkEntry.h:18-19`) and `VerseAnchor.offset` are both
  consumed as `offsetJump` by the reader (`EpubReaderActivity.cpp:679-683` for a picked
  bookmark, via `EpubReaderBookmarksActivity.cpp:98-102`; `:735` for the navigator's verse result), so they share one coordinate space: a bookmark in
  `verseSpine` marks the last anchor with `offset <= visibleTextOffset`. Pre-offset bookmarks
  (`hasVisibleTextOffset == false`) can only mark the chapter.

The bookmark data crosses an activity boundary. The two options the issue names — a const
accessor on `ReaderBookmarks` or a precomputed set passed into the navigator constructor
alongside `spineIdx` (`EpubReaderActivity.cpp:724`) — are both local to `src/activities/reader`.
The constructor option keeps the navigator from holding a reference into the reader.

### Size of the marker set
1,189 chapters (the NWT count, `Unit.h:19-20`) → a 1,189-bit set is 149 B. The navigator only
ever shows one book at a time, so a per-book `MAX_CHAPTERS = 150` bitset (19 B,
`BibleNavigationActivity.h:40`) plus one for the verse page (≤ 176 verses → 22 B) also works;
the spec picks one. Both fit in the activity object; no new heap allocation is needed.

## 4. Rendering the markers: constraints found

1. **`secondaryLabel` draws in the top-right third of the cell** (`key-grid.h:93-97`: rect
   `cellW/3` × `cellH/2` at the right edge), only when `key.enabled`, with one
   `props.secondaryText` style for every key. This matches the mockup's corner placement.
2. **The current (selected) cell is filled black**: `defaultKeyStyles()` = `defaultButtonStyles()`
   (`freeink-sdk/libs/ui/FreeInkUI/src/FreeInkUI.cpp:19-35,58-61`), `selected.background = Black`,
   `selected.foreground = White`. `secondaryText` is a fixed `TextStyle` (default
   `color = Color::Black`, `FreeInkUICore.h:534-544`) that does not follow the key state, so a
   marker on the current chapter would draw **black on black and vanish**. The acceptance
   criteria require the current chapter to stay inverted *and* markers to show, so this needs
   either an SDK change to `key-grid.h` (state-aware secondary ink, as the Space glyph already
   does at `key-grid.h:85-91` via `bp.styles.resolve(...)`) or activity-side drawing after
   `keyGrid`. `freeink-sdk` is our fork (memory: SDK edits go to `victorstein/freeink-sdk`
   `berean` plus a gitlink bump).
3. **`⌂` (U+2302) is in no built-in font.** Scanning every `lib/EpdFont/builtinFonts/*.h`
   interval table: U+2022 `•` is present in 37 fonts (all Ubuntu UI and Noto sizes); U+2302 is
   present in **0**. Rendering `⌂` as text would fall to the SD fallback font path
   (`renderer.prewarmFallbackText`, used at `BibleNavigationActivity.cpp:506-512`) or render
   nothing. Existing bookmark art is bitmap: `BookmarkStatusIcon`
   (`src/components/icons/bookmark.h:15`, drawn by `BaseTheme.cpp:34-41,678`) and
   `icon_bookmark_24` / `_32` (`src/components/icons/listIcons.h:213`,
   `src/components/UiAppHelpers.h:101,124`). `KeyGridKey.icon` exists (`key-grid.h:27`) but
   `button()` draws it as the key's main glyph, not in the corner.
4. The mockup (design artifact, "Bible chapter grid" pair) shows both markers side by side in the
   top-right corner (`•⌂`), current chapter inverted, and a legend line under the grid
   ("• tagged passage ⌂ bookmark"). A legend would be new UI text (`tr()` keys = shared
   translation files, hand-off only).

## 5. Tooling actually installed

```
$ ~/.platformio/penv/bin/pio --version
PlatformIO Core, version 6.1.19
$ grep -n '^platform' platformio.ini
15:platform = https://github.com/pioarduino/platform-espressif32/releases/download/55.03.37/platform-espressif32.zip
$ cmake --version | head -1
cmake version 4.4.2
$ c++ --version | head -1
Apple clang version 21.0.0 (clang-2100.0.123.102)
$ git submodule status freeink-sdk
 67f7e012e3554106062bc52a3131523339a8ac4e freeink-sdk (67f7e01)
```

`pio` is not on PATH in this worktree; `./bin/bootstrap` first (project memory, fresh-worktree
bootstrap).

## 6. Nearest existing example

- **`7186b796` — "page Bible chapters and verses as number grids (#15)"** is the direct
  predecessor: it introduced `NumberGridLayout.h`, `test/number_grid/`, the 48-cell cap and the
  64-slot `MAX_INTERACTIONS` rationale (`UiAppHost.h` +13), and touched only
  `BibleNavigationActivity.{h,cpp}`, `UiAppHost.{h,cpp}`, the header and its tests. This issue
  is a revision of exactly that change set, and its tests are the model for the new ones.
- **Host-tested mapping from spine/chapter data**: `BibleEntryPosition.h` +
  `test/number_grid/BibleEntryPositionTest.cpp` (`chapterRowFor(chapterSpine, count, spine)`) is
  the pattern the marker-set mapping should mirror: a FreeInkUI-free header in
  `src/activities/reader/`, tested in `test/number_grid/`.
- New test executables go in **`test/number_grid/CMakeLists.txt`** (a surface-local file, which
  already holds three executables), not the shared `test/CMakeLists.txt`; that file already has
  `add_subdirectory(number_grid)` (`test/CMakeLists.txt:101`).

## 7. Tier

Stays `standard`. Every file is on the `ui` surface (`src/activities/reader`,
`src/components/UiAppHost.h`); `STUDY` and `BookmarkEntry` are read, not changed; no on-disk
format or store is touched. The one cross-boundary risk is §4.2: if the spec chooses a
state-aware secondary label in `key-grid.h`, that is a `freeink-sdk` fork commit plus a gitlink
bump, which is the FreeInkUI layer `ui-dev` owns — to be weighed in the spec against drawing the
marker from the activity.

## 8. Open questions for the spec

1. `⌂` has no glyph: bitmap icon in the corner, a different glyph that exists, or add U+2302 to
   the UI font.
2. Marker on the inverted current cell: SDK `key-grid.h` change vs activity-side overdraw.
3. Should raising `MAX_CELLS` change the book grid (§2 side effect), or should the book level keep
   its own 48 cap?
4. Multi-chapter passages: mark the start chapter only, or every chapter spanned.
5. Bitset scope: whole-Bible 149 B vs per-book/per-page, and which task builds it.
6. A legend line under the grid (mockup) means new `tr()` keys.
