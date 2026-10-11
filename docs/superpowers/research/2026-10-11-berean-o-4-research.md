# berean-o-4 research: close the reader menu without re-rendering the page (R21)

- **Base:** `perf/r21-menu-close-restore`, fast-forwarded to `origin/main` `478cf1ed` (release 1.29.9).
  `git diff --stat 1ba1cd34 478cf1ed -- src lib` is empty, so every line cited below is the code the
  2026-10-11 device baseline ran. `freeink-sdk` is at `67f7e012`.
- **Device measurements:** none taken here. Every millisecond figure is quoted from the baseline
  (`docs/superpowers/notes/responsiveness-baseline.md`, now on `main`) or derived from a cited clock
  rate, and says which.

## Installed tools

| Tool | Version | Evidence |
|---|---|---|
| PlatformIO Core | 6.1.19 | `~/.platformio/penv/bin/pio --version` (not on `PATH`) |
| Platform | pioarduino `platform-espressif32` 55.03.37 | `platformio.ini:15` |
| Arduino core | `framework-arduinoespressif32` 3.3.7, libs `5.5.0+sha.87912cd291` | `~/.platformio/packages/*/package.json` |
| Toolchain | `toolchain-xtensa-esp-elf` 14.2.0+20251107 | same |
| Host tests | CMake 4.4.2, Apple clang 21.0.0, GoogleTest v1.17.0 | `cmake --version`, `c++ --version`, `test/CMakeLists.txt:17` |

## Who owns the behaviour

| File | Role |
|---|---|
| `src/activities/reader/EpubReaderMenuActivity.{h,cpp}` | The sheet. Decides OverPage/Cleared, takes and frees the page-region snapshot. |
| `src/activities/reader/ReaderMenuSheetLayout.h` | Sheet geometry, including the saved `page` box and `snapshotBytes`. |
| `src/activities/reader/EpubReaderActivity.{h,cpp}` | Opens the menu, handles its result, renders the page (`renderBook`, `renderContents`). |
| `src/activities/reader/ReaderActivity.cpp` | `render()` → `renderBook()` → `PostedMessage::drawNext` (`:169-191`). |
| `src/activities/ActivityManager.cpp` | Pops the menu, runs the result handler, requests the reader's re-render. |
| `src/activities/reader/ReaderUtils.h` | `displayWithRefreshCycle` (`:165-177`), the refresh cadence. |
| `lib/GfxRenderer/GfxRenderer.{h,cpp}` | Framebuffer access, region copy, `storeBwBuffer`/`restoreBwBuffer`, gray-plane upload. |
| `lib/hal/HalDisplay.{h,cpp}` | Pass-through to the SDK facade; already takes a pointer for each gray plane (`HalDisplay.h:88-90`). |
| `freeink-sdk/.../FreeInkDisplay.cpp`, `driver/Uc8179Driver.cpp`, `driver/Ssd1677Driver.cpp` | What each display call does on the panel. Read for evidence only; nothing there needs to change. |

## Current control flow

### Opening

1. A centre tap, Confirm, or a Home-key hold calls `openReaderMenu(true)`
   (`EpubReaderActivity.cpp:629-631`, `:613-615`). It pushes `EpubReaderMenuActivity` with
   `pageOnScreen = true` (`:278-304`).
2. The menu's first `render()` runs `decideMode()` on the render task (`EpubReaderMenuActivity.cpp:521-523`).
3. `decideMode` (`:221-288`) takes OverPage only when not rotated, `pageOnScreen`, night mode off, a
   framebuffer exists and the sheet leaves a third of the height (`:256-268`,
   `ReaderMenuSheetLayout.h:97`). It then allocates `snapshotBytes(page.w, page.h)` with
   `makeUniqueNoThrow` and copies the framebuffer region `layout.page` into it with
   `readFramebufferRegion` (`:270-283`). Success logs `sheet mode: over-page (page saved)` (`:286`).
