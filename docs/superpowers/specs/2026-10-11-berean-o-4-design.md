# berean-o-4 design: close the reader menu without re-rendering the page (R21)

- **Research:** `docs/superpowers/research/2026-10-11-berean-o-4-research.md` (the "research note").
- **Baseline:** `docs/superpowers/notes/responsiveness-baseline.md`. Owner's X4 Pro: UC8179 panel, `x4pro`
  dev build of `1ba1cd34`.
- **Base:** `origin/main` `478cf1ed`. `src/` and `lib/` are unchanged since `1ba1cd34`.
- **Tier:** `standard`, surface `ui`. No store, no on-disk format, no SDK change.
- **Modelled on:**
  - the restore sequence: `renderContents`' own branches (`src/activities/reader/EpubReaderActivity.cpp:1753-1769`,
    `:1779-1819`, `:1829-1853`);
  - the request flag: `pendingScreenshot` (`:909-912` set, `:1507-1510` consumed);
  - PSRAM ownership: `CatalogIndexStore::PsramBuffer` (`src/network/CatalogIndexStore.h:83-89`, `.cpp:40-46`);
  - the decision helper and its host test: `ReaderMenuSheetLayout.h` with `test/ui_layout/`, and
    `LauncherRefresh.h` with `test/launcher_refresh/`;
  - a cache that lives for the reader's life: `navCache` (`EpubReaderActivity.h:48-50`).

## Problem

Closing the reader menu without choosing anything re-renders the page from scratch. On the owner's
unit, with anti-aliasing (AA) on, the baseline logs
`Page render: prewarm=8ms bw_render=44ms display=616ms ... gray_lsb=65ms gray_msb=64ms gray_display=357ms total=1160ms`
(n=6, 1,152–1,171 ms).

The chain, as it stands:

1. The close sets a cancelled result and `finish()`es (`EpubReaderMenuActivity.cpp:200-206`).
2. `ActivityManager` pops the menu. Its `onExit` frees the snapshot (`ActivityManager.cpp:106`,
   `EpubReaderMenuActivity.cpp:180`). The reader's handler runs (`ActivityManager.cpp:127`), then
   `requestUpdate()` (`:131-132`).
3. `ReaderActivity::render` → `renderBook` (`ReaderActivity.cpp:189`). That clears the screen, reloads the page
   from SD and runs `renderContents` (`EpubReaderActivity.cpp:1442`, `:1466`, `:1491`).

The menu's own "page saved" cannot replace this. It is a B/W slice above the sheet only, and it is freed before
the reader runs (research note, "What 'page saved' is").

## Goal

| ID | Goal | Acceptance criterion |
|---|---|---|
| G-1 | A cancelled close that changed nothing shows the page from a copy the reader kept. No SD page load, no B/W render, no gray render. It logs `Page restore:` and no `Page render`. | AC1 |
| G-2 | Every other close, and every menu action, re-renders exactly as today. | AC2 |
| G-3 | With AA on, the restored page runs its gray pass from planes saved when the page was rendered. | AC3 |
| G-4 | Correct on SSD1677 units, through the same branches those units already run. | brief |
| G-5 | Time to settled drops. The PR quotes the measured `Page restore: ... total=` line against the baseline's 1,160 ms. | AC1 |

## Non-goals

