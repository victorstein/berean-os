# Issue #200 — reader menu as a bottom sheet: research

Branch `feature/200-reader-menu-sheet`, based on `67eaaf1e` (release 1.21.0, which follows #197).
Every claim below cites a line I read or a command I ran in this worktree.

## 1. Which files own this behaviour

| Concern | Owner |
|---|---|
| Menu screen, item list, in-place toggles | `src/activities/reader/EpubReaderMenuActivity.{h,cpp}` (84 + 232 lines) |
| Opening the menu, building its inputs, the title | `EpubReaderActivity::openReaderMenu` (`EpubReaderActivity.cpp:262-289`), `readerMenuTitle` (`:1611-1618`) |
| What each confirmed action does | `EpubReaderActivity::onReaderMenuConfirm` (`EpubReaderActivity.cpp:672-859`) |
| List skeleton that `render()` bypasses | `src/activities/UiListActivity.{h,cpp}` |
| FreeInkUI hosting and touch routing | `src/components/UiAppHost.{h,cpp}` |
| Page snapshot API | `GfxRenderer::storeBwBuffer` / `restoreBwBuffer` (`lib/GfxRenderer/GfxRenderer.cpp:2298-2355`) |
| Tap-outside-to-dismiss precedent | `src/components/OptionPopup.h:64-129` |
| Icons | `src/components/icons/` (two formats, see §6) |
| Per-chapter tag data | `StudyStore::passagesInDocument` (`src/study/StudyStore.h:119`), `TaggedPassage::documentSpine` (`lib/StudyStore/StudyStore/TaggedPassage.h:37`) |

## 2. Current control flow

### Opening

1. The reader's `loop()` calls `openReaderMenu()` in three cases: on Confirm, or on
   `ReaderUtils::isTouchMenuGesture` (`EpubReaderActivity.cpp:534-535`); on a Home-key hold when
   `longPressMenuFunction == LP_MENU_READER_MENU` (`:517-519`); or when a sub-screen's result
   handler reopens it (see below).
2. `openReaderMenu` (`:262-289`) passes the title, orientation, `!currentPageFootnotes.empty()`,
   `!bookmarks.empty()`, `hasHighlights` (compile-time `BOARD_HAS_PSRAM`, so `true` on the X4 Pro)
   and `isBible = epub->getBibleBookNavSpineIndex() >= 0` to
   `startActivityForResult(make_unique<EpubReaderMenuActivity>(...))`.
3. `ActivityManager` pushes the reader onto the stack and calls the menu's `onEnter()`
   (`src/activities/ActivityManager.cpp:143-162`). The menu does not override `onEnter`, so
   `UiListActivity::onEnter` runs: it resets the UI, registers `ACTION_ROW`, and requests a render
   (`UiListActivity.cpp:20-27`).

### What the framebuffer holds when the menu opens

- **From the reading surface:** the finished B/W page. `renderContents` leaves the BW page in the
  single framebuffer on every path. The non-tiled grayscale path calls `restoreBwBuffer()`
  (`EpubReaderActivity.cpp:1570`). The tiled path renders the planes into separate strip buffers
  and ends with `cleanupGrayscaleWithFrameBuffer()` (`:1487`, `:1536`), which does not modify the
  BW bytes. Anything drawn after the page is drawn *into* that buffer too: the bookmark toast
  (`:1276-1278`) and the indexing popup (`:297`). While the menu is on top, the reader's `loop()`
  does not run: `ActivityManager` loops `currentActivity` only. Its idle prewarm (`:366-389`) runs
  under a PrewarmScope, and scan mode draws no pixels (`GfxRenderer.cpp:767,993,1161,1392`,
  `ImageBlock.cpp:328`).
- **When a sub-screen is cancelled, the framebuffer holds that sub-screen's last frame, not the
  page.** These result handlers call `openReaderMenu()` directly from the handler:
  - `SELECT_CHAPTER` cancel (`:728-731`)
  - `SEARCH_BIBLE` cancel (`:745-748`)
  - `FOOTNOTES` cancel (`:757-760`)
  - `TEXT_SETTINGS` return (`:771-774`), after `releaseSectionKeepingPosition()`
  - `GO_TO_PERCENT` cancel (`:798-801`)
  - `progressChangeResultHandler` cancel, used by `BOOKMARKS` (`:675-677`)

  `ActivityManager` only requests the reader's re-render when the handler did not start another
  activity (`ActivityManager.cpp:130-133`). So in each of these cases the menu opens over whatever
  the sub-screen painted. A snapshot taken in the menu's `onEnter` would capture the chapter grid,
  search screen and so on, not the page. **The spec must deal with this handoff.**

### Rendering today

`EpubReaderMenuActivity::render` (`EpubReaderMenuActivity.cpp:222-232`):

1. Hands off to `optionPopup.processRender` if a popup is up (`:223`). The popup draws over the
   current framebuffer with no clear (`OptionPopup.h:15`).
2. Otherwise calls `renderer.clearScreen()` (`:225`): **the page is wiped.**
3. Then `drawChrome()`, which is `GUI.drawHeader` with the title (`:212-220`), then `renderUi()`
   (the FUI list), `drawFooter()` and `displayBuffer()` (default `FAST_REFRESH`,
   `GfxRenderer.h:189`).

`buildScreen` (`:168-210`) sets the content margin below the header and fills live values for
Night mode, Frontlight, Auto page turn and Rotate. It builds one `fui::ListProps` over
`menuRowItems` (a fixed 24-slot array, `EpubReaderMenuActivity.h:62-63`), and paginates through
`syncListViewport`.

`FreeInkApp` does not clear the target unless `setClearColor` is called
(`freeink-sdk/libs/ui/FreeInkUI/include/FreeInkApp.h:663-669`), and nothing in `src/` calls it.
`UiAppHost::renderUi` only sets the device and renders (`src/components/UiAppHost.cpp:17-27`). So
"restore the page, then draw the sheet over it" needs no FreeInkUI change.

### Item list

`buildMenuItems` (`EpubReaderMenuActivity.cpp:41-82`), in order:

1. `SELECT_CHAPTER`
2. `SEARCH_BIBLE` (Bible only)
3. `FOOTNOTES` (page has footnotes)
4. `BOOKMARKS` (book has bookmarks)
5. `HIGHLIGHTS` (PSRAM)
6. `TOGGLE_BOOKMARK`
7. `HIGHLIGHT_PASSAGE` (PSRAM)
8. `TEXT_SETTINGS`
9. `NIGHT_MODE`
10. `FRONTLIGHT` (`Frontlight.present()`)
11. `ROTATE_SCREEN` (`#if BEREAN_CAP_ROTATION`)
12. `AUTO_PAGE_TURN`
13. `GO_TO_PERCENT`
14. `SCREENSHOT`
15. `GO_HOME`
16. `DELETE_CACHE`

On the X4 Pro:

- `BEREAN_CAP_ROTATION` is 0 (`src/CrossPointSettings.h:18-24`), so `ROTATE_SCREEN` is never built
  and **rotation is not a snapshot invalidator on this device**.
- The board has a frontlight (`freeink-sdk/docs/xteink-x4pro-support.md:4,285`).
- So the maximum is **15 items for a Bible** (with footnotes and bookmarks) and **14 for other
  books**.
- #197's commit message gives 14 rows per page for the reader menu after the compact metrics
  (`git show 3591e3fe`: "Reader menu 800 − (5 + 44 + 8) = 743 → 14"). The issue's "about 9 are
  visible" is the pre-#197 figure. The problem still holds, because a 15-item Bible menu still
  scrolls.

### In-place actions (the menu stays open)

These change the screen while the sheet is up:

- `NIGHT_MODE` flips `SETTINGS.screenInverted` and re-renders (`:130-135`).
- `FRONTLIGHT` toggles the light (`:137-144`).
- `AUTO_PAGE_TURN` opens an `OptionPopup` (`:120-128`).
- `ROTATE_SCREEN` opens a popup and re-orients the renderer (`:105-118`), but only when the
  rotation capability is compiled in.

### Closing

- `handleButtons` closes on `wasReleased(Back)` (`:154-158`), and
  `MappedInputManager::wasReleased(Back)` is already true for the left-edge swipe
  (`src/MappedInputManager.cpp:265`, via `wasBackGesture`, `:222`). **The swipe-to-close in the
  issue's point 3 already works.** Only tapping the page area to close is new.
- `handleHomeGesture` closes the menu, cancelled (`:92-95`).
- Every close goes through `setResult` and `finish()`. The reader's handler then applies the
  orientation and the auto-turn value, and dispatches the action (`EpubReaderActivity.cpp:277-285`).
  `ActivityManager` then requests a reader render (`ActivityManager.cpp:130-133`), which repaints
  the page.

## 3. Night mode, and why the snapshot cannot survive it

- Inversion is applied at output, not in the framebuffer. `ActivityManager::renderTaskLoop` sets
  `display.setInverted(SETTINGS.screenInverted && currentActivity->appliesNightMode())` before
  every render (`ActivityManager.cpp:55-58`).
- `FreeInkDisplay` then inverts the bytes around the transfer
  (`freeink-sdk/libs/display/FreeInkDisplay/src/FreeInkDisplay.cpp:577-579`).
- The menu is not a reading surface (`Activity.h:50`, default `false`), so it renders with normal
  polarity.

In night mode, the page the user was looking at is inverted on the panel, but the snapshot holds
the normal-polarity bytes. A sheet drawn over it would flip the whole page region to normal
polarity. So night mode, whether it was on at entry or toggled from inside the sheet, has to take
the full-screen path, or at least do a clean repaint. This matches the issue's point 4.

## 4. The snapshot API as it exists, and why it can't be reused as-is

`GfxRenderer.cpp:2298-2355`:

- `storeBwBuffer()` mallocs `ceil(frameBufferSize / 8000)` chunks into the member
  `bwBufferChunks` and copies the framebuffer into them. The chunk size is
  `BW_BUFFER_CHUNK_SIZE = 8000` (`GfxRenderer.h:42`). With `frameBufferSize` = 48,000
  (`CLAUDE.md`, 800×480÷8), that is 6 chunks. If any chunk fails, everything is freed and it
  returns `false` (`:2312-2317`).
- `restoreBwBuffer()` copies the chunks back, **then calls `display.cleanupGrayscaleBuffers(frameBuffer)`
  and then frees the chunks** (`:2352-2354`). It is single-use.
- `FreeInkDisplay::cleanupGrayscaleBuffers` writes the buffer into controller RAM as the
  differential baseline (`FreeInkDisplay.cpp:868-877`, `_driver->cleanupGrayscaleBuffers`).
  Calling it on each sheet render would reset the baseline to "page without sheet" while the panel
  shows "page with sheet". The next FAST refresh would then re-drive the whole sheet. That is the
  ghosting the acceptance criteria forbid.

So the sheet needs a **repeatable, non-destructive restore**: copy the snapshot back without
touching controller RAM, and free only in `onExit`. There are two ways to get one:

- a new `GfxRenderer` method over `bwBufferChunks`, or
- a snapshot the menu owns itself.

Using the renderer's shared `bwBufferChunks` while the menu is open is safe from the other users:

- `renderAntiAliased` (`ReaderUtils.h:204`), the reader's grayscale pass (`EpubReaderActivity.cpp:1549`)
  and `ScreenshotUtil` (`src/util/ScreenshotUtil.cpp:91`) all run on the reader's render.
- The reader does not render while the menu is current.
- `SCREENSHOT` is taken by the reader after the menu has finished (`EpubReaderActivity.cpp:837-843`,
  `:1271-1274`).

The risk is a leaked snapshot. `storeBwBuffer` already logs and frees any stale chunk it finds
(`:2302-2306`), but the sheet must free its snapshot in `onExit` on every path.

**Memory placement.** Each 8,000-byte `malloc` lands in PSRAM on this build:

- The framework sdkconfig for the S3 OPI variant sets `CONFIG_SPIRAM_USE_MALLOC 1` and
  `CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL 4096`
  (`~/.platformio/packages/framework-arduinoespressif32-libs/esp32s3/qio_opi/include/sdkconfig.h:1072-1075`).
- `platformio.ini`'s `custom_sdkconfig` block has no SPIRAM line (`awk` over the block: 0 matches).
- Per-allocation placement still has to be confirmed on the device, as the issue says.

PSRAM free-space logging already exists as
`heap_caps_get_free_size(MALLOC_CAP_SPIRAM)` (`src/study/BibleSearchIndexer.cpp:325`,
`src/study/PsramJsonAllocator.cpp:39`). Internal free space is `ESP.getFreeHeap()`.

## 5. Tap and gesture routing available to a sheet

- `OptionPopup` is the precedent for dismissing on a tap outside, drawn over an existing screen.
  It registers its buttons plus an `ACTION_CHROME` guard rect over the dialog body. A release that
  hits no interaction, with real coordinates (`snap.touchX >= 0`), dismisses; swipe-end releases
  carry −1,−1 and don't dismiss (`OptionPopup.h:88-96`).
- `UiListActivity::loop` runs `handleCustomInput`, then `handleButtons`, then `routeListTouch`, then
  swipes (`UiListActivity.cpp:84-107`). An unrouted tap outside the sheet currently falls through
  and is ignored.
- `FreeInkApp` supports buttons with icons at explicit rects: `button(const ButtonProps&, Rect)`
  (`FreeInkApp.h:223`) and `ButtonProps::icon` / `iconSize`
  (`components/controls/button.h:10,21`). Existing call sites at explicit rects include
  `src/activities/reader/BibleSearchActivity.cpp:771-780` and
  `src/activities/network/BibleDownloadActivity.cpp:334-335`.
- The UiAppHost interaction cap is 64 (`src/components/UiAppHost.h:34`). The sheet needs at most
  1 close + 4 quick actions + about 11 rows + 1 guard, far below the cap.
- On the X4 Pro, `drawButtonHints` returns early on touch boards (`src/components/themes/BaseTheme.cpp:172-174`),
  so the footer takes no height.
- Theme row metric: `touchListRowHeight = 50` in Lyra (`src/components/themes/lyra/LyraTheme.h:20`),
  which `ListRowHeight::resolve` uses (`src/components/ListRowHeight.h:19-28`).

## 6. Icons

There are two formats in `src/components/icons/`:

- **SDK `freeink::Icon`** (`{w, h, opticalCenterY, bits}`, 1-bpp MSB-first, 1 = transparent),
  generated from stock Lucide SVGs by `freeink-sdk/libs/assets/Icons/tools/gen_icons.py` per
  `listIcons.manifest`, into `listIcons.h`. Hand-edited ones go in `customListIcons.h`
  (`listIcons.manifest:1-10`). `search24.h` is also this format (`Search24Icon = {24, 24, 11, ...}`,
  `search24.h:13`). These are the icons FreeInkUI can draw: `listIconFor` and `bitmapFromIcon`
  (`src/components/UiAppHelpers.h:78-96`), and `FrontlightPanelActivity.cpp:213`.
- **Legacy raw arrays** (`Book24Icon`, `BookmarkIcon`, `Settings2Icon`, …), drawn by
  `renderer.drawIcon`. `UiAppHelpers.h:78-79` notes "the legacy drawIcon assets use a different
  bit layout", so they cannot go into a FreeInkUI button as they are.

For the issue's icons: `bookmark` is already in the manifest at 24/32 (`listIcons.manifest`, line
`bookmark = bookmark`) and `book` is too. Lucide sources exist in the submodule for `x`,
`grid-3x3`, `layout-grid`, `tag`, `tags`, `highlighter` and `search`
(`ls freeink-sdk/libs/assets/Icons/lucide/icons`).

