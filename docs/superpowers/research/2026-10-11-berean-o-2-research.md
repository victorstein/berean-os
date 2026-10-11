# berean-o-2 research — R9, cache the cover band

Bead `berean-o-2`, task `t2`. Branch `perf/r9-cover-band-cache` at `1ba1cd34`. Every `file:line`
below was read in this worktree on 2026-10-11; nothing here was measured on a device.

## 1. What the brief asks, in one line

Today every Home paint and every masthead paint streams the whole cover thumbnail from SD and
re-decodes it. Keep the band's drawn result across paints, and across the activity being destroyed,
so a repeat paint replays it in a few ms. The replayed pixels must be identical to a fresh draw.

## 2. Who owns the behaviour

| Piece | Where | Role |
|---|---|---|
| `CoverBand::draw` | `src/components/CoverBand.cpp:64-84` | Blits, then masks the corners (`:70-72`) and lays the plate (`:76-82`) |
| `blitCropped` (anon.) | `src/components/CoverBand.cpp:19-58` | Opens the BMP, crops it and plots every inked pixel with `drawPixel` |
| `CoverBandGeometry::crop` | `src/components/CoverBandGeometry.h:34-41` | Pure crop arithmetic; returns `fits=false` when the cover is smaller than the band |
| `Masthead::draw` | `src/components/Masthead.cpp:78-95` | Band rect and style from the theme; logs `Band %s in %lu ms` (`:83-85`) |
| Home hero | `src/activities/launcher/LauncherActivity.cpp:246-267` (`drawHero`) | Calls `CoverBand::draw` at `:248`. **No timing log.** |
| Masthead callers | `BibleNavigationActivity.cpp:777`, `PublicationsActivity.cpp:48`, `MeetingsActivity.cpp:60` | All three are in `drawFooter()` |

Home's style is `{BOOK_TITLE_BAND, RADIUS, layout.plate.height}` (`LauncherActivity.cpp:247`). The
masthead's is `{focusBand, 0, metrics.headerHeight + 1}` (`Masthead.cpp:81`). Meetings passes
`MAGAZINE_MASTHEAD_BAND` (0.0); the other two pass `BOOK_TITLE_BAND` (0.25)
(`CoverBandGeometry.h:19-20`).

The R0 timing log now exists in `Masthead.cpp:83-85`. The responsiveness research §7 says it does
not exist in `src/`; that note predates it.

## 3. Current control flow, per paint

### Home

`LauncherActivity::render` (`:353-371`) runs `clearScreen()`, then `drawHero()`, then the rest, then
`displayBuffer`. `drawHero` runs on **every** render. That means every entry, and every selection
move: `buttonNavigator` calls `requestUpdate()` at `:477-484`. `bibleCoverPath` is resolved once per
`resolveTargets()` (`:117-128`), through `CoverBand::thumbPathFor`. It is cached in `APP_STATE` for
the sleep screen.

### Mastheads

`UiListActivity::render` (`src/activities/UiListActivity.cpp:154-170`) clears the screen and draws
chrome and rows, possibly through up to 8 rebuild passes. It calls `drawFooter()` **once**, after
those passes (`:168`), then `displayBuffer`. The band is therefore plotted over a framebuffer that
already holds the list rows. The rows start below `contentTop`, so the band area is paper.

### `blitCropped`, for both callers

1. `Storage.openFileForRead`, then `Bitmap::parseHeaders`, then `crop(...)` (`:22-30`).
2. Two row buffers come from `makeUniqueNoThrow`: `(width+3)/4` bytes and `rowBytes` (`:32-37`).
   Both are on the internal heap and live for the call only.
3. **Every row of the file** is read and decoded (`:39-56`). A row outside the crop is decoded and
   then discarded with `continue` (`:48`). Nothing seeks to the crop, and nothing breaks out after
   the last band row.
4. For each band row and column, a 2-bit value below 3 is plotted with `drawPixel(..., true)`
   (`:51-55`). White pixels are never written, so the blit only ever adds ink.

#### Row order

