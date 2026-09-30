# Issue #204 research: cover mastheads on the chapter grid, Meetings and Publications

Base: `feature/204-cover-mastheads` at `0275a5bd` (= `origin/main` after `git fetch`). Both
dependencies have landed: #202 as `c72df634` (PR #209, `CoverBand`) and #199 as `7d019349`
(PR #213, 7×10 grid). The only open PR, #212 (`feature/205-tag-chip-row`), touches none of the
files below (`gh pr list --state open --json files`).

## Who owns the behaviour

| Screen | Activity | Header drawn by | Content top computed at |
|---|---|---|---|
| Bible book/chapter/verse grid | `src/activities/reader/BibleNavigationActivity.cpp` | own `drawChrome()` `:653-672` → `GUI.drawHeader(safe.x, safe.y + topPadding, safe.width, headerHeight)` `:671` | `buildScreen` `:455-470`: `setContentMargin(top = safe.y + topPadding + headerHeight + subHeaderHeight())` `:461`, then `spacer(verticalSpacing)` |
| Meetings | `src/activities/network/MeetingsActivity.cpp` | base `UiListActivity::drawChrome()` (`UiListActivity.cpp:142-147`) with `headerTitle()` = `STR_MEETINGS` (`MeetingsActivity.cpp:50`) | `computeLayout()` `:58-104`, `top = safe.y + topPadding + headerHeight + gap` `:64`; all bands, both cards and the cover height derive from it |
| Publications | `src/activities/catalog/PublicationsActivity.cpp` | base `drawChrome()` with `headerTitle()` = `STR_PUBLICATIONS` (`:35`) | `buildScreen` `:121-129`: `setContentMargin(top = safe.y + topPadding + headerHeight)`, then `spacer(verticalSpacing)` |

All three derive from `UiListActivity`. Its `render()` (`UiListActivity.cpp:154-171`) is
`clearScreen → drawChrome → renderUi` (repeated up to 8× on a rebuild request, `:163-167`) →
`drawFooter → displayBuffer()`. `displayBuffer()` defaults to `FAST_REFRESH`
(`GfxRenderer.h:189`), so **every paint of these screens today is FAST**, including entry.
`PublicationsActivity::render` only adds the delete popup in front (`PublicationsActivity.cpp:234-237`).

Entry points: the grid is only constructed from the reader menu
(`EpubReaderActivity.cpp:723`); Meetings and Publications from the launcher
(`LauncherActivity.cpp:449,454`), which is the art-heavy screen the issue means.

## The component to reuse: `CoverBand` (from #202)

- `CoverBand::draw(renderer, coverPath, band, Style{focusBand, cornerRadius, plateHeight})`
  (`src/components/CoverBand.h:26`, `.cpp:64-84`): blits the cropped cover, masks corners, fills an
  opaque white plate over the band's foot with a 1 px rule on top (`.cpp:76-82`). Returns `false`
  with nothing of the cover shown when the file is missing, unreadable, or smaller than the band
  (a mid-stream failure clears the band white, `.cpp:40-43`). The caller draws the label.
- `CoverBand::plateRect` (`.cpp:86-89`), `CoverBand::thumbPathFor(bookPath, bandW, bandH, generatedAny)`
  (`.cpp:91-93`) → `CoverThumb::pathFor(bookPath, thumbHeightFor(w, h))`.
- Geometry is host-tested: `src/components/CoverBandGeometry.h` (`crop`, `thumbHeightFor`, `plateTop`,
  `BOOK_TITLE_BAND = 0.25f`, `MAGAZINE_MASTHEAD_BAND = 0.0f`) in `test/cover_band/`
  (registered at `test/CMakeLists.txt:126`).
- Only consumer today: `LauncherActivity::drawCoverTile` (`LauncherActivity.cpp:307-322`), which
  is the model to mirror: build a `Style`, call `draw`, on `false` fall back to a non-art tile, else
  draw text at `plateRect(...).y + padding` and the border.

### Cost of one draw — the "every redraw streams from SD" point

`blitCropped` (`CoverBand.cpp:19-58`) opens the BMP, then reads **every** row of the file
(`for row < height`, `:39`) and discards those outside the crop (`:48`); only the visible
`band.width × band.height` pixels are drawn, one `drawPixel` each (`:51-55`). Two transient row
buffers, `(width+3)/4` and `getRowBytes()` bytes (`:32-33`), both far under 4096 B, so internal SRAM,
freed on return. Nothing band-sized is held today.

For a full-width 480 × 120 band: `thumbHeightFor(480, 120) = max(120, int(480/0.6)) = 800`
(`CoverBandGeometry.h:46-48`), i.e. an 800-row thumbnail, ~480 px wide, read in full on every
render of the screen — a list selection move included, because `UiListActivity::render` repaints
the whole frame (`:155`). That is the SD traffic the issue's PSRAM fallback is about. Whether it is
"visibly slow" is a device measurement; nothing on the host can settle it.

The issue's cache size checks out: a 1-bit 480 × 120 band is `480 × 120 / 8 = 7,200 B`. That is
above the 4096 B auto-routing threshold, but the only way to *demand* PSRAM is `heap_caps_malloc`:
`CatalogIndexStore.h:83-90` says so and wraps it in `std::unique_ptr<uint8_t[], PsramFree>`
(`CatalogIndexStore.cpp:40-47`, `MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT`). The same idiom is at
`BibleSearchStore.cpp:25` and `PsramJsonAllocator.cpp:8-19`. That is the pattern to mirror if the
cache is needed.

### Thumbnail sizes — a band size is a cache key

`CoverThumb::pathFor` (`src/util/CoverThumb.cpp:29-47`) names `thumb_<h>.bmp` and, when absent,
opens the EPUB with `epub.load(true, true)` and generates it (`:41-42`). The X4 Pro profile sets no
viewable insets (`BoardConfig.h:675`, `viewableInsets = {}`; `XTEINK_X4_PRO` at `:1364` does not
override it), so the launcher's Bible tile is `480 − 2 × topPadding = 470` wide
(`LauncherActivity.cpp:260-262,281`) → `thumb_783.bmp`. A 480-wide masthead asks for
`thumb_800.bmp`: a second generation per publication. A band of the launcher's width (470) would
reuse `thumb_783`. Generation happens on first entry and is the slow part (it opens the EPUB).

On the grid, the Bible EPUB is already open: the activity holds `std::shared_ptr<Epub> epub`
(`BibleNavigationActivity.cpp:36-41`), and `Epub::getPath()` / `getThumbBmpPath(h)` /
`generateThumbBmp(h)` exist (`lib/Epub/Epub.h:56,62-63`). `APP_STATE.bibleCoverPath` holds the
launcher's `thumb_783` path (`LauncherActivity.cpp:108-113`).

## The header the masthead replaces, and the battery

`BaseTheme::drawHeader(renderer, rect, title, subtitle)` (`src/components/themes/BaseTheme.cpp:285-397`)
draws the FreeInkUI `header` — which **fills its whole rect** with the popup style's background
first (`freeink-sdk/libs/ui/FreeInkUI/include/components/controls/header.h:63-66`) — then the title,
the bottom underline (`:362-366`), and the battery via `fui::batteryIndicator` anchored to the band's
top `batteryBarHeight` strip (`:369-384`). So the existing header, drawn into the plate rect, is
already an opaque plate carrying the title and the battery; whether the fill is white is set by the
theme tokens and needs confirming in the spec. Active theme on this device is Lyra
(`CrossPointSettings.h:331`, `uiTheme = LYRA`; `UITheme.cpp:36-43`): `topPadding 5`,
`batteryBarHeight 44`, `headerHeight 44`, `verticalSpacing 8`, `tabBarHeight 40`
(`LyraTheme.h:9-13,35`). With touch, `buttonHintsHeight` is zeroed (`UITheme.cpp:53-56`), so the
portrait safe area is the full 480 × 800 (`UITheme.cpp:83-93`). The compact header from #194 is
this `headerHeight 44` path, and Settings and Tags keep it unchanged.

## The chapter grid's row budget (computed from source, not measured on device)

`NumberGrid::geometryFor` (`src/activities/reader/NumberGridLayout.h:33-40`): stride
`MIN_CELL 56 + GAP 8 = 64`; `cols = clamp((W+8)/64, 4, 8)`; `rows = min((H+8)/64, MAX_CELLS 70 / cols)`.
`FreeInkApp::spacer` consumes from `content_` and `body()` returns what is left
(`FreeInkApp.h:72-83,103-107`).

| | Top chrome | Body H (800 − chrome − 8) | 7 × 10? | Square cell `min((480−48)/7, (H−72)/10)` |
|---|---|---|---|---|
| Today, chapter/verse | 5 + 44 = 49 | 743 | yes | min(61, 67) = 61 |
| Band 120 at y = 0 | 120 | 672 | yes | min(61, 60) = **60** |
| Band 124 (mockup) | 124 | 668 | yes | 59 |
| Largest band keeping 10 rows | 160 | 632 | yes, at the floor | 56 |

So a 120 px masthead does not cost the grid a row; it shrinks the square cell from 61 to 60 px.
The book level also reserves the 40 px section sub-header (`subHeaderHeight()`, `:271-273`), and
the book grid has its own layout (`BookGridLayout.h`, `rebuildBookLayout`); the issue names only the
chapter grid, so whether the masthead also runs at the book and verse levels is a spec decision.
The exact body height should be read from the device log, as #199's spec also left it
(`docs/superpowers/research/2026-09-30-issue-199-research.md:70-80`).

## Meetings and Publications specifics

- **Meetings.** The cards are sized from what is left under the header (`computeLayout` `:94-103`)
  and the card cover thumbnail is generated at exactly `layout_.coverHeight` (`fillCard` `:227`).
  A 120 px masthead replacing the 49 px header takes 71 px from the two cards, which changes
  `coverHeight` and so the `thumb_<h>.bmp` the cards ask for — a one-off regeneration per issue. The
  card covers are drawn with `CoverThumb::drawNative` through the `bookCard` painter (`:369-376`).
  The screen holds two publications (Watchtower and workbook, `cards_[0..1]`); **which cover the
  masthead shows is not stated in the issue**. The launcher's precedent is "this week's
  Watchtower, else the workbook" (`LauncherActivity.cpp:117-135`, comment at `:117`).
- **Publications.** A list of every book on the card, with 44 px row thumbnails decoded into
  per-entry BW1 buffers (`THUMB_HEIGHT = 44`, `MAX_THUMBS = 32`, `PublicationsActivity.h:35-38`,
  `loadThumb` `:79-119`). **There is no single "own" cover for this screen**, and the issue does not
  say which one to use (first/newest publication, the selected row's, or a fixed one). The mockup
  does not draw this screen.
- Neither screen logs heap today (`grep -rn "getFreeHeap\|heap_caps_get_free_size" src/activities`
  finds only `BibleNavigationActivity.cpp:69-72`, `EpubReaderActivity`, `WifiSelectionActivity`,
  `CrossPointWebServerActivity`, `SleepActivity`). The grid's `onEnter` log (`:69-72`,
  internal free + largest block + PSRAM free) is the format to mirror for the acceptance criterion
  "heap is logged on each screen".

## The design reference

The mockup (claude.ai artifact `4qvfHbNnJ77F2gM5DELQrQ`, read in this session) draws only the chapter
grid: art full-bleed 480 × 124 at y = 0, a plate inset 8 px each side at y = 74, 46 px tall,
2 px border, radius 8, carrying "‹ Books", the book name in caps, and the battery; cells begin at
y = 132. It is marked "hand-drawn, not a device render". Its cells (62 × 57) are not square, which
#199 made them, so its numbers are illustrative only.

## Refresh: the nearest existing examples

- **Launcher** (`LauncherActivity.cpp:398-403`): `launcherNeedsCleanPaint(cleanInitialRefresh, firstRenderDone)`
  (`LauncherRefresh.h:15-17`, host-tested in `test/launcher_refresh/`) picks HALF for the first paint
  of an entry that needs it, FAST otherwise; `firstRenderDone = true` after.
- **Bible search** (`BibleSearchActivity.cpp:1015-1038`): the only `UiListActivity`-shaped screen
  that overrides `render()` to choose the mode per frame. It mirrors the base render body and calls
  `displayBuffer(full ? HALF : FAST)` from an `std::atomic<bool> fullRefreshPending`
  (`BibleSearchActivity.h:116`), set on entry (`.cpp:113,136`). This is the closest pattern for "one
  HALF on entry, FAST for every selection move" inside a list activity.

## Nearest example of this kind of change

`LauncherActivity::drawCoverTile` (`LauncherActivity.cpp:302-322`), plus its tests
(`test/cover_band/CoverBandGeometryTest.cpp`, `test/launcher_refresh/`). A host-testable masthead
layout would follow `CoverBandGeometry.h` / `NumberGridLayout.h`: a renderer-free `constexpr`
header with a new suite registered in `test/CMakeLists.txt` (shared file — hand the
`add_subdirectory` line off in the PR body; `number_grid` is at `:101`, `cover_band` at `:126`).

## Installed tools

| | Version | Evidence |
|---|---|---|
| PlatformIO Core | 6.1.19 | `~/.platformio/penv/bin/pio --version` (`pio` not on PATH) |
| Platform | pioarduino espressif32 55.03.37 | `platformio.ini:15` |
| CMake | 4.4.2 | `cmake --version` |
| GoogleTest | v1.17.0 | `test/CMakeLists.txt:17` (FetchContent) |
| freeink-sdk | `67f7e01` | `git submodule status` |

## Open questions for the spec

1. Which cover Meetings and Publications show (see above). Recommended defaults: this week's
   Watchtower, else the workbook, matching the launcher; for Publications the most recently
   opened publication (`RECENT_BOOKS`), else the first listed; with no cover at all, fall back
   to the compact header.
2. Masthead at the book and verse levels of the grid too, or chapter only.
3. Band width 470 (reuses the launcher's `thumb_783.bmp` for the Bible) or 480 (a new `thumb_800`).
4. Plate: draw `GUI.drawHeader` into the plate rect (reuses title + battery + fill) or a new
   masthead drawing routine. The first adds no new pattern.
5. PSRAM band cache: build it now, or only if the device shows a slow redraw, as the issue says.

## Tier

Stays `standard`. Everything is on the `ui` surface (`src/activities`, `src/components`). No
persisted store, no on-disk format change (`thumb_<h>.bmp` naming is unchanged; new heights are
only new cache files), no SDK edit, no input-layer change. Shared-file touches (a translation key
if any, a `test/CMakeLists.txt` line) are handed off in the PR body.