**Tooling gap on this machine:** `gen_icons.py` needs `rsvg-convert` (`gen_icons.py:35-40`) and
PIL (`:28`). `which rsvg-convert` finds nothing, and `python3 -c "import PIL"` fails with
`ModuleNotFoundError` (Python 3.14.7). `scripts/convert_icon.py` needs `cairosvg`, which is also
missing. ImageMagick is at `/opt/homebrew/bin/magick`. The plan has to install the rasteriser
(e.g. `brew install librsvg`, `pip install pillow`) or hand-pack the bits.

## 7. "Tags here (n)"

- There is no chapter-scoped tag count or list today. `HighlightsActivity` filters by tag only
  (`src/activities/reader/HighlightsActivity.h:81-110`: `filterTagId_`,
  `rebuildVisibleIndices` from `STUDY.passages()`). `openHighlights` passes no spine
  (`EpubReaderActivity.cpp:338-358`).
- There are two sources for a count:
  - `STUDY.passagesInDocument(spine)`, which resolves offsets through the unit index and drops
    stale fingerprints (`StudyStore.h:115-119`). The reader already calls it on every page render
    (`EpubReaderActivity.cpp:1398`).
  - A cheap scan of `STUDY.passages()` by `documentSpine`. The header calls that field "a weaker
    hint" (`TaggedPassage.h:37`).
