# Issue #202 research: reusable CoverBand component

Base: `refactor/202-cover-band` at `67eaaf1e` (= `origin/main` after `git fetch`; `git log HEAD..origin/main` is empty).
No open PRs (`gh pr list --state open` returns nothing), so no sibling is holding `LauncherActivity.cpp`.

## Line numbers in the issue are stale

The issue cites `LauncherActivity.cpp:341-419` and buffers at `:359-360`. At `67eaaf1e`:

| What | Where |
|---|---|
| `drawCoverFilling` (the streamed, cropped blit) | `src/activities/launcher/LauncherActivity.cpp:329-372` |
| its two row buffers | `:341-342` |
| `drawCoverTile` (mask, plate, caption, border) | `:379-401` |
| `drawCenteredIn` (plate caption text) | `:403-415` |
| `coverFillHeight` | `:259-261` |
| focus-band constants | `:58` (`NARROWEST_COVER_ASPECT = 0.6f`), `:63` (`BOOK_TITLE_BAND = 0.25f`), `:64` (`MAGAZINE_MASTHEAD_BAND = 0.0f`) |
| declarations | `LauncherActivity.h:57-63` |

## Who owns the behaviour

- **Drawing:** `LauncherActivity`, private members only (`LauncherActivity.h:58-63`). Nothing outside the launcher can call them.
- **Thumbnail cache:** `src/util/CoverThumb.{h,cpp}`. `pathFor(bookPath, height, generatedAny)` (`CoverThumb.cpp:29-47`) returns `Epub::getThumbBmpPath(height)` = `<cache>/thumb_<h>.bmp` (`lib/Epub/Epub.cpp:668`), generating it via `Epub::generateThumbBmp` when absent (`CoverThumb.cpp:41-42`).
- **Generation:** `Epub::generateThumbBmp` (`Epub.cpp:670-764`) asks for target `w = height * 0.6`, `h = height` (`:706-707`, PNG `:741-742`) through `jpegFileTo1BitBmpStreamWithSize` / `pngFileTo1BitBmpStreamWithSize`. Both pass `crop = true` (`JpegToBmpConverter.cpp:731-734`, `PngToBmpConverter.cpp:848-851`), which picks `scale = max(tw/sw, th/sh)` (`JpegToBmpConverter.cpp:582-583`) and outputs the **whole** scaled image (`:588-589`) — it is not trimmed to the target box. So a thumbnail always covers `0.6h × h`: one axis matches, the other may exceed it. That is why `drawCoverFilling` has to crop in both axes.
- **Other `CoverThumb` users**, which the component must not break: `MeetingsActivity.cpp:227,231,374` (`pathFor`, `sizeOf`, `drawNative` inside bookCard's `coverPainter`), `PublicationsActivity.cpp:80` (`pathFor`), and the launcher's own stacked-tile fallback `drawTileArt` → `drawNative` (`LauncherActivity.cpp:316`).

## Current control flow on Home

1. `onEnter` → `computeLayout()` then `resolveTargets()` (`:74-81`). Layout first because the thumbnail height derives from the tile rect.
2. `resolveTargets` requests the Bible cover at `coverFillHeight(rects[Bible])` (`:118-119`) and the meetings cover at `coverFillHeight(rects[Meetings])` (`:143-144`). `coverFillHeight` = `max(tile.h, tile.w / 0.6)` (`:259-261`). The Bible path is also saved to `APP_STATE.bibleCoverPath` (`:122-125`) for the sleep screen.
3. `render` (`:454-484`): `clearScreen`, header, then `drawCoverTile` for Bible (`BOOK_TITLE_BAND`) and Meetings (`MAGAZINE_MASTHEAD_BAND`) (`:461-465`); `drawTile` for Search, Settings and Resume. Then `launcherNeedsCleanPaint` picks HALF or FAST (`:477-481`).
4. `drawCoverTile` (`:379-401`):
   - `plateHeight = tileTextHeight(UI_10_FONT_ID, hasSubtitle) + 2 * TILE_PADDING` (`:383`).
   - `drawCoverFilling(coverPath, rect, rect.h - plateHeight, focusBand)`; on `false`, falls back to `drawTile(..., emphasised=true, {}, icon)` (`:385-388`).
   - `maskRoundedRectOutsideCorners(rect, TILE_RADIUS)` (`:393`), opaque white `fillRoundedRect` plate with bottom corners only (`:395-397`), a 1 px rule on the plate top (`:398`), centred title/subtitle (`:399`), border 3 px if selected else 1 px (`:400`).
5. `drawCoverFilling` (`:329-372`):
   - Opens the BMP through `HalStorage`, `parseHeaders`, declines if `width < rect.w || height < rect.h` (`:339`) — never upscales.
   - Allocates `packedRow` = `(width+3)/4` bytes and `rowScratch` = `bitmap.getRowBytes()` via `makeUniqueNoThrow`, freed on return (`:341-346`). For the widest tile (full column width, a few hundred px) these are well under 4096 B, so by the PSRAM auto-routing threshold they sit in internal SRAM, transiently.
   - **Geometry:** `xOffset = (width - rect.w) / 2` (`:348`); `focusRow = int(height * focusBand)` (`:354`); `yOffset = clamp(focusRow - visibleHeight/2, 0, height - rect.h)` (`:355`).
   - Streams every file row (`:357-370`), maps bottom-up rows with `isTopDown()` (`:361`), skips rows outside `[yOffset, yOffset + rect.h)` (`:362`), and for each visible column reads the 2-bit value and draws black when `value < 3` (`:367-368`).

The geometry (steps: x-centre, focus row, clamp) is pure arithmetic on `(bmpW, bmpH, rect.w, rect.h, visibleHeight, focusBand)` and is the part the issue asks to host-test.

## Renderer facts the component relies on

- No rectangular clip region exists. `GfxRenderer.h:70,210,226` are a grayscale *render band* clip, not a rect clip. The blit loop is the clip, as the comment at `LauncherActivity.cpp:326` says.
- APIs in use exist as called: `drawPixel(int,int,bool)` (`GfxRenderer.h:233`), `maskRoundedRectOutsideCorners(x,y,w,h,r,Color)` (`:242`), `fillRoundedRect` per-corner overload (`:256`), `drawBitmap` (`:260`, scales down only — not usable here). `Bitmap::readNextRow`, `isTopDown`, `getRowBytes` at `lib/GfxRenderer/Bitmap.h:70,74,76`.
- `drawPixel` maps portrait coordinates then bounds-checks against the panel (`GfxRenderer.cpp:559-571`).

## Nearest existing examples

1. **A second copy of the same blit already exists:** `SleepActivity::drawBibleCover` (`src/activities/boot_sleep/SleepActivity.cpp:602-639`) — identical row buffers (`:617-618`), identical row walk and 2-bit test (`:626-636`), different y-offset rule (`clamp(height/8)`, `:624`) and full-panel rect. It is the obvious later consumer; whether #202 migrates it is a scope call for the spec (the issue names only the launcher).
2. **FreeInkUI painter shape:** `fui::BookCardProps::coverPainter` is `bool (*)(DrawTarget&, Rect, const BookCardProps&, void*)` with `coverPainterUserData` (`freeink-sdk/libs/ui/FreeInkUI/include/components/media/book-card.h:41-42,72-73`); `cover-grid.h:64-65,151-152` has the same shape. `MeetingsActivity::paintCover` (`MeetingsActivity.cpp:369-376`, context struct `MeetingsActivity.h:60-64`) is the in-repo user. A CoverBand draw function can be called from such a painter without an SDK edit.
3. **Host-testable geometry beside a component:** `src/components/ToastLayout.h` — a `constexpr`, renderer-free header of geometry for `BaseTheme::drawToast`, tested by `test/posted_message/ToastLayoutTest.cpp`. Likewise `src/activities/launcher/LauncherRefresh.h` tested by `test/launcher_refresh/` (CMake: `add_executable`, `target_include_directories(... ${REPO_ROOT}/src)`, link `crosspoint_test_common GTest::gtest_main`, `gtest_discover_tests`). A `CoverBandGeometry`-style header plus a new `test/cover_band/` suite registered with `add_subdirectory(cover_band)` in `test/CMakeLists.txt` (shared file — hand off the line; existing launcher lines are `:125-126`) is the established pattern.
4. `test/bit_blit/BitBlitTest.cpp` tests `lib/GfxRenderer/BitBlit.h` bit packing on host — a precedent if the spec wants the 2-bit column extraction tested too.

## Installed tools and packages

| | Version | Evidence |
|---|---|---|
| PlatformIO Core | 6.1.19 | `~/.platformio/penv/bin/pio --version` (`pio` is not on PATH) |
| Platform | pioarduino espressif32 55.03.37 | `platformio.ini:15` |
| Framework | arduino | `platformio.ini:17` |
| CMake | 4.4.2 | `cmake --version` |
| GoogleTest | v1.17.0 | `test/CMakeLists.txt:17` (FetchContent) |
| ArduinoJson (host) | v7.4.2 | `test/CMakeLists.txt:31` |
| freeink-sdk | `67f7e01` | `git submodule status` |

## Constraints this surfaces for the spec

- **"No visible change"** means the refactor must reproduce the exact `xOffset`/`yOffset` integer arithmetic, the `value < 3` threshold, the mask → plate → rule → caption → border order, and the same requested thumbnail height (`coverFillHeight`) — a different height is a different `thumb_<h>.bmp` and a different dither.
- **No new resident buffers:** today's two row buffers are per-call and freed on return; the component keeps them per-call.
- **Caption text** is launcher-specific (fonts `UI_10_FONT_ID` / `SMALL_FONT_ID`, `drawCenteredIn`). The "optional opaque label plate" can be the plate geometry and fill only, with the caller drawing its text, or can take title/subtitle; the spec decides.
- **Fallback** to the stacked icon tile is launcher policy and stays in the launcher; the component reports `false` as today.

## Tier

Stays `standard`. Everything is on the `ui` surface (`src/activities/launcher`, `src/components`, `src/util/CoverThumb`); no on-disk format changes (the `thumb_<h>.bmp` naming is unchanged), no persisted store, no SDK edit. The only shared-file touch is one `add_subdirectory` line in `test/CMakeLists.txt`, handed off in the PR body.