4. Every render writes the region back (`:526-527`), paints the opaque plate and the sheet, and
   refreshes FAST (`:532-541`).

### Closing without an action (the measured case)

1. A page tap or ✕ → `closeTrampoline` → `closeCancelled()`; Back swipe → `handleButtons`; Home →
   `handleHomeGesture` (`EpubReaderMenuActivity.cpp:185-211`, `:371-375`). All set
   `isCancelled = true` with `MenuResult{-1, pendingOrientation, selectedPageTurnOption}` and `finish()`.
2. `ActivityManager::loop` pops: it moves the result out (`ActivityManager.cpp:103`), runs
   `exitActivity` → the menu's `onExit` frees `pageSnapshot` (`:106`, `EpubReaderMenuActivity.cpp:180`),
   then runs the reader's handler (`ActivityManager.cpp:127`), then calls `requestUpdate()`
   unconditionally (`:131-133`).
3. The handler (`EpubReaderActivity.cpp:294-303`) applies an orientation change if any, **always**
   calls `toggleAutoPageTurn(menu.pageTurnOption)`, and calls `onReaderMenuConfirm` only when not
   cancelled.
4. The render task runs `ReaderActivity::render` → `renderBook()` (`ReaderActivity.cpp:189`).
   `renderBook` always clears and rebuilds: `clearScreen()` (`EpubReaderActivity.cpp:1442`),
   `bookmarks.refreshPageFlag` (`:1463`), `section->loadPage` from SD (`:1466`), then
   `renderContents` (`:1491`), then the progress save check, the screenshot hook and the bookmark
   toast (`:1498-1514`). `PostedMessage::drawNext` follows (`ReaderActivity.cpp:190`).

### Which branch of `renderContents` the owner's unit takes

- `tiledGrayscale` needs `renderer.supportsStripGrayscale()` (`EpubReaderActivity.cpp:1644`). That is
  true only for SSD1677 (`Ssd1677Driver.h:87`); UC8179 says so explicitly (`Uc8179Driver.h:82-86`),
  and the facade also returns false while inverted (`FreeInkDisplay.cpp:858-860`).
- So the UC8179 unit runs the non-tiled branch (`:1829-1866`):
  B/W render → `displayWithRefreshCycle` → `storeBwBuffer` → LSB pass into the framebuffer →
  `copyGrayscaleLsbBuffers` → MSB pass → `copyGrayscaleMsbBuffers` → `displayGrayBuffer` →
  `restoreBwBuffer`. Its log line is exactly the baseline's close line:
  `Page render: prewarm=8ms bw_render=44ms display=616ms ... gray_lsb=65ms gray_msb=64ms
  gray_display=357ms total=1160ms` (`:1856-1860`).
- SSD1677 units take the tiled branch (`:1722-1828`): planes rendered in strips, into two full-plane
  buffers when the internal-heap gate passes (`:1739-1746`), uploaded with
  `writeGrayscalePlaneStrip`, then `displayGrayBuffer` and `cleanupGrayscaleWithFrameBuffer`.

## What "page saved" is, where it lives, and why closing does not use it

- **What:** the B/W framebuffer region from the safe area's top to the sheet's top, full safe width
  (`ReaderMenuSheetLayout.h:99`), copied as its panel-oriented, byte-aligned bounding box
  (`GfxRenderer.cpp:1744-1760`). On the X4 Pro that is a 21,655 B allocation
  (`docs/superpowers/specs/2026-09-30-issue-200-design.md`, A-2).
- **Where:** PSRAM, by size rather than by request. `makeUniqueNoThrow` is plain `new (std::nothrow)`,
  and this build has `CONFIG_SPIRAM_USE_MALLOC=y` with `CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=4096`
  (`~/.platformio/packages/framework-arduinoespressif32-libs/esp32s3/sdkconfig:2153-2154`; the
  generated `sdkconfig.x4pro` in the primary checkout agrees at `:2146-2147`, and
  `platformio.ini:94-116` does not override either). The header comment says the same
  (`EpubReaderMenuActivity.h:79-81`).