- What a tap on "Tags here" opens (a chapter-filtered Tags list, or the existing list) is not
  settled by the issue. This will be raised as a decision in the spec, or answered from the
  mockup.
- The mockup (artifact `4qvfHbNnJ77F2gM5DELQrQ`, "Reader menu" pair) lists these rows: "Bookmarks
  (3), Tags here (2), Footnotes, Text settings, Night mode|Off, Frontlight|On, Auto turn|Off,
  Screenshot, Home, Delete cache". It has **no separate "Highlights"/Tags-list row**. It also
  shows a "RECENT" chip row, which is phase 4 (Recent places, `places.json`), **not #200**.

## 8. The mockup's geometry (hand-drawn, 480×800)

- The sheet top is at y = 300 (the sheet is about 62% of the height), with a 4 px black top rule.
- The title ("Isaiah 40", bold) is on the left, with ✕ on the right.
- Four quick-action tiles, 106×80, 2 px border, icon above label: Go to, Search, Mark, Tag.
- Then two columns of 220×58 rows with 1 px bottom rules and a centre divider.

With a 50 px row and no Recent row, the worst case fits:

| | Rows (at 2 per line) | Height |
|---|---|---|
| Non-Bible, 11 remaining items | 6 | 300 px |
| Title, quick-action row and gaps | | about 142 px |
| **Total** | | **about 442 px** |