The thumbnails are written **top-down**: `-height` at `lib/JpegToBmpConverter/JpegToBmpConverter.cpp:102`
and at `lib/PngToBmpConverter/PngToBmpConverter.cpp:139`. So every row after
`crop.yOffset + band.height` is read and decoded for nothing.

#### What a pixel depends on

The decode depends only on the file bytes. `CoverBand` builds `Bitmap(file)` with dithering off
(`lib/GfxRenderer/Bitmap.h:68`). 1-bit and 2-bit files take the native-palette branch
(`Bitmap.cpp:145`), which is `adjustPixel(lum) >> 6` (`:205-207`). `adjustPixel` returns its input,
because `USE_BRIGHTNESS = false` is a `constexpr` (`lib/GfxRenderer/BitmapHelpers.cpp:9,43-44`).

No runtime setting enters, and the 1-bit path carries no state from row to row. `prevRowY` only
feeds `quantize`, which this branch does not call. So the ink is a pure function of:

- the cover file's bytes;
- the band's width and height;
- `visibleHeight = band.height - plateHeight`;
- `focusBand`.

`band.x` and `band.y` only translate it.

### `drawPixel`

`lib/GfxRenderer/GfxRenderer.cpp:559-598`. Each call does three things:

- a Portrait fast path (`:564-566`);
- a bounds check that logs on failure (`:572-575`);
- a strip-mode branch (`:581-587`).

It ends in a read-modify-write of one framebuffer byte. In Portrait, logical `x` becomes physical
row `panelHeight-1-x` and logical `y` becomes physical column (`:565-566`). So one logical column of
the band is one contiguous bit-run inside one physical framebuffer row.

### Threading

There is one render task for the whole firmware. `ActivityManager::begin` creates
`"ActivityManagerRender"` (`src/activities/ActivityManager.cpp:32-38`). `renderTaskLoop` calls
`currentActivity->render(...)` under a `RenderLock` (`:47-60`). Home's `drawHero` and every
`drawFooter` run there, so a cache touched only from those draw paths is touched by one task.

## 4. Sizes

| | Value | Evidence |
|---|---|---|
| Band width, Home and masthead | 464 | `test/home_layout/HomeLayoutTest.cpp:48`; Home reuses `MastheadLayout::band` (`HomeLayout.h:125-126`) |
| Masthead height | 120, both themes | `BaseTheme.h:201`, `lyra/LyraTheme.h:79` |
| Home hero height | 272 Classic, 250 Lyra | `HomeLayoutTest.cpp:64-65` |
| Thumbnail height | 773 | `HomeLayoutTest.cpp:49`; `thumbHeightFor` (`CoverBandGeometry.h:46-48`) |
| Thumbnail width | ≥ 464 whenever the band is drawn | `crop` refuses a narrower cover (`CoverBandGeometry.h:36`) |
| BMP row bytes | `(w+31)/32*4`, so ≥ 60 at w ≥ 464 | `Bitmap.cpp:127` |

Home and the masthead ask for the **same** thumbnail file (773 tall) and crop it differently.

The work per paint, as counted from the code (not timed):

- SD bytes read: ≥ 773 × 60 = 46,380, over 773 `HalFile::read` calls. Each call takes
  `storageMutex` (`CLAUDE.md`, "SdFat is not thread-safe").
- Pixels decoded: ≥ 773 × 464 = 358,672 `packPixel` calls.
- Pixels plotted, at most: 464 × 120 = 55,680 for a masthead, and 126,208 for the Classic Home hero.

**Ink bitset per band**, as `ceil(w*h/8)`: masthead 6,960 B, Home 15,776 B (Classic) or 14,500 B
(Lyra). #238's plan says 7,050 B, because it assumed a width of 470.

**Distinct bands one wake can paint:**

| Band | Cover |
|---|---|
| Home hero | the Bible's |
| Bible Go to masthead | the Bible's, a different crop |
| Publications masthead | `pickCover` (`PublicationsActivity.cpp:113`) |
| Meetings masthead | a meeting publication's, focus 0.0 (`MeetingsActivity.cpp:164`) |