- **What it is for:** undoing popups and tap flashes over the page while the sheet is up. It is
  written back on every sheet render (`EpubReaderMenuActivity.cpp:525-527`), never handed to anyone.
- **Why closing cannot use it, four reasons:**
  1. It is partial. The region under the sheet is not saved — the sheet paints over it — and the
     close has to repaint exactly that region.
  2. It is B/W only. Nothing in the firmware keeps the gray planes past the render that made them:
     the non-tiled branch renders them into the framebuffer and frees the stored B/W chunks in
     `restoreBwBuffer` (`GfxRenderer.cpp:2331-2356`); the tiled branch's plane buffers are locals of
     `renderContents` (`EpubReaderActivity.cpp:1745-1746`).
  3. It is freed in the menu's `onExit` (`EpubReaderMenuActivity.cpp:180`), which runs before the
     reader's result handler (`ActivityManager.cpp:106` before `:127`).
  4. The pop always requests a render (`ActivityManager.cpp:131-133`), and `renderBook` has no path
     that shows an existing frame; it always clears and re-renders.
- The issue-200 design chose this deliberately: close re-renders "exactly as today", and a
  reader-owned snapshot was rejected for cross-activity ownership and invalidation
  (`2026-09-30-issue-200-design.md`, A-7 and Follow-ups). It also rejected reusing
  `storeBwBuffer`/`restoreBwBuffer` because they rewrite the controller baseline (A-1).

## Can the page be restored and refreshed with no re-render?

### B/W plane

Yes. After `renderContents` returns, the framebuffer holds the page's B/W frame: text, inverted
highlight rects and the status bar (`EpubReaderActivity.cpp:1700-1704`; on the non-tiled branch
`restoreBwBuffer` puts it back at `:1852`). Toasts are drawn after that point
(`:1512-1514`, `ReaderActivity.cpp:190`), each with its own `displayBuffer` (`BaseTheme.cpp:549`),
so a copy taken at the end of `renderContents` is the clean page. `GfxRenderer::getFrameBuffer()`
and `getBufferSize()` are public (`GfxRenderer.h:371-372`).

Copying it back and calling `displayWithRefreshCycle` reproduces today's B/W step. On UC8179,
`display()` diverts a FAST to the gray-transition waveform only while `_redriveAfterGray` is set
(`Uc8179Driver.cpp:174-176`). The menu's own FAST clears it (`displayStart` `:317`, or
`transitionGrayscaleBase` `:204`), so the close's B/W step is the ordinary DU partial (`8179_DRF`,
551–566 ms in the baseline), as it is today.

### Gray planes

Yes, if they are captured when the page is rendered. They are pure render output:

- **What the drivers are given:** two 1-bpp masks, each laid out like the framebuffer
  (`renderer.clearScreen(0x00)` then a pass in `GRAYSCALE_LSB`/`GRAYSCALE_MSB` mode,
  `EpubReaderActivity.cpp:1837-1846`; the strip target uses the same row stride, `:1728-1736`).
- **UC8179:** `displayStart` snapshots whatever B/W frame it displays into its PSRAM `_grayBase`
  (`Uc8179Driver.cpp:273-276`). `copyGrayscaleLsb`/`copyGrayscaleMsb` fold the masks against it
  (`:473-515`), `displayGray` runs the 300 ms waveform and writes the B/W base back to both RAM
  planes (`:517-578`), and `cleanupGrayscaleBuffers` is then a no-op (`:590`). Replaying
  "restore B/W, display, upload the two saved masks, `displayGrayBuffer`, cleanup" is the same call
  sequence `renderContents` makes, without the renders.
- **SSD1677:** `copyGrayscaleLsb` writes BW RAM for the full frame and `copyGrayscaleMsb` writes RED
  RAM (`Ssd1677Driver.cpp:580-590`). That is the same as `writeGrayscalePlaneStrip` over rows
  `0..h` (`:592-600`), so one replay works on both branches.