- **Making the panel waveforms faster.** A restore still runs the B/W refresh (~551 ms) and, with AA on, the
  gray pass (~300 ms). The research note explains why neither can be skipped on UC8179 ("What a restore cannot
  skip").
- **Restoring anywhere but a cancelled reader-menu close.** That excludes `reopenReaderMenu` after a sub-screen
  cancel, toast expiry, `PassageSelectActivity`'s return and Mark. See Follow-ups.
- **Changing the menu.** `EpubReaderMenuActivity` is not touched. Its region snapshot, its modes and its opening
  refresh stay as they are.
- **Image pages, night mode and the SSD1677 overlap gate** (F9/R4). Each keeps today's behaviour; see A-6, A-12
  and A-4.
- **Refreshing the status bar's battery on close** (A-21).
- **Any `freeink-sdk` or `ReaderActivity.cpp` change.** `ReaderActivity` is sibling t3's file.

## Architecture

| File | Change |
|---|---|
| `src/activities/reader/MenuCloseRestore.h` | **New.** Pure, header-only, no firmware includes. It holds two things. `decide()` answers "may this close restore?" from what the menu changed. `fit()` answers "does the saved page match where the reader is?" Each returns an enum with a log name. |
| `src/activities/reader/PageFrameCache.{h,cpp}` | **New.** Owns up to three 48,000 B PSRAM planes: B/W, gray LSB, gray MSB. It also owns the `MenuCloseRestore::CapturedPage` record describing them. Capture, invalidate, release, read-only accessors. No renderer calls. |
| `src/activities/reader/EpubReaderActivity.h` | **Changed.** New members `PageFrameCache pageCache;` and `bool pageRestoreRequested = false;`, plus one private method `bool restorePageFromCache();`. |
| `src/activities/reader/EpubReaderActivity.cpp` | **Changed.** Capture calls in `renderContents`. Before-state and the decision in `openReaderMenu`'s handler. The restore attempt and the invalidation at the top of `renderBook`. `pageCache.release()` in `onExit`. |
| `lib/GfxRenderer/GfxRenderer.{h,cpp}` | **Changed.** One pass-through: `void copyGrayscaleBuffers(const uint8_t* lsb, const uint8_t* msb) const` → `display.copyGrayscaleBuffers(lsb, msb)`. Mirrors the no-argument `copyGrayscaleLsbBuffers()` (`GfxRenderer.cpp:2267`) and `HalDisplay::copyGrayscaleBuffers` (`HalDisplay.h:87`, `.cpp:102-104`). |
| `test/ui_layout/MenuCloseRestoreTest.cpp`, `test/ui_layout/CMakeLists.txt` | **New test and its block.** Copies the `ReaderMenuSheetLayoutTest` block (`test/ui_layout/CMakeLists.txt`). This is the suite's own file, not the shared `test/CMakeLists.txt`. |

Unchanged: `EpubReaderMenuActivity.*`, `ReaderMenuSheetLayout.h`, `ReaderActivity.*`, `ActivityManager.*`,
`ReaderUtils.h`, `lib/hal`, `freeink-sdk`.

## Data and control flow

### 1. Capture: every page render (render task, under `RenderLock`)

```
renderBook
  pageRestoreRequested consumed (always, first statement)            A-14
  ... restore attempt (section 3) ...
  pageCache.invalidate()                                             A-8
  ... section load/build, unchanged ...
  renderContents
    page->render + highlights + renderStatusBar        (:1700-1704)
    if !pageHasImages: pageCache.captureBw(fb, spine, page)          A-3, A-6
    display (B/W)                                      (:1707-1719)
    tiled, planes buffered (both):  after renderPlaneToBuffer x2 (:1749-1750)
        pageCache.captureGrayPlane(lsb, lsbPlaneBuf); (msb, msbPlaneBuf)   A-4
    non-tiled: after the LSB pass (:1839-1840) captureGrayPlane(lsb, fb)
               after the MSB pass (:1845-1846) captureGrayPlane(msb, fb)    A-4
    at every displayGrayBuffer() (:1766, :1815, :1849): pageCache.noteGrayShown()  A-5
```

### 2. Close decision: the menu's result handler (loop task)

```
openReaderMenu
  autoTurnBefore = autoTurn.active(); nightBefore = SETTINGS.screenInverted != 0   A-15
  handler [this, autoTurnBefore, nightBefore](result):
    orientationChanged = SETTINGS.orientation != menu.orientation
    if orientationChanged: applyOrientation(...)              (unchanged)
    toggleAutoPageTurn(menu.pageTurnOption)                   (unchanged)
    verdict = MenuCloseRestore::decide({cancelled, orientationChanged,
                autoTurnBefore, autoTurn.active(), nightBefore,
                SETTINGS.screenInverted != 0, isAtEndOfBook()})          A-11
    LOG_DBG("ERS", "Menu closed: %s", name(verdict))                     A-23
    if verdict == Restore: { RenderLock lock; pageRestoreRequested = true; }   A-14
    if !cancelled: onReaderMenuConfirm(menu)                  (unchanged)
ActivityManager: requestUpdate()                              (ActivityManager.cpp:131-132)
```

### 3. Restore (render task, `renderBook`)

```
renderBook
  restoreRequested = std::exchange(pageRestoreRequested, false)
  if !epub: return                                            (:1219)
  spine clamps; end-of-book return                            (:1227-1232)
  if restoreRequested && restorePageFromCache():                         A-16
      if bookmarks.toastVisible(): GUI.drawToast(...)          (as :1512-1514)   A-20
      return
  pageCache.invalidate()
  ... today's path ...

restorePageFromCache
  fit = MenuCloseRestore::fit(pageCache.captured(), {section != nullptr, currentSpineIndex, section->currentPage})
  if fit != Fits or !renderer.hasFrameBuffer(): LOG_DBG refused (reason); return false     A-22
  memcpy(renderer.getFrameBuffer(), pageCache.bw(), renderer.getBufferSize())             A-7
  ReaderUtils::displayWithRefreshCycle(renderer, pagesUntilFullRefresh)                   A-18
  if MenuCloseRestore::restoresGray(captured):                                            A-17
      supportsStripGrayscale ? writeGrayscalePlaneStrip(true, lsb, 0, h); (false, msb, 0, h)
                             : renderer.copyGrayscaleBuffers(lsb, msb)                     A-19
      renderer.displayGrayBuffer()
      renderer.cleanupGrayscaleWithFrameBuffer()
  LOG_DBG("ERS", "Page restore: copy= display= gray_write= gray_display= total=")         A-23
  lastRenderCompleteMs = millis(); pageShown = true; pageRendered = true                  A-20
  return true
```

## Assumptions

Each one is a decision a reviewer can attack.

### Capture

- **A-1: the reader owns the copy, not the menu.** The gray planes exist only inside `renderContents`
  (`:1745-1746` locals; the non-tiled branch overwrites the framebuffer with them, `:1837-1846`). The menu never
  sees them. The issue-200 design also rejected cross-activity snapshot ownership
  (`docs/superpowers/specs/2026-09-30-issue-200-design.md`, Follow-ups). `EpubReaderActivity` already keeps
  `navCache` for its own life (`EpubReaderActivity.h:48-50`); `pageCache` sits beside it.

- **A-2: capture on every page render, by copy.** That is one 48,000 B copy with AA off and three with AA on.
  - **Cost, estimated:** ≤ 2 ms per copy. The baseline's `bw_store=2ms` is the same 48,000 B copy into PSRAM
    plus six `malloc`s (`GfxRenderer.cpp:2298-2323`). That adds ≤ 2 ms to a 741 ms AA-off turn and ≤ 6 ms to a
    1,378 ms AA-on turn. V-5 measures it.
  - On SSD1677's buffered tiled path the two gray copies run before `waitRefreshComplete()` (`:1753`), so they
    overlap the B/W waveform.
  - *Rejected:*
    - **Capture when the menu opens.** The gray planes no longer exist then. The framebuffer may also hold a
      toast drawn after the page (`:1512-1514`, `ReaderActivity.cpp:190`).
    - **Render the gray passes straight into the cache planes**, as the tiled branch renders into its buffers
      (`:1728-1737`). That would avoid the copies and drop `storeBwBuffer`/`restoreBwBuffer` from the non-tiled
      branch. It would also rewrite the AA page-turn pipeline the owner's panel runs on every turn, to save a few
      milliseconds. It is recorded as a follow-up, worth doing if V-5 shows the copies cost more than estimated.
    - **Keep `storeBwBuffer`'s chunks.** That saves one of the three copies. It would need a new renderer API,
      because `restoreBwBuffer` frees the chunks and also runs the cleanup (`GfxRenderer.cpp:2331-2356`).

- **A-3: capture B/W after the status bar and before the B/W display.**
  - At `:1704` the framebuffer holds text, the inverted highlight rects and the status bar (`:1700-1704`). That
    is the frame the panel is about to show.
  - The async display has not started yet, so nothing reads the framebuffer while the panel streams it
    (`HalDisplay.h` `displayBufferAsync` contract).
  - The non-tiled branch's `restoreBwBuffer` (`:1852`) restores exactly these bytes; they are stored after
    `displayWithRefreshCycle`, which does not modify them outside night mode (`FreeInkDisplay.cpp:577-579`).
  - Toasts are drawn after `renderContents` returns, so they are never captured.

- **A-4: gray capture points, and the branches that cannot capture.**
  - **Non-tiled** (UC8179, the owner's unit): after each pass, the framebuffer holds that plane's mask
    (`:1837-1846`). It is copied after the `copyGrayscale*Buffers()` call that reads it.
  - **Tiled with both plane buffers** (SSD1677, internal-heap gate passed, `:1739-1746`): both buffers are copied
    after they are rendered (`:1749-1750`).
  - **Tiled with one buffer** (`:1759-1761`), **or with the strip scratch** (`:1777-1827`): no full plane exists,
    so nothing is captured. `fit()` returns `GrayMissing` and the close re-renders, as today. The SSD1677 overlap
    gate itself is not touched (F9/R4 stay out of scope).

- **A-5: "gray shown" is recorded where the gray waveform is requested.** `noteGrayShown()` sits beside each
  `displayGrayBuffer()` (`:1766`, `:1815`, `:1849`).
  - A page whose gray pass was skipped is shown flat: `storeBwBuffer` failed (`:1831-1834`) or the strip scratch
    was OOM (`:1778-1790`). Its `grayShown` stays false, and the restore shows it flat, as the panel showed it.
  - A page shown with grays restores only if both planes were captured (`fit()` → `GrayMissing` otherwise).
  - The restore runs the gray pass if and only if `grayShown` (`restoresGray()`).

- **A-6: image pages are not captured.**
  - They have their own sequence: a placeholder FAST (`:1659-1664`), then a HALF or FAST base (`:1707-1712`),
    then a gray pass for images even with AA off (`:1643`, `:1655`).
  - Replaying that faithfully is a second design for pages the Bible rarely has. With no B/W capture, `fit()`
    returns `Empty` and the close re-renders.

- **A-7: raw full-frame copies through `getFrameBuffer()`/`getBufferSize()`** (`GfxRenderer.h:371-372`,
  `GfxRenderer.cpp:2236-2238`). The region API is not used.
  - The gray planes are raw panel-layout planes handed to the display, so the B/W plane uses the same layout.
  - A full-screen region copy would move the same 48,000 B row by row, through an orientation transform that
    does nothing here.
  - Precedent for raw framebuffer access outside the renderer: `ScreenshotUtil.cpp:72` reads it, and
    `restoreBwBuffer` writes it with `memcpy` (`GfxRenderer.cpp:2346-2350`).

- **A-8: invalidate at the top of every `renderBook` that does not restore.** Then any render that does not end
  in a captured page leaves nothing to restore:
  - an empty chapter or an out-of-bounds page (`:1445-1461`);
  - a build error (`:1221-1225`);
  - a page-load retry (`:1466-1484`);
  - an image page.

  The end-of-book screen never reaches `renderBook` (`ReaderActivity.cpp:170-187`). That is covered by A-11's
  `atEndOfBook` and by `fit()`'s position check.

### Memory

- **A-9: explicit PSRAM, allocated on first use, kept for the reader's life.**
  - `heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)` behind a `PsramFree` deleter, copied from
    `CatalogIndexStore` (`.h:83-89`, `.cpp:40-46`).
  - The B/W plane is allocated on the first `captureBw`. The two gray planes are allocated together on the first
    `captureGrayPlane`; if the second allocation fails, the first is freed.
  - Planes are never reallocated per turn, which follows CLAUDE.md's "allocate once and reuse" rule. They are
    freed in `EpubReaderActivity::onExit` (`pageCache.release()`, under `RenderLock`, `:1610-1615`), and again
    by the destructor if `onExit` was skipped.
  - A failed allocation logs `LOG_ERR` once and latches for the session, so there is no per-turn retry or log
    spam. A failed B/W plane means the cache never fits; a failed gray pair means AA pages refuse with
    `gray-missing` while pages shown without grays still restore. A close that cannot restore re-renders, as today.
  - Turning AA off mid-session keeps the gray planes. They are only reused if AA comes back on.

- **A-10: the budget, and why it is heap.**
  - 48,000 B of PSRAM for an AA-off session. 144,000 B with AA on, which the brief anticipates
    (2 × 48,000 B for the gray planes).
  - PSRAM free while reading is ~8.2 MB (baseline), so this is under 2 %. Internal SRAM cost is the new members
    only: three pointers, a size, two latches and the `CapturedPage` record, well under 64 B.
  - *Stack:* rejected. 48,000 B against an 8,192 B render-task stack (`ActivityManager.cpp:33`).
  - *Static:* rejected. It would pin 144,000 B for the firmware's whole life, outside the reader, and a plain
    static array goes to internal `.bss`.

### Close decision

- **A-11: restore only on a cancelled close that changed nothing the page shows.** `decide()` checks in this
  order:
  1. **Not cancelled** → `Action`. A confirmed action takes its existing path (`onReaderMenuConfirm`,
     `:767-931`).
  2. **At end of book** → `EndOfBook`. The menu can be opened there by the Home-key hold, which is checked before
     `handleEndOfBookMenu` (`:606-615`, `:625`).
  3. **Orientation changed** → `Orientation`. It resets the section (`:1075-1092`). This is compiled out on the
     X4 Pro (`CrossPointSettings.h:18-24`) but handled.
  4. **Auto turn active before or after** → `AutoTurn`.
     - The handler always calls `toggleAutoPageTurn(option)` (`:299`).
     - The menu starts at option 0 (`EpubReaderMenuActivity.h:85`), and `start(0)` turns it off
       (`AutoPageTurn.h:14-18`).
     - So any close with auto turn on changes the status-bar title (`:1912-1917`), and may change margins and
       reset the section (`:1100-1110`).
  5. **Night mode on before or after** → `NightMode` (A-12).
  6. Otherwise → `Restore`.

  The order only decides which reason is logged. Any of the five blocks a restore.

- **A-12: night mode never restores.**
  - With night mode on, the menu is already Cleared (`EpubReaderMenuActivity.cpp:260-263`).
  - The reader's next refresh is a HALF either way, because the render task switches polarity between the menu
    and the reader (`ActivityManager.cpp:58`, `FreeInkDisplay.cpp:316-324`, `:573-575`).
  - The facade skips every gray step while inverted (`FreeInkDisplay.cpp:773-781`, `:825-835`).
  - A restore would save only the ~50 ms render under a ~1.5 s HALF. Excluding it keeps the gray bookkeeping
    honest: `noteGrayShown()` would otherwise record a gray pass that never reached the panel.

- **A-13: Frontlight does not block a restore.** It changes the light, not the page (`EpubReaderMenuActivity.cpp:339-346`).
  If its settings save fails, the posted message is drawn after `renderBook` by `ReaderActivity::render`
  (`ReaderActivity.cpp:190`) whether or not the page was restored.

- **A-14: the request is a flag, modelled on `pendingScreenshot`.**
  - The handler sets `pageRestoreRequested` under `RenderLock`, as `:909-912` sets `pendingScreenshot`. The
    handler runs with the pop's lock released (`ActivityManager.cpp:126`).
  - `renderBook` consumes the flag as its **first** statement, with `std::exchange`, before even the `!epub`
    return. So a request never outlives the render that follows it.
  - A reader render that slips in between the pop's unlock and the handler only does a normal render and
    recaptures. The handler's request then restores that same page on the next render. That is correct, and no
    worse than today.

- **A-15: the before-state is taken in `openReaderMenu` and captured by value in the handler.**
  `autoTurn.active()` and `SETTINGS.screenInverted` are read there. Capturing extra values follows `[this, cancelTo]`
  (`:963`). The handler type is the existing `ActivityResultHandler` (`ActivityResult.h:106`); no new callback
  type is introduced.

### Restore

- **A-16: the restore runs at the top of `renderBook`.** It comes after the spine clamps and the end-of-book
  return, and before the margins and section work.
  - **Nothing in the section logic can change while the menu is up.** `ActivityManager::loop` runs only the
    current activity's `loop()` (`ActivityManager.cpp:89`), and section builds advance only from the reader's
    loop and `renderBook`. `grep -rn xTaskCreate lib/Epub src/activities/reader` finds no build task.
  - **It also skips `chapterPassageCount = 0` (`:1443`).** Only `renderContents` sets that count again
    (`:1683`), and the next menu reads it (`:284`).
  - `fit()` still checks that a section exists and that `currentSpineIndex` and `section->currentPage` equal the
    captured page. If anything moved, the restore is refused and today's path runs.

- **A-17: the restore replays each panel's live branch, minus the renders.**
  - **No strip grayscale** (UC8179; `supportsStripGrayscale()` false, `Uc8179Driver.h:82-86`). Mirrors the
    non-tiled branch (`:1829-1853`):
    1. the B/W display;
    2. `copyGrayscaleBuffers(lsb, msb)`, the same driver calls as `copyGrayscaleLsbBuffers()`/`Msb` with the
       planes passed directly (`FreeInkDisplay.cpp:796-801`);
    3. `displayGrayBuffer()`;
    4. `cleanupGrayscaleWithFrameBuffer()`. `restoreBwBuffer` ends with the same cleanup once the framebuffer
       holds the B/W page (`GfxRenderer.cpp:2352`).
  - **Strip grayscale** (SSD1677). Mirrors the tiled strip branch with a blocking display (`:1779-1819`), but
    writes each plane as one full-height strip, as the buffered branch already does (`:1756-1758`). Then
    `displayGrayBuffer()` and `cleanupGrayscaleWithFrameBuffer()`.
  - The UC8179 driver re-snapshots the B/W base from the displayed frame (`Uc8179Driver.cpp:273-276`) and folds
    the masks against it (`:473-515`). That is why replaying the same calls with saved masks is equivalent.

- **A-18: the B/W step goes through `displayWithRefreshCycle`, blocking.** A close keeps counting toward the HALF
  every `refreshFrequency` pages, as today's re-render does (`ReaderUtils.h:165-177`). When that HALF is due, the
  restore's B/W step is the HALF. It is never async, because there is no CPU work to overlap.

- **A-19: one new renderer pass-through, `copyGrayscaleBuffers(lsb, msb)`.**
  - `GfxRenderer::copyGrayscaleLsbBuffers()` always passes the framebuffer (`GfxRenderer.cpp:2267-2269`).
  - Without the pass-through, the restore would have to copy each mask into the framebuffer, upload it, and
    copy the B/W page back: three extra 48,000 B copies.
  - `HalDisplay` already takes both pointers (`HalDisplay.h:87`).

- **A-20: after a restore, the reader's state is left as a render leaves it.**
  - `lastRenderCompleteMs`, `pageShown` and `pageRendered` are set as at `:1493-1495`.
  - The bookmark toast is drawn if it is still showing (`:1512-1514`). The toast does not expire while the menu
    is up, because the reader's loop does not run then (`:574`).
  - Posted messages are drawn by `ReaderActivity::render` after `renderBook` (`ReaderActivity.cpp:190`).
  - The progress save is skipped, because the page did not change (`:1498-1505` would find nothing to save).
  - `pendingScreenshot` cannot be set: only a confirmed action sets it (`:908-915`).
  - `chapterPassageCount`, `currentPageFootnotes` and `currentPageVisibleOffset` keep the values the captured
    render set (`:1487-1488`, `:1683`).

- **A-21: the status bar's battery level and charging icon are shown as they were when the page was rendered**
  (`BaseTheme.cpp:642-660`). Any page left on screen behaves the same. The next page turn refreshes it. The clock
  is X3-only (`:661-663`).

- **A-22: a refused restore falls through to today's render.** It logs `LOG_DBG` with the `fit()` reason. The
  `Menu closed: restore` line followed by `Page restore refused: <reason>` makes a mismatch visible on serial.

### Logging

- **A-23: three lines, all `LOG_DBG("ERS", ...)`.** Timings follow `renderContents`' style (`:1856-1860`).
  - `Menu closed: <restore|action|end-of-book|orientation|auto-turn|night>`.
  - `Page restore: copy=%lums display=%lums gray_write=%lums gray_display=%lums total=%lums`. Its field names
    deliberately avoid `bw_render` and `gray_lsb` (AC1). `gray_*` fields read 0 when the page has no grays.
  - `Page restore refused: <empty|no-section|moved|gray-missing|no-fb>; rendering`. `no-fb` is checked in
    `restorePageFromCache` itself, outside `fit()`.
  - The cache's OOM latch logs `LOG_ERR("ERS", "OOM: page cache plane (%u bytes, PSRAM)")` once.

## Error handling

| Failure | Behaviour | Log |
|---|---|---|
| PSRAM allocation fails | Latch for the session. B/W: the cache never fits. Gray pair: AA pages refuse (`gray-missing`), pages shown without grays still restore | `LOG_ERR` once |
| `renderContents` skips its gray pass (store or scratch OOM) | `grayShown` false; the restore shows the page flat, as it was shown | existing `LOG_ERR` lines (`:1832`, `:1781`) |
| Gray shown but planes not captured (one-buffer or strip tiled branch) | `fit()` → `GrayMissing`; normal render | `Page restore refused: gray-missing` |
| The reader moved, the section was released, or the last render was not a page | `fit()` → `Moved` / `NoSection` / `Empty`; normal render | `Page restore refused: ...` |
| No framebuffer (`!renderer.hasFrameBuffer()`) | Normal render | `Page restore refused: no-fb` |
| Anything the menu changed | `decide()` ≠ `Restore`; no request | `Menu closed: <reason>` |

No new failure can leave the panel showing the menu: every refusal re-renders the page as today. The display
calls the restore makes return `void`, as they do in `renderContents`.

## Testing strategy

### Host (TDD: each test written and run failing before the code)

`test/ui_layout/MenuCloseRestoreTest.cpp`, built like `ReaderMenuSheetLayoutTest` (`${REPO_ROOT}/src` only, no
stubs):

| ID | What it asserts |
|---|---|
| T-1 | `decide()` on a cancelled close with nothing changed → `Restore`. |
| T-2 | Each single change, alone, blocks the restore with its own verdict: not cancelled; end of book; orientation; auto turn before only; auto turn after only; night before only; night after only. |
| T-3 | A non-cancelled close reports `Action` whatever else changed (the log reason is stable). |
| T-4 | `fit()`: a default record → `Empty`; B/W at the same position → `Fits`; no section → `NoSection`; another spine → `Moved`; another page → `Moved`. |
| T-5 | `fit()` with grays: shown with both planes → `Fits` and `restoresGray`; shown with one plane → `GrayMissing`; planes without `grayShown` → `Fits` and not `restoresGray`. |
| T-6 | Every `Verdict` and `Fit` has a distinct, non-empty name. |

`PageFrameCache` itself is not host-built: it calls `heap_caps_malloc`, and `test/stubs/` has no
`esp_heap_caps.h`. Adding one would be a new stub pattern for three trivial `memcpy`s. Its logic is the record
`fit()` reads, which T-4 and T-5 cover.

### Build and checks

- `pio run -e x4pro`, once, after the last code edit (CLAUDE.md, testing checklist).
- `./bin/clang-format-fix` over the whole tree before committing.
- Host suite: `cmake` build and `ctest -R MenuCloseRestore` in `test/`.

### On the device (owner; dev build `x4pro`, serial via `scripts/debugging_monitor.py` or `cat`)

| ID | Steps | Expected |
|---|---|---|
| V-1 | AA on. Open the menu with a centre tap, close it by tapping the page, six times. Then once each with ✕, the left-edge swipe and Home. | Each close: `Menu closed: restore`, then `Page restore: ... total=`, and no `Page render` or `Rendered page in`. The restored page has its gray edges, compared with a page turned away from and back to. The `total=` median is the PR's after figure against 1,160 ms. |
| V-2 | AA off (the owner's setting). Same as V-1. | Same lines, `gray_*=0`. The after figure has no baseline; also record one close on the pre-change build. |
| V-3 | From the menu: Mark; Night mode on, then close; Night mode off, then close; Auto turn 1; Go to then cancel, then close the reopened sheet; Text settings with a font change; Screenshot; a Recent chip; Tag. | Each logs `Menu closed: <reason>` other than `restore` (except the final close after Go to cancel, which restores). The page shows the change: bookmark icon and toast, inverted page, auto-turn title, new font, screenshot border, the new place. |
| V-4 | With the long-press set to Mark, mark a page with a Home-key hold, then open and close the menu while the toast still shows. | The toast reappears over the restored page and then expires as usual. |
| V-5 | Ten page turns each with AA on and off. | `Page render ... total=` medians within ~10 ms of the baseline (1,378 / 741 ms). More than that means A-2's copies cost more than estimated. |
| V-6 | Watch `[MENU] heap before/open/closed` and `MEM` across entering the reader, ten closes, and leaving to Home. | PSRAM down by 48,000 B (AA off) or 144,000 B (AA on) after the first page, steady across closes, and back after leaving. Internal heap unchanged by the closes. |
| V-7 | Set the refresh frequency to its smallest value, then open and close the menu repeatedly. | When the HALF is due, the restore's `display=` is a HALF (~1.5 s); otherwise FAST. |
| V-8 | At the end-of-book screen, open the menu with a Home-key hold and close it. | `Menu closed: end-of-book`, and the end-of-book screen is shown again. |
| V-9 | SSD1677. | No unit available. A-17's SSD1677 branch is reasoned from the live strip branch only; the PR flags it as untested. |

## Expected effect (estimate, not measured)

| | Today | After | Basis |
|---|---|---|---|
| Close, AA on (UC8179) | 1,160 ms `total=`, plus an unmeasured SD page load | ~1,030 ms | research note table: `display` 616 + uploads ~50–60 + `gray_display` 357 + copy ~2 |
| Close, AA off | not measured (page turn: 741 ms) | ~635 ms | `display` ≈ 631 + copy ~2 |
| Page turn, AA on / off | 1,378 / 741 ms | +≤ 6 / +≤ 2 ms | A-2 |

## Risks

- **A-2's per-turn copies.** If PSRAM copies are slower than `bw_store=2ms` suggests, every turn pays for it. V-5
  catches it. The follow-up below removes the copies.
- **SSD1677 is untested** (V-9). The branch mirrors live code (`:1779-1819`, `:1756-1758`). A wrong replay would
  show as a ghosted sheet or a flat page on close only, never on a page turn, and the next page turn repaints.
- **A stale battery icon** after a long menu visit (A-21). Accepted.
- **Gain is modest.** The panel waveforms are ~851 ms of the 1,160 ms, and no restore removes them on this panel
  (research note).

## Follow-ups (not built here)

- **Render the gray passes straight into the cache planes** (A-2's rejected option). It drops the per-turn copies
  and `storeBwBuffer`/`restoreBwBuffer` on UC8179.
- **Restore before reopening the sheet after a sub-screen cancel.** `reopenReaderMenu` re-renders first
  (`:306-326`). Footnotes and Go to % keep the section, so their cancels could restore.
- **Restore on toast expiry.** Expiry re-renders the whole page (`:574-576`).
- **Mark without a re-render.** The status bar is B/W only (the gray passes render page content only,
  `:1651-1657`), so a bookmark toggle could restore, redraw the status bar and reuse the saved grays.

## For the orchestrator

Nothing to apply. The new test is registered in `test/ui_layout/CMakeLists.txt`, the suite's own file. No
shared append point (`test/CMakeLists.txt`, `src/main.cpp`, translations) is touched, and no user-facing string
is added.