All four held at once is ≤ 6,960 × 3 + 15,776 = **36,656 B**. A single slot would thrash on
Home → Go to → Home.

## 5. What can invalidate a held band

| Event | Effect on a held band | Evidence |
|---|---|---|
| Sleep | Nothing to do. Deep-sleep wake is a chip reset, so RAM and PSRAM state is gone. | `src/main.cpp:269-270` ("Wake from deep sleep is effectively a chip reset") |
| Theme switch | Changes `headerHeight` and the hero height, so the rect and plate height change. Caught by a rect + style key. | Applied live by `SettingsActivity.cpp:183-192`; also `main.cpp:427` at boot |
| Orientation | Changes `getScreenWidth()` and the insets, so the rect changes. Caught by a rect key. | `Masthead.cpp:20-29`, `LauncherActivity.cpp:87-100` |
| A different cover at the **same path** | **Not** caught by file size. See below. | — |

### A different cover at the same path

- The thumbnail path is `<book cache>/thumb_<h>.bmp`, keyed by the **book path's** hash
  (`lib/Epub/Epub.cpp:668`). It is generated only when missing (`:670-674`) and written in place,
  not atomically (`:700-718`).
- The cache directory is dropped by `clearBookCache` (`src/util/BookCacheUtils.cpp:23-29`). Its
  callers are `PublicationDownloader.cpp:110,252`, `CrossPointWebServer.cpp:622,691` and
  `FileRoutes.cpp:398,549,639,740`.
- It is also dropped by `CardBooks::remove` (`src/util/CardBooks.cpp:57`), the reader's
  delete-cache action (`EpubReaderActivity.cpp:898`) and `ClearCacheActivity` (`:97-130`).
- A re-download, or an upload of a different file under the same name, therefore regenerates a
  **new** cover at the **same** path within one wake.
- **File size cannot tell the two apart.** A thumbnail is always a 1-bit BMP of
  `62 + rowBytes × height` bytes. The JPEG path computes that at `JpegToBmpConverter.cpp:88-90`,
  the PNG path at `PngToBmpConverter.cpp:129`. Size is a pure function of the dimensions, so a
  different cover with the same dimensions has the same size.
- `HalFile` has no modification-time accessor. `lib/hal/HalStorage.h` declares
  `size`/`seek`/`seekCur`/`seekSet` (`:77-83`) and nothing date-related.
- Generation goes through two places: `CoverThumb::pathFor` (`src/util/CoverThumb.cpp:29-47`, sets
  `generatedAny`) into `Epub::generateThumbBmp`. The callers are `CoverBand::thumbPathFor`,
  `Masthead::pickCover`, `MeetingsActivity.cpp:253` and `PublicationsActivity.cpp:124`.
  `BibleNavigationActivity.cpp:88-89` only looks the thumbnail up; it never generates one.

## 6. Nearest existing examples

1. **#238 Task 9** (`docs/superpowers/plans/2026-09-30-issue-238-plan.md:1465-1859`), the design
   the brief points at. It was never built: `grep -rln BandInk src lib test` finds nothing. Its parts:
   - a pure, header-only `BandInk` bitset, row-major and MSB-first, with `bytesFor`, `set` and
     `test`, plus its host test (`:1482-1596`);
   - `CoverBand::draw(..., uint8_t* inkOut)`, which records each inked pixel during the blit
     (`:1647-1652`);
   - `CoverBand::drawInk`, which replays it with `drawPixel`, then a shared `finishBand` that masks
     the corners and lays the plate (`:1657-1706`);
   - a `Masthead::BandInkCache` with a `PsramFree` deleter, filled through `reserveInk`
     (`heap_caps_malloc(SPIRAM|8BIT)`, zeroed each fill) (`:1716-1810`);
   - the cache held in `BibleNavCache`, which lives as long as the reader (`:1814-1849`).

   That owner dies when the activity does; this bead needs one that outlives it.