- **API:** `GfxRenderer::copyGrayscaleLsbBuffers()`/`MsbBuffers()` take no argument and always pass
  the framebuffer (`GfxRenderer.cpp:2267-2269`). `HalDisplay` already takes a pointer
  (`HalDisplay.h:89-90`), so a pointer overload in `lib/GfxRenderer` would avoid copying each mask
  through the framebuffer. No SDK change is needed.
- **Night mode:** the facade skips every gray upload and the gray waveform while inverted
  (`FreeInkDisplay.cpp:773-781`, `:825-835`), and a polarity change turns FAST into HALF
  (`:573-575`).

### What a restore cannot skip on UC8179

The panel waveforms. The panel shows page-above plus sheet. The region under the sheet has to go
from sheet to page (a B/W waveform), and it has to get its grays back (the gray waveform). There
is no windowed B/W update on this panel (`.claude/agents/ui-dev.md`, and the full-panel `PTIN`
with no window at `Uc8179Driver.cpp:345`). The gray LUTs are short and are applied on top of a
settled B/W base (`:37-45`). So a restore still runs FAST (~551 ms) and the gray pass (~300 ms).
Skipping either would leave the sheet ghosted or the page flat, which acceptance criterion 3
forbids.

Whether the grays above the sheet survive the menu's opening transition (`8179_gray_pre_DRF`,
674 ms, `Uc8179Driver.cpp:182-212`, `:410-459`) can only be seen on the device. It does not change
the answer, because the region that was under the sheet needs the gray pass anyway.

## What the close costs today, and what a restore would save

Measured (baseline, AA on, n=6, 1,152–1,171 ms): `prewarm=8 bw_render=44 display=616 ...
gray_lsb=65 gray_msb=64 gray_display=357 total=1160`.

| Part | Today | With a restore | Why |
|---|---|---|---|
| Before `renderContents`: SD `loadPage`, bookmark flag, highlight query and rects | not in `total=`, unmeasured | 0 | `EpubReaderActivity.cpp:1463-1466`, `:1673-1698` |
| `prewarm` | 8 ms | 0 | font scan pass, `:1627-1635` |
| `bw_render` | 44 ms | ~3 ms | one 48,000 B copy; `bw_store=2ms`/`bw_restore=3ms` in the baseline page-turn line are the same size of PSRAM copy |
| `display` | 616 ms | ~616 ms | waveform + two plane streams |
| `gray_lsb` + `gray_msb` | 129 ms | ~50–60 ms | the renders go; each upload stays (60,000 B per plane on the wire, `Uc8179Driver.cpp:236-246`, 600 gates × 100 B, at 20 MHz, `BoardConfig.h:1379`, ≈ 24 ms, plus the OR/XOR loop over 48,000 B) |
| `gray_display` | 357 ms | ~357 ms | waveform + base rewrite |

**Estimate, not measured:** AA on, ~1,030 ms against 1,160 ms (−110 to −160 ms, about 10–14 %),
plus whatever the unmeasured SD page load costs. AA off — the owner's current setting — the close
has never been measured. The page-turn line with AA off is `prewarm=65 bw_render=45 display=631
total=741`; a restore would drop the ~110 ms of prewarm and render, plus the page load. Of the
1,160 ms the roadmap cites, 851 ms is the two waveforms, which no restore removes on this panel.

## What must invalidate the saved page

The reader's `loop()` does not run while the menu is the current activity (`ActivityManager.cpp:89`
calls only `currentActivity->loop()`), so background section builds, the idle prewarm and toast
expiry are frozen until the menu closes. Everything that can change the page comes through the
menu's result or through `SETTINGS`.

