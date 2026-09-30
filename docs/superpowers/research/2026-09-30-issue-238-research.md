# Issue #238 research: Go to is slow to open the chapter grid

Base: `perf/238-go-to-latency` at `58546aaf` (= `origin/main` after `git fetch`, release 1.29.2).
Nothing below has been timed on the device yet. The issue's own first step is to measure, and
this note records the mechanism of each candidate cost, not how long it takes. The ranking at
the end is a hypothesis for the dev-build measurement to confirm or overturn.

## Installed tools

| Tool | Version | Evidence |
|---|---|---|
| PlatformIO Core | 6.1.19 | `~/.platformio/penv/bin/pio --version` (not on `PATH`) |
| Platform | pioarduino `platform-espressif32` 55.03.37 | `platformio.ini:15` |
| Arduino core | `framework-arduinoespressif32` 3.3.7, libs `5.5.0+sha.87912cd291` | `~/.platformio/packages/*/package.json` |
| Toolchain | `toolchain-xtensa-esp-elf` 14.2.0+20251107 | same |
| Host tests | CMake 4.4.2, Apple clang 21.0.0 | `cmake --version`, `c++ --version` |

## The two entry points

**Reader menu → Go to.** The sheet's `SELECT_CHAPTER` result reaches
`onReaderMenuConfirm` → `openChapterPicker(CancelTo::Menu)`
(`src/activities/reader/EpubReaderActivity.cpp:729-731`).

**Home → Go to.** `HomeTargets::route(Target::GoTo)` → `reader(Kind::GoTo)`, with
`allowFastInitialRefresh = false` (`src/activities/launcher/HomeTargets.h:55-56`).
`LauncherActivity::openReader` → `activityManager.goToReader(biblePath, …, intent)`
(`LauncherActivity.cpp:429-445`) → `replaceActivity(EpubReaderActivity)`. So a Home Go to
**builds a whole new reader** first. `ReaderActivity::onEnter` (`ReaderActivity.cpp:42-64`) runs
`sdFontSystem.ensureLoaded`, `loadBook()`, then `APP_STATE.saveToFileAtomic()` and
`RECENT_BOOKS.addBook(...)` (both write to SD), then `onBookLoaded()`. `loadBook`
(`EpubReaderActivity.cpp:155-240+`) loads `book.bin`, reads `progress.bin`, loads bookmarks,
calls `STUDY.openPublication(epub, renderer)` and `STUDY.repairTexts()`. `onBookLoaded`
routes `ChapterGrid` → `openChapterPicker(CancelTo::Page)` (`:957-963`,
`ReaderEntryIntent.h:41`).

The consequence for the acceptance criterion "a second Go to in the same session is faster":
from Home, every Go to constructs a new `EpubReaderActivity` and a new `Epub`
(`loadBook`, `:156`), and both are destroyed on the way home (`~EpubReaderActivity`, `:134-152`).
A cache owned by the reader or by the `Epub` object survives a menu Go to, but not
Home → Go to → Home → Go to.

## Control flow from tap to first grid frame (menu path)

1. The menu finishes and `ActivityManager::loop` pops it. Its handler runs `onReaderMenuConfirm`,
   which calls `openChapterPicker`, and that `push`es the navigator. Because a push is now pending,
   no reader repaint is requested (`ActivityManager.cpp:119-136`).
2. `openChapterPicker` (`EpubReaderActivity.cpp:848-874`):
   - `releaseSectionKeepingPosition()` (`:258-267`) only records the position and calls
     `section.reset()`. That is cheap on the way in. The cost falls on the **return**, when the
     reader rebuilds the section, and the return is outside the tap-to-grid window this issue
     targets.
   - `isBible()` is `epub->getBibleBookNavSpineIndex() >= 0` (`EpubReaderActivity.h:94`), and the
     result is memoised on the `Epub` (`lib/Epub/Epub.h:33-36`, `Epub.cpp:980-985`).
   - Constructing `BibleNavigationActivity` copies the bookmarks into a heap array
     (`BibleNavigationActivity.cpp:38-54`).