2. **Singletons that outlive an activity.**
   - `HomeVerse` (`src/activities/launcher/HomeVerse.h:12-49`): "RAM state that outlives the screen
     that filled it", modelled on `CatalogIndexStore::getInstance`, and owned by one named task.
     Its key is the date plus the source file's size (`HomeVerse.cpp:40-53,62`).
   - `CatalogIndexStore::getInstance` (`src/network/CatalogIndexStore.cpp:35-38`).
3. **The explicit-PSRAM idiom.** `PsramFree` and `PsramBuffer` are declared at
   `src/network/CatalogIndexStore.h:83-89`. `heap_caps_free` and
   `heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)` are at
   `CatalogIndexStore.cpp:40-47`.
4. **Replaying a decoded image from RAM.** `ImageBlock`'s pxc slot
   (`lib/Epub/Epub/blocks/ImageBlock.cpp:93-188`) is keyed by an FNV-1a hash of the path (`:71-78`).
   It replays through `DirectPixelWriter` (`lib/Epub/Epub/converters/DirectPixelWriter.h:17-187`).
   That writer bypasses `drawPixel`: it uses the renderer's raw-writer accessors
   (`GfxRenderer.h:223-230`) and honours strip mode.
5. **Writing whole framebuffer bytes.** `fillRectImpl`
   (`lib/GfxRenderer/GfxRenderer.cpp:990-1059`) rotates the rect's corners into physical space and
   writes whole bytes with head and tail masks.

## 7. Tests and harness

- `test/cover_band/` holds only `CoverBandGeometryTest`. It is header-only, and its only include
  directory is `${REPO_ROOT}/src` (`test/cover_band/CMakeLists.txt:1-18`). The directory is
  registered at `test/CMakeLists.txt:126`, so a second executable in that directory's
  `CMakeLists.txt` needs **no** edit to the shared `test/CMakeLists.txt`.
- No host test compiles the real `GfxRenderer.cpp`. `test/pagination` uses a link-time fake
  (`test/pagination/GfxRendererFake.cpp`; its `CMakeLists.txt` says GfxRenderer has no virtual
  functions).
- The fake `HalStorage` (`test/stubs/HalStorageFake.cpp`) implements `read`, `seek` and `size`
  (`:195-227`). It does **not** define `seekCur` or `seekSet`, though `test/stubs/HalStorage.h:82-83`
  declares them, and `Bitmap::parseHeaders` calls `seekCur` (`Bitmap.cpp:88,110,115`). A host test
  that runs the real `Bitmap` over a fake file will not link until the fake gains `seekCur`.
- `millis()` is stubbed to return 0 (`test/stubs/Arduino.h:20`).
- Run in this worktree after `./bin/bootstrap`:

  ```
  cmake -S test -B build/test && cmake --build build/test --target CoverBandGeometryTest MastheadLayoutTest HomeLayoutTest -j 8
  CoverBandGeometryTest  [  PASSED  ] 16 tests.
  MastheadLayoutTest     [  PASSED  ] 11 tests.
  HomeLayoutTest         [  PASSED  ] 41 tests.
  ```

## 8. Installed tools

| Tool | Version | Command |
|---|---|---|
| PlatformIO Core | 6.1.19 | `/Volumes/stein/.platformio/penv/bin/pio --version` (not on `PATH`) |
| platform-espressif32 (pioarduino) | 55.03.37 | `platformio.ini:15`; `~/.platformio/platforms/espressif32/platform.json` |
| framework-arduinoespressif32 | 3.3.7 | `~/.platformio/packages/framework-arduinoespressif32/package.json` |
| CMake | 4.4.2 | `cmake --version` |
| Host compiler | Apple clang 21.0.0, arm64-apple-darwin25.5.0 | `c++ --version` |
| GoogleTest | v1.17.0 (FetchContent) | `test/CMakeLists.txt:15-17` |
| clang-format | 21, through `bin/clang-format-fix` | `bin/bootstrap` (`REQUIRED_CLANG_FORMAT_MAJOR=21`), "already present in .venv/bin" |