| Event | What changes | Evidence | Restore valid? |
|---|---|---|---|
| Cancel, nothing changed | nothing | — | **yes** — the target case |
| Orientation picked | reflow; `section.reset()` | handler `:296-298`, `applyOrientation` `:1075-1092` | no. Compiled out on the X4 Pro (`BEREAN_CAP_ROTATION 0`, `CrossPointSettings.h:18-24`), but the path exists. |
| Auto turn picked | status-bar title; margins and `section.reset()` when the status bar is off or progress-only | `toggleAutoPageTurn` `:1094-1111`, `renderStatusBar` `:1912-1917` | no |
| Any cancel while auto turn was on | the handler always calls `toggleAutoPageTurn(option)`; the menu starts at option 0 (`EpubReaderMenuActivity.h:85`), and `start(0)` turns it off (`AutoPageTurn.h:14-18`), which changes the status bar. Reachable through the Home-key hold, which is checked after the auto-turn block (`:548-572`, `:606-615`). | as cited | no |
| Night mode toggled in the menu | output polarity only; the framebuffer stays normal polarity | `EpubReaderMenuActivity.cpp:332-337`, `ActivityManager.cpp:58`, `FreeInkDisplay.cpp:316-324` | B/W content still valid; grays are skipped by the facade when inverted. The spec should choose: restore, or re-render conservatively. |
| Frontlight toggled | nothing on the page | `EpubReaderMenuActivity.cpp:339-346` | yes |
| Mark (`TOGGLE_BOOKMARK`) | status-bar bookmark icon; bookmark toast | `:922-925`, `addBookmark` `:2016-2036`, `renderStatusBar` `:1930-1931` | no (re-render, as today) |
| Screenshot | needs `renderBook`'s screenshot hook | `:908-915`, `:1507-1510` | no |
| Go to, Search, Footnotes, Go to %, Text settings, Tag, Tags, Tags here, Bookmarks, Recent, Home, Delete cache | another activity runs, or the reader navigates or exits | `onReaderMenuConfirm` `:767-931` | no. A cancelled sub-screen returns through `reopenReaderMenu`, which re-renders first (`:306-326`). |
| A toast was showing when the menu opened | the toast must reappear if still due | `:1512-1514`, `ReaderActivity.cpp:190`, `ReaderBookmarks.h:34-36` | yes, if the restore path still runs those two steps |
| Battery level or charging icon in the status bar | stale by however long the menu was open | `BaseTheme.cpp:642-660`; the clock is X3-only (`:661-663`) | acceptable — any page left on screen behaves the same |
| Last render was not a page (empty chapter, out of bounds, build error, end of book) | no page frame exists | `:1445-1461`, `:1221-1225`, `ReaderActivity.cpp:170-187` | no; `pageRendered` (`EpubReaderActivity.h:60-62`, set at `:1495`) only marks a completed page render |
| The gray capture failed (B/W store OOM, strip scratch OOM) | page shown without grays | `:1831-1834`, `:1778-1790` | B/W only, or re-render |
| Page with images | its own sequence: placeholder FAST, HALF/FAST base, gray pass for images even with AA off | `:1637-1664`, `:1707-1712` | simplest to exclude |

**Refresh cadence.** Today a close goes through `displayWithRefreshCycle`, so it counts toward the
HALF every `refreshFrequency` pages (`ReaderUtils.h:165-177`, default `REFRESH_15`,
`CrossPointSettings.h:311`). A restore that keeps calling it keeps that behaviour, including an
occasional HALF on close.

## Memory

| Buffer | Size | Where | Lifetime |
|---|---|---|---|
| Menu page-region snapshot (today) | 21,655 B | PSRAM, by the 4,096 B threshold | the menu |
| `storeBwBuffer` chunks (today, non-tiled AA) | 6 × 8,000 B (`GfxRenderer.h:42`) | PSRAM, by the threshold | one render |
| Tiled plane buffers (today, SSD1677) | 2 × 48,000 B | PSRAM, by the threshold | one render |
| A restore cache, AA off | 48,000 B | explicit PSRAM, as the brief requires | see below |
| A restore cache, AA on | 3 × 48,000 B = 144,000 B | explicit PSRAM | see below |