3. The same loop iteration moves the reader onto the stack and calls
   `BibleNavigationActivity::onEnter()` **on the loop task** (`ActivityManager.cpp:138-162`).
   `onEnter` (`BibleNavigationActivity.cpp:56-81`):
   1. `UiListActivity::onEnter` → `requestUpdate()`, which is deferred: it only sets
      `requestedUpdate` (`UiListActivity.cpp:20-27`, `ActivityManager.cpp:285-295`). The render
      task is notified at the end of `ActivityManager::loop` (`:166-173`), **after every step
      below**.
   2. `fcm->clearCache()` frees the font cache.
   3. Masthead: `epub->getThumbBmpPath(Masthead::thumbHeight(renderer))`, then `Masthead::fits`
      (`Masthead.cpp:51-60`): `Storage.exists`, plus `CoverThumb::sizeOf`, which opens the BMP
      header. **Nothing here generates a thumbnail** (`BibleNavigationActivity.h` comment on
      `mastheadCover`; `Masthead::fits` never calls `CoverThumb::pathFor`).
   4. `loadBooks()` (`:83-137`), detailed below.
   5. `enterAtPosition()` (`:229-253`) → `loadChapters(book)` (`:139-171`) →
      `markChapters` (`:193-206`), then `enterLevel(Chapter, row)`, which takes `RenderLock` and
      calls `requestUpdate()` again.
   6. `LOG_INF` of internal and PSRAM heap (`:76-80`). This is already the "free internal heap
      logged at Go to" that the acceptance criteria ask for, and a baseline exists for it.
4. The render task runs `UiListActivity::render` (`UiListActivity.cpp:154-171`): `clearScreen`,
   then `drawChrome` (which returns early when there is a masthead, `BibleNavigationActivity.cpp:653-656`),
   then `renderUi` → `buildScreen` → `buildGrid` (`:469-586`), with up to 8 rebuild passes, then
   `drawFooter` → `Masthead::draw` (`:675-677`), then `displayBuffer(FAST or HALF)`.

### Cost 2a: `loadBooks()` — two SD walks over `book.bin` per Go to

- `SpineHtmlStream::stream(epub, bookNavSpine, …)` (`SpineHtmlStream.cpp:19-82`) reads the cached
  `biblebooknav.xhtml` HTML in 2 KB chunks through the `BibleNav::Scanner`. When no HTML cache
  exists, it inflates the page from the zip under `RenderLock` with a `FrameBufferLoan`
  (`:28-49`). The nav page is 6,344 B in `nwt_S.epub`, under the 32 KB popup threshold
  (`SpineHtmlStream.h:22`).
- `epub->resolveFilenamesToSpineIndices(targets, …, bookCount)` (`Epub.cpp:963-978`) walks the
  spine from index 0 and calls `getSpineItem(i)` until every target has resolved. Each
  `getSpineItem` → `BookMetadataCache::getSpineEntry` (`BookMetadataCache.cpp:508-525`) does a
  **seek, a 4-byte LUT read, a second seek, and `readSpineEntryFrom`**: a length-prefixed string
  into a fresh `std::string`, plus two PODs (`:46-52`). Every one of those reads is a `HalFile` call
  that takes `storageMutex`.
- `bookNames.joinToc(*epub, targets, bookCount)` (`BibleBookNameTable.cpp:53-66`) calls
  `epub.getTocItem(i)` for **every TOC entry**. Each is again seek → LUT → seek → three strings
  and two PODs (`BookMetadataCache.cpp:527-544, 55-63`).

Measured on the user's own copy of the Spanish NWT (`~/Downloads/nwt_S.epub`, 14,945,282 B), with
a Python `zipfile` pass over its OPF, `toc.xhtml` and nav pages:

| Quantity | Value |
|---|---|
| Spine items | 3,937 |
| `<nav epub:type="toc">` links (`toc.xhtml`) | 196 (129 in the user's TOC-trimmed `x4pro-upload/nwt_S.epub`) |
| `biblebooknav.xhtml` spine index | 2 |
| `biblechapternav1.xhtml` (Genesis) … `biblechapternav66.xhtml` (Revelation) spine index | 29 … 1,321 |
| Revelation's last chapter spine index | 1,343 |

So `loadBooks` alone does **~1,322 spine entry reads**, because the book targets resolve only once
the walk reaches `biblechapternav66` at 1,321, plus **196 TOC entry reads**. That is ~1,500
seek-and-read-string round trips, and on the order of 7,000 `HalFile` calls, each taking the
mutex. All of it happens before the render task is even notified.

### Cost 2b: `loadChapters(book)` — a third walk, scaled by the book's position

`loadChapters` streams the book's `biblechapternavN.xhtml` (2,437 B for Revelation), then calls
`resolveFilenamesToSpineIndices` again for its chapter targets (`BibleNavigationActivity.cpp:151-165`).
The walk starts from 0 again and stops at the book's last chapter: about 80 entries for Genesis
(its chapter nav page is at 29, chapter 1 at 30) and **~1,344 for Revelation**. The cost of Go to
therefore grows with how far into the Bible the reader is. The marks that follow are in-memory
work: `markChapters` loops over `STUDY.passages()` and the copied bookmarks
(`:193-206`, `GridMarks.h`).

### Cost 3: masthead band — a full-BMP stream in the render task

The band is `MastheadLayout::band` (`MastheadLayout.h:22-27`): `480 − 2 × topPadding` wide
(`topPadding = 5`, `BaseTheme.h:134`), so 470 × `mastheadHeight = 120` (`BaseTheme.h:201`).
`thumbHeightFor(470, 120) = max(120, 470 / 0.6) = 783` (`CoverBandGeometry.h:46-48`). That is the
same `thumb_783.bmp` the launcher hero generates and caches
(`LauncherActivity.cpp:129`, `CoverBand::thumbPathFor`), which is why the grid never has to
generate one.

`CoverBand::blitCropped` (`CoverBand.cpp:19-58`) reads **all 783 rows** of the BMP and discards
those outside the crop (`:39-48`). It then sets the band's 470 × (120 − plate) pixels one
`drawPixel` at a time (`:51-55`). `Masthead::draw` already logs this:
`"Band %s in %lu ms"` (`Masthead.cpp:83-85`). It runs once per paint (`drawFooter`), so every
selection move repays it as well as entry.

A 1-bit cache of the band would be `470 × 120 / 8 = 7,050 B`. That is over the 4,096 B
auto-routing threshold, but PSRAM is only guaranteed through `heap_caps_malloc(MALLOC_CAP_SPIRAM)`,
wrapped in `std::unique_ptr<uint8_t[], PsramFree>` (`CatalogIndexStore.h:83-90`,
`CatalogIndexStore.cpp:40-47`; also `BibleSearchStore.cpp:25`).
`GfxRenderer::readFramebufferRegion` / `writeFramebufferRegion` (`GfxRenderer.cpp:1744+`) already
copy a framebuffer rectangle out and back: `EpubReaderMenuActivity::render` restores its page
snapshot with `writeFramebufferRegion` (`EpubReaderMenuActivity.cpp:520-523`).

### Cost 4: the refresh — FAST on entry, not HALF

`UiListActivity::render` ends with
`displayBuffer(halfRefreshPending.exchange(false) ? HALF_REFRESH : FAST_REFRESH)`
(`UiListActivity.cpp:169`). In `BibleNavigationActivity`, `halfRefreshPending` is set in exactly
one place: `enterLevel(Level::Book)` when a masthead is showing, i.e. **Back** from the chapter
grid (`BibleNavigationActivity.cpp:293-296`). The entry paint is `enterLevel(Level::Chapter)`, so it
is **FAST**. The SDK upgrades FAST to HALF only when `_inversionDirty` is set
(`freeink-sdk/libs/display/FreeInkDisplay/src/FreeInkDisplay.cpp:572-574`), which happens when
the output polarity changes between paints. `setInverted` sets `_inversionDirty` only on a real
change (`FreeInkDisplay.cpp:316-324`), and `ActivityManager` resolves the polarity per render from
`appliesNightMode()` (`ActivityManager.cpp:56-58`). That is `true` only for `ReaderActivity`
(`ReaderActivity.h:60`) and `PassageSelectActivity`, and `false` by default (`Activity.h:50`).
Even with night mode on, neither path changes polarity at the grid's first paint:

- On the menu path, the non-inverted menu sheet has already painted, and that paint consumed the
  dirty flag (`FreeInkDisplay.cpp:598`).
- On the Home path, the launcher is non-inverted, and the reader never paints before the grid.

So the issue's item 4 ("HALF ≈ 1.7 s on entry") does not match the code. On both paths the
grid's entry paint is a FAST refresh.

`GfxRenderer::displayBuffer` already logs `"Time = %lu ms from clearScreen to displayBuffer"`
(`GfxRenderer.cpp:1724-1727`). With the masthead log, that already gives the render-task share of
the time once `LOG_LEVEL` allows DBG. What is missing is a timestamp for the tap, for
`openChapterPicker`, and for each loop-task step in `onEnter`.

## Ranking (hypothesis — to be measured)

1. **Loop-task SD walks** (2a + 2b): ~1,500 book.bin entry reads on every Go to, and up to
   ~1,350 more for a New Testament book. All of it happens before the render task is notified,
   and the whole thing repeats on the second Go to.
2. **Masthead full-BMP stream** (3): 783 rows read, and ~50 K `drawPixel` calls, in the render task.
3. **Refresh** (4): FAST on entry on both paths. It is a fixed panel cost, not a target.
4. From Home only: the whole reader cold-load (`loadBook`, `STUDY.openPublication`, two SD writes).
   That is a separate cost, and the issue's "warm path" target arguably excludes it.

## Where a cache could live, and what each owner survives

| Owner | Survives menu Go to ×2 | Survives Home Go to ×2 | Surface |
|---|---|---|---|
| `BibleNavigationActivity` member | no (deleted on pop) | no | ui |
| `EpubReaderActivity` member | yes | no (reader replaced) | ui |
| `Epub` member (like `bibleBookNavSpine`, `Epub.h:33-36`) | yes | no (`Epub` rebuilt in `loadBook`) | epub (`lib/Epub`) |
| File-scope/static in `src/activities/reader`, keyed by the book | yes | yes | ui |

The memory is small whichever owner is chosen. The book table the grid needs is
`bookTargetSpine[66]` (`int16_t`), `bookIsDirect[66]`, `bookAbbrev[66][16]`, the section titles
and starts, and the `BibleBookNameTable` (`66 × (48 + 16)` B, `BibleBookNameTable.h:27-31,56-57`),
about 5.4 KB in total. The per-book chapter spines are a single first-chapter spine per book, if
consecutive chapters are consecutive spine items. In NWT they are not: Revelation's 22 chapters
are split files, `1001061170.xhtml` … `1001061170-split22.xhtml` at spine 1,322 … 1,343. The full
map is 1,189 chapters × `int16_t` = 2.4 KB. Every figure is under the 4 KB auto-route threshold,
so each would land in internal SRAM unless placed in PSRAM explicitly.

## Nearest existing examples

- **Memoise for an object's lifetime:** `Epub::bibleBookNavSpine`, a `mutable std::optional<int>`
  resolved on first query (`lib/Epub/Epub.h:33-36`, `Epub.cpp:980-985`).
- **The same book-table load, done elsewhere per call:** `EpubReaderActivity::collectRecentChips`
  heap-allocates a `BibleBookNameTable` and calls `load(..., WhenMissing::Fail)` on **every menu
  open** (`EpubReaderActivity.cpp:898-916`). That is the same book-nav stream plus TOC join
  (`BibleBookNameTable.cpp:46`), without the spine walk. `BibleSearchActivity` holds its own copy
  (`BibleSearchActivity.h:111`). A shared cache would serve all three.
- **Explicit PSRAM buffer:** `CatalogIndexStore` (`CatalogIndexStore.h:83-90`,
  `CatalogIndexStore.cpp:40-47`).
- **Timing logs:** `LOG_DBG("ERS", "Rendered page in %lums", millis() - start)`
  (`EpubReaderActivity.cpp:1390`) and `Masthead.cpp:83-85`.
- **Host tests for pure reader logic:** `test/bible_book_join/BibleBookJoinTest.cpp` and
  `test/bible_nav_scanner/`, registered in `test/CMakeLists.txt:94-95`. `test/masthead/` and
  `test/cover_band/` sit at `:126-127`. A new suite needs a line in `test/CMakeLists.txt`, which is
  a shared file, so the line goes in the PR body for the orchestrator (`.claude/agents/ui-dev.md`).

## Overlap with open work

PR #236 (`fix/235-bible-only-tags`) edits `EpubReaderActivity.{h,cpp}`, `EpubReaderMenuActivity.*`
and `ReaderEntryIntent.h`, with hunks around `openReaderMenu` (`:287`) and
`onReaderMenuConfirm` (`:787`). None touches `openChapterPicker` (`:848`) or
`BibleNavigationActivity.*`. If #236 lands first, re-read those files before implementing.

## Tier

This stays `standard`. Every candidate fix sits in `src/activities/reader` and
`src/components` (the ui surface). None changes an on-disk format or store, and none needs a
migration. It would reach the epub surface only if the cache is placed on `Epub`, and the
owner table above shows that placement buys nothing over a reader-surface cache.