That is inside a 500 px sheet. The spec must derive this from `getScreenHeight()` and theme
metrics, not from these numbers.

## 9. Nearest existing examples to mirror

- **Drawing over an existing frame, and tap-outside dismiss:** `src/components/OptionPopup.h`.
- **Pure, host-testable layout rule:** `src/components/ListRowHeight.h` with
  `test/ui_layout/ListRowHeightTest.cpp`, and `src/activities/launcher/LauncherRefresh.h`
  (`launcherNeedsCleanPaint`) with `test/launcher_refresh/`. Both keep firmware includes out so
  CMake can build them (`test/ui_layout/CMakeLists.txt`, registered at `test/CMakeLists.txt:102`).
  The item selection and sheet geometry (which items become quick actions, the column split, and
  whether everything fits) can follow the same pattern.
- **Previous phase of this overhaul:** #197 (`3591e3fe`), the reader-menu title from
  `BibleReference` (`EpubReaderActivity.cpp:1611-1618`, `test/ui_layout/BibleReferenceTest.cpp`).
- **A single-use snapshot used around a transient overlay:** `ScreenshotUtil.cpp:91-102`. It
  stores, draws a border, displays, then restores and does a HALF refresh.

## 10. Installed versions

- PlatformIO Core **6.1.19** (`~/.platformio/penv/bin/pio --version`). `pio` is not on PATH.
- Platform `pioarduino/platform-espressif32` **55.03.37** (`platformio.ini:15`).
- `freeink-sdk` submodule at **`67f7e01`** (`git submodule status`).
- CMake **4.4.2** for the host tests.
- Python **3.14.7**, without PIL, cairosvg or rsvg-convert (§6).

## 11. Tier

The work stays inside the `ui` surface:

- `src/activities/reader`
- `src/components` (icons, layout helper)
- `lib/GfxRenderer`, for a non-destructive restore

It reads StudyStore through its existing public API and adds no store or on-disk format. The
shared-file additions (translation keys for new labels, and a `test/CMakeLists.txt` line if a new
suite is added) are hand-offs, not edits. **`heavy` stays; no raise needed.**

## Open points for the spec

1. The snapshot must not be taken when the menu reopens from a sub-screen's cancel path (§2). The
   options are to fall back to the full-screen menu there, or to have the reader repaint the page
   before it reopens the menu.
2. A non-destructive restore in `GfxRenderer`, versus a snapshot the menu owns.
3. Night mode at entry, and toggling it in the sheet: fall back, or clean repaint (§3).
4. What "Tags here" opens, and whether the Highlights/Tags row stays (§7).
5. Refresh policy: FAST for sheet updates. The first sheet paint is also FAST, since only the
   sheet region differs from the panel.