- **Lifetime is forced by the gray planes.** They exist only during `renderContents`, before anyone
  knows the menu will open. So the cache must be written on every page render and live as long as
  the reader. PSRAM free while reading is ~8.2 MB (baseline); 144,000 B is under 2 % of it, and
  internal SRAM pays nothing beyond a pointer or two.
- **Per-turn cost.** Three 48,000 B PSRAM copies per AA page turn would add roughly 6–9 ms to every
  turn, judging by `bw_store=2ms`/`bw_restore=3ms`. Two routes cost less. Keeping the
  `storeBwBuffer` chunks makes the B/W copy free. Rendering the gray passes straight into the cache
  planes, as the tiled branch already renders into its plane buffers (`:1728-1737`), makes the gray
  copies free too, and removes `storeBwBuffer`/`restoreBwBuffer` from the non-tiled branch. That is
  a design choice for the spec.
- **Explicit PSRAM idiom:** `heap_caps_malloc(..., MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)` behind a
  `PsramFree` deleter (`src/network/CatalogIndexStore.h:83-89`, `.cpp:40-46`).
- **Unverified, aside:** on the X4 Pro the framebuffer itself comes from plain `malloc(48000)`
  (`FreeInkDisplay.cpp:368-380`; `FREEINK_FB_PSRAM` is 0 here, `BoardConfig.h:294-295`). By the same
  threshold that likely puts it in PSRAM, not internal SRAM as `CLAUDE.md`'s table says. It does not
  change this design, but it means rendering into PSRAM cache planes should cost about what
  rendering into the framebuffer does. A device heap check would settle it.

## Nearest existing examples

1. **Save a frame, restore it instead of re-rendering:** the menu's own snapshot
   (`EpubReaderMenuActivity.cpp:270-283`, `:525-527`), modelled on `PassageSelectActivity`'s outline
   fast path (`PassageSelectActivity.cpp:44-60`, `:498`, `:530-542`). Both use
   `readFramebufferRegion`/`writeFramebufferRegion` and skip the page render entirely.
2. **Full-plane gray buffers owned by the caller and uploaded later:** the tiled branch's
   `renderPlaneToBuffer` and `writeGrayscalePlaneStrip` (`EpubReaderActivity.cpp:1728-1766`).
3. **Explicit PSRAM ownership:** `CatalogIndexStore::PsramBuffer` (`CatalogIndexStore.h:83-89`).
4. **A cache that lives for the reader's life:** `navCache` (`EpubReaderActivity.h:50`, #238,
   `08ac400`).
5. **A pure decision helper with a host test:** `LauncherRefresh.h` with `test/launcher_refresh/`, and
   `ReaderMenuSheetLayout.h` with `test/ui_layout/`. A "may this close restore?" predicate over the
   cases in the table above fits this shape. Any new suite needs a `test/CMakeLists.txt` line, which
   goes to the orchestrator.

## Tier and overlap

- **Tier stays `standard`.** The change is confined to the `ui` surface (`src/activities/reader`,
  `lib/GfxRenderer`). It has no on-disk format, no store, no SDK change and no contract with
  another surface.
- **Overlap with this run's siblings.** None of t1, t2, t5, t6 or t7 declares
  `EpubReaderActivity`, `EpubReaderMenuActivity` or `lib/GfxRenderer`. **t3 (R12, heavy)** declares
  `src/activities/reader/ReaderActivity`. If this work changes `ReaderActivity.cpp` (its `render()`
  calls `renderBook` and `drawNext`), it shares t3's file lock. Keeping the restore inside
  `EpubReaderActivity::renderBook` avoids that.

## Open questions for the spec

- Capture strategy: copy three planes per render, or render the gray passes straight into the cache.
- Whether night mode, toggled in the menu or on before it, restores or re-renders.
- Whether image pages are excluded.
- The log line that proves the restore. Suggested: one `LOG_DBG("ERS", "Page restore: ... total=")`
  with no `bw_render`/`gray_lsb` fields, so the PR can quote it next to the baseline line.