The render task runs at `normalFreq`, captured from `getCpuFrequencyMhz()` at power-manager init
(`lib/hal/HalPowerManager.cpp:29`). The value was not established here.

## 9. Baseline figures this bead is judged against

From `git show docs/252-responsiveness-baseline:docs/superpowers/notes/responsiveness-baseline.md`
(UC8179, `x4pro` dev build of `1ba1cd34`, `LOG_LEVEL=2`):

- `[MASTHEAD] Band drawn in 225 ms` on every masthead paint (n=11, 223–229 ms). The representative
  line is `[155060] [DBG] [MASTHEAD] Band drawn in 228 ms` (baseline `:72`). The table cites 225 ms
  for the Go to picker, each further picker step, and Publications entry (`:44-46,50`).
- Home's paint is 241–257 ms of CPU, with no band line (`:42,51-52`). The Home band reads and
  decodes the same 773 rows as a masthead (§4), and the plotting is the only part that grows with
  the hero's height. So most of Home's paint is plausibly the band, but **that is inferred, not
  measured**. The new Home log is what settles it.

## 10. Findings that bear on the design (for the spec, not decided here)

1. **The replay mechanism.** Replaying #238's way (a bit test plus `drawPixel` per pixel) does
   roughly 55.7 K bit tests and ≤ 55.7 K `drawPixel` calls per masthead. Today's decode does
   ≥ 358 K `packPixel` calls plus 773 mutex-guarded SD reads. Whether the #238 replay reaches "a few
   ms" is unmeasured, and Home's band is 2.3× the masthead's.

   A row-wise raw write would make the replay a few thousand byte operations: the
   `DirectPixelWriter` / `fillRectImpl` idiom, with each logical column stored as its physical bit
   run (§3, `drawPixel`). It would also be a new renderer-level write path. Either way, the replay
   must keep `drawPixel`'s add-ink-only semantics (§3, step 4) to stay pixel-identical. A
   framebuffer snapshot would also restore paper.
2. **Correctness of the key.** Path + rect + style + file size misses a same-size cover regenerated
   at the same path (§5). The cheap, sound options are explicit invalidation where thumbnails are
   (re)generated or caches cleared (`CoverThumb::pathFor` / `clearBookCache`), or a key that carries
   something which does change, such as the BMP's header bytes plus a content sample.
3. **Capacity.** Four distinct bands per wake, ≤ 36.7 KB (§4). PSRAM free is ~8.2 MB (baseline
   `:17`).
4. **The first paint is also cheap to cut, and pixel-identical.**
   - Rows are fixed-size (`Bitmap.cpp:127`), the files are top-down (§3), and the 1-bit decode has
     no state from row to row (§3).
   - So seeking to `crop.yOffset` and stopping after `band.height` rows would decode 120 rows
     instead of 773 for a masthead, and 272 for Home.
   - This is not what the brief asks for. The spec should say whether it rides along or goes to
     `hpipe discover`.
5. **Proving the replay equals a fresh draw on the host** needs one of these:
   - the blit's row loop factored into a pure, renderer-free function, so a test can drive it with
     synthetic rows (top-down and bottom-up) and compare its output with the replay's;
   - or the real `CoverBand.cpp` + `Bitmap.cpp` linked against a `GfxRenderer` fake and the
     Storage fake. That needs `seekCur` added to `test/stubs/HalStorageFake.cpp`, and
     `CoverThumb::pathFor`, which `CoverBand::thumbPathFor` pulls in, stubbed.

## 11. Tier

This stays **standard**. The work sits in `src/components` (ui-dev). It adds no on-disk format and
no migration, and the cache is RAM-only and dies at sleep.

The edges that touch shared code are small and additive:

- possibly an invalidation call in `src/util` (`CoverThumb.cpp` or `BookCacheUtils.cpp`);
- possibly `seekCur` in `test/stubs/HalStorageFake.cpp`;
- possibly a raw-write helper in `lib/GfxRenderer`, which is still ui-dev's surface.

None of them changes another surface's contract.
