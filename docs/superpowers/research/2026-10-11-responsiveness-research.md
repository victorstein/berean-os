# Responsiveness: where the time goes

Research only. No code was changed. The plan built on this lives in
`docs/superpowers/plans/2026-10-11-responsiveness-roadmap.md`.

- **Base:** `berean-os` `1ba1cd3` (after release 1.29.8). `freeink-sdk` pinned at `67f7e012`.
- **Device measurements: none.** No device was attached. Every millisecond figure below is
  quoted from a source comment or derived from a clock rate. Each one says which. The first job
  of whoever picks this up is the baseline in roadmap Phase 0.
- **Citations.** Paths without a prefix are in `berean-os`. `freeink-sdk/...` paths are at
  `67f7e012`. The submodule is not checked out by a plain clone:
  `git submodule update --init freeink-sdk` before following any SDK citation.

## 0. How this was produced

1. Read the main loop (`src/main.cpp:651-827`), the render task
   (`src/activities/ActivityManager.cpp:26-71`), `HalDisplay`, `HalPowerManager` and
   `GfxRenderer::displayBuffer` directly.
2. Ran three read-only sweeps in parallel, with non-overlapping briefs:
   - the e-ink driver: bus clock, BUSY waits, RAM writes per refresh, LUTs, async support;
   - the input → render pipeline: polling, debounce, gesture windows, refresh-mode choice, the
     reader page turn;
   - storage and caching: SD clock, synchronous writes on input, per-screen entry costs, glyph
     caches, PSRAM placement, build flags.
3. Re-verified every load-bearing claim from those sweeps by reading the cited line, and checked
   two upstream facts against upstream source: ESP-IDF v5.5 `sdmmc_types.h` and arduino-esp32
   3.3.7 `Esp.cpp`. Section 6 lists the claims that are still unverified.
4. Looked for prior work so as not to repeat it: the only `perf:` commit is `08ac400` (#238, Go
   to). There are no open issues on the topic.

## 1. The shape of one interaction

From a finger or key to a settled screen, in order:

| # | Stage | What decides its cost | Where |
|---|---|---|---|
| 1 | Input detected | Loop period (10 ms active, **50 ms after 3 s idle**), 5 ms debounce needing two samples, act-on-release | `src/main.cpp:812-826`, `freeink-sdk/libs/hardware/InputManager/src/InputManager.cpp:465` |
| 2 | Activity reacts | Synchronous work in `loop()`/`onEnter()`, often SD | per activity, §4–§5 |
| 3 | Render task woken | `requestUpdate()` is only a flag; it notifies at the end of `ActivityManager::loop()` | `ActivityManager.cpp:167-173` |
| 4 | CPU render | Clear, draw, glyph decompression, SD page load; ~14 passes for an anti-aliased page | §3 |
| 5 | SPI transfer | 48 KB per plane at 20 MHz, several planes per refresh | §2 |
| 6 | **Panel waveform** | FAST ~500 ms, HALF ~1720 ms, FULL ~1800 ms (source comments) | §2 |
| 7 | After the waveform | Post-refresh RAM resync, gray pass (a second waveform), progress save | §2, §3 |

The render task holds `RenderLock` from stage 4 through stage 7 (`ActivityManager.cpp:52-59`), so
input that arrives meanwhile is deferred and coalesced (`ulTaskNotifyTake(pdTRUE, ...)`,
`:49`).

**Stage 6 dominates, and every refresh pays it.** Stages 1–5 and 7 are where the firmware adds
its own time: typically tens of ms each, up to seconds for some screen entries. The panel work
is mostly fixed. The work around it is not, and much of it was written for the C3's ~380 KB.
The S3 has 8 MB of PSRAM, yet the code still discards caches and reloads data the S3 could keep.

## 2. Panel and driver

**F1 — The X4 Pro does not use the SDK's faster refresh mode.**
- FAST uses the stock `0xFC` sequence, which reloads the temperature and LUT and power-cycles the
  analog rails on every refresh: "PART: 3C=C0, 22=FC, 20, ~500 ms"
  (`freeink-sdk/libs/display/FreeInkDisplay/src/driver/Ssd1677Driver.cpp:44-51`).
- `-DFREEINK_X4PRO_FAST_DU_SHORTCUT` switches FAST to the incremental `0x1C` DU path.
  - The SDK's estimate is "~85 ms/refresh win" (`Ssd1677Driver.cpp:110-151`). It is selected at
    `:714-717` and is not set in `platformio.ini`.
  - The SDK's caveat: artifacts "tend to appear only over long sessions and across temperature".
- It applies to **SSD1677-batch units only**. UC8179 and UC8279 batches use other drivers.

**F2 — Every blocking refresh ends with two full-frame RAM writes.**
- After the waveform, single-buffer mode rewrites BW and RED (2 × 48,000 B) "so the next
  differential update starts from a matched BW/RED baseline" (`Ssd1677Driver.cpp:488-499`).
- At 20 MHz that is about 38 ms of SPI per refresh. That figure is the SDK's own, for all plane
  writes, at `freeink-sdk/libs/hardware/BoardConfig/include/BoardConfig.h:760-768`.
- The caller stays blocked and `RenderLock` stays held throughout.
- This is stock-firmware parity, so do not remove it casually (roadmap R16).

**F3 — Display SPI runs at 20 MHz, the SSD1677 datasheet maximum.**
- `BoardConfig.h:1379`. The 40 MHz option exists only for the plain X4 (`:770-774`) and is out of
  spec.
- **Not recommended.** It would save about 19 ms per refresh.

**F4 — On SSD1677 units, the first paint after a wake is always HALF.**
- `src/main.cpp:467-470` says a skipped splash makes "the first paint FAST_REFRESH (~500ms)".
- On SSD1677, the driver's `_needsInitialFull` branch ends with
  `mode = (_cfg.halfSeqOverride != 0) ? RefreshMode::Half : RefreshMode::Full;`, which runs
  unconditionally (`Ssd1677Driver.cpp:428-439`).
  - Its preceding `if (mode == Fast) mode = Full;` is dead.
  - A requested FULL is downgraded to HALF.
- `skipInitialResync()` is overridden by the UC drivers but not by SSD1677
  (`PanelDriver.h:161`, `Uc8179Driver.cpp:391`, `Uc8279X4Driver.cpp:261`). So `HalDisplay`'s
  `seamless` path (`lib/hal/HalDisplay.cpp:21-26`) cannot avoid this.
- The HALF has a purpose: after reset the controller's RED baseline does not match what the panel
  shows. A FAST wake would first need RED seeded with the retained frame (roadmap R15).

**F5 — With night mode on, every switch between the reader and a menu is a HALF.**
- The render task calls `display.setInverted(SETTINGS.screenInverted && appliesNightMode())` on
  every render (`ActivityManager.cpp:58`).
- A change marks `_inversionDirty` (`freeink-sdk/.../FreeInkDisplay.cpp:316-324`), and a dirty
  inversion promotes FAST to HALF (`:572-575`).
- Inversion also disables async refresh (`:557-559`). Night mode defaults off
  (`src/CrossPointSettings.h:259`).

**F6 — UC8179 and UC8279 units get none of the reader's overlap work.**
- `supportsStripGrayscale()` is true only for SSD1677 (`Ssd1677Driver.h:87`; default false at
  `PanelDriver.h:103`; UC8179 says so at `Uc8179Driver.h:86`). The reader's tiled and overlapped
  gray path is gated on it (`src/activities/reader/EpubReaderActivity.cpp:1644-1650`).
- Refresh completion on UC panels is a level poll with `delay(1)`, not an interrupt
  (`freeink-sdk/libs/display/FreeInkDisplay/src/bus/EpdBus.cpp:297-305`).
- **Find out which controller the owner's unit(s) use before prioritising** (boot log,
  `XteinkDetect`).

**F7 — Partial-window refresh would not help.**
- `displayWindow` exists in the facade and in the SSD1677 driver, but the waveform time does not
  shrink: "Scan time is unchanged (the DU waveform length does not depend on how many pixels
  drive)" (`Ssd1677Driver.cpp:475-477`).
- It saves SPI bytes only. Ruled out.

## 3. Reader page turn

**F8 — An anti-aliased page is two waveforms and about 14 render passes.**
- `textAntiAliasing = 1` by default (`src/CrossPointSettings.h:284`).
- The turn renders:
  1. a scan pass (`EpubReaderActivity.cpp:1628-1634`);
  2. the B/W pass (`:1700-1704`);
  3. B/W refresh (waveform 1);
  4. 6 strips × 2 planes of gray passes (`:1728-1736`);
  5. `displayGrayBuffer()` (waveform 2);
  6. cleanup.
- Input waits for all of it.
- The page is legible after waveform 1. Whether AA stays the default is a product call (roadmap
  §Owner decisions).

**F9 — Possible bug: the overlap path is gated on internal RAM but allocates from PSRAM.**
- `overlapRefresh` renders both gray planes into 48,000 B buffers while waveform 1 runs
  (`EpubReaderActivity.cpp:1650`, `:1745-1753`).
- The gate is `ESP.getFreeHeap() >= planeBytes + 60000 && ESP.getMaxAllocHeap() >= planeBytes +
  16384` (`:1739-1744`).
  - Both read **internal** RAM only (arduino-esp32 3.3.7 `cores/esp32/Esp.cpp:163-173`,
    `MALLOC_CAP_INTERNAL`).
  - A 48,000 B `malloc` goes to PSRAM (`CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=4096`, see
    `docs/superpowers/research/2026-09-30-issue-200-research.md:187-191`).
- So the overlap is skipped whenever internal free heap is below ~108 KB, even with megabytes of
  PSRAM free. The gray planes are then rendered after waveform 1 instead of during it.
- **Whether it actually trips is unmeasured.** The log tells: `Page render (tiled async)` with
  `planes buffered: 2` means it ran. `Page render (tiled)` means it did not (`:1772-1776`,
  `:1822-1826`).
- **The same internal-only gate appears elsewhere.** Each of these should be checked for whether
  what it protects actually lives in PSRAM:
  - the idle prewarm (`EpubReaderActivity.cpp:459-460`);
  - SD-card font arena retention (`lib/EpdFont/SdCardFont.cpp:122`).

**F10 — The built-in glyph cache is discarded after every render.**
- `PrewarmScope` clears the cache in its constructor and destructor
  (`lib/GfxRenderer/FontCacheManager.cpp:119-141`). For built-in fonts that unconditionally frees
  every decompressed group (`lib/EpdFont/FontDecompressor.cpp:23-46`).
- Every page turn therefore re-inflates the reading font's groups.
- The idle prewarm of page N+1 (`EpubReaderActivity.cpp:457-480`) is thrown away for built-in
  fonts by its own scope's destructor.
- **SD-card fonts behave differently.** Their per-style arenas are retained across scopes, with
  underuse hysteresis, unless internal free heap is below `MINI_RETAIN_MIN_FREE_HEAP`
  (`lib/EpdFont/SdCardFont.cpp:117-137`). So the "heap-adaptive" comment at
  `src/activities/reader/EpubReaderChapterSelectionActivity.cpp:26-31` is about SD fonts only.
  The default reading font, Noto Serif, is built in (`src/CrossPointSettings.h:301`).
- **Size of a resident alternative**, as the sum of `uncompressedSize` over `...Groups[]` in
  `lib/EpdFont/builtinFonts/`:

  | Font | Regular | All four styles |
  |---|---|---|
  | Noto Serif 14 | 99,750 B | ~430 KB |
  | Noto Serif 18 | — | ~686 KB |

  That is affordable in PSRAM.
- The `prewarm=` field of the page-render log is the cost to beat.

**F11 — Nothing is kept from the previous page or the next one.**
- Each turn reloads the page from the section file (`lib/Epub/Epub/Section.cpp:754-787`) and
  redraws from scratch.
- The idle prewarm loads page N+1, then discards the `Page` and draws no pixels.
- With PSRAM, the next page's B/W frame and both gray planes (3 × 48,000 B) could be drawn ahead
  of time. A turn would then be copy + status bar + waveform (roadmap R14).

**F12 — Smaller per-turn SD work.**
- **Progress save:** every turn ends with an atomic progress save while `RenderLock` is still held
  (`EpubReaderActivity.cpp:1498-1505`). That delays the next turn, not the current one.
- **Status bar:** with the default `CHAPTER_TITLE` status bar, `renderStatusBar()` reads the TOC
  from `book.bin` (`:1919-1923`). It runs in both the scan pass and the B/W pass (`:1633`,
  `:1704`).

## 4. Input

**F13 — Almost every page turn is noticed 50–100 ms late.**
- After `IDLE_POWER_SAVING_MS = 3000` the CPU drops to 80 MHz and the loop sleeps 50 ms per pass
  (`lib/hal/HalPowerManager.h:28-33`, `src/main.cpp:818-821`).
- A button needs two samples to pass the 5 ms debounce
  (`freeink-sdk/libs/hardware/InputManager/include/InputManager.h:383`, `src/InputManager.cpp:465`),
  so a press lands 50–100 ms after the edge. A touch release lands up to 50 ms late.
- Reading a page takes longer than 3 s, so this applies to nearly every turn.
- The GT911 is polled every loop. Its INT pin is used only for address selection at reset; there
  is no `attachInterrupt` anywhere in `InputManager.cpp`.

**F14 — Physical nav keys act on release by design.**
- A hold of 850 ms means Back or Confirm (`lib/Input/Input/NavKeyGestures.cpp:21-30`,
  `NavKeyGestures.h:41`), so a press cannot fire early. Not a target.

**F15 — The power-button Confirm waits 500 ms, only when chosen.**
- It waits for the frontlight double-click window (`src/main.cpp:58,735-743`).
- `shortPwrBtn` defaults to `IGNORE` (`src/CrossPointSettings.h:286`). Not a target.

## 5. Screen entry and storage

**F16 — The SD card runs at 20 MHz, not the intended 40 MHz.**
- `host.max_freq_khz = SDMMC_FREQ_DEFAULT;  // 40 MHz`
  (`freeink-sdk/libs/hardware/SDCardManager/src/SdmmcBlockDevice.cpp:24-25`).
- ESP-IDF v5.5 defines `SDMMC_FREQ_DEFAULT 20000` and `SDMMC_FREQ_HIGHSPEED 40000`
  (`components/esp_driver_sdmmc/include/driver/sdmmc_types.h:215-216`).
- The comment above it (`:20-23`) says the stock firmware clocks data at 40 MHz.
- The bus is 1-bit (`platformio.ini:175`), so this caps raw throughput at about 2.5 MB/s instead
  of 5 MB/s, for every SD read in the firmware.

**F17 — Data is reloaded on every screen entry.**

| Screen | What it re-does on each entry | Where |
|---|---|---|
| Home | Reloads recents already loaded at boot. `BibleFinder` re-parses the publication registry. Meeting tiles re-parse the registry and load an `Epub`. | `src/activities/launcher/LauncherActivity.cpp:76-187`, `:105` |
| Home, every redraw (each selection move) | Re-reads the whole cover BMP from SD, row by row, and plots it with `drawPixel` | `LauncherActivity.cpp:248`, `src/components/CoverBand.cpp:19-57` |
| Catalog (Buscar) | Reads and inflates the ~217 KB index before the first paint; frees it on exit | `src/activities/catalog/CatalogSearchActivity.cpp:63-83` |
| Bible search | Reopens the ~444 KB term index ("seconds of SD on the first search after boot", deferred until after the first frame); closes it on exit | `src/activities/reader/BibleSearchActivity.cpp:112-125` |
| Publications | One full registry JSON read and parse **per listed book** | `src/activities/catalog/PublicationsActivity.cpp:79`, `src/study/PubKeyRegistry.cpp:78-93` |
| Home → Bible | `Epub::load`, progress, bookmarks, `STUDY.openPublication` every time. The Go-to `BibleNavCache` lives and dies with the reader. Verse anchors are re-scanned from the chapter HTML on every visit. | `src/activities/reader/ReaderActivity.cpp:55-66`, `BibleNavigationActivity.cpp:250-266` |

- The registry reads cost more than they look. `PersistableStore` reads through
  `Storage.readFile` (`lib/Serialization/PersistableStore.cpp:128`). That is
  `SDCardManager::readFile`, which reads one byte per call into an Arduino `String`
  (`freeink-sdk/libs/hardware/SDCardManager/src/SDCardManager.cpp:202-206`).

**F18 — Avoidable writes before a book's first frame.**
- Opening a book runs `APP_STATE.saveToFileAtomic()` and then `RECENT_BOOKS.addBook()` before the
  first `requestUpdate()` (`ReaderActivity.cpp:62-65`).
- `addBook` calls `exists()` on every recent entry and always saves, even when the book is
  already first (`src/RecentBooksStore.cpp:27-50`).
- The app-state save is the crash-loop guard and must stay. The recents save need not run when
  nothing changed, and need not run before the first paint.

## 6. Unverified, and open questions

- **The baseline itself.** Which of F9, F10, F13 and F17 costs most on the device is unknown
  until Phase 0 is run.
- **Which panel controller the owner's units have** (F1, F6).
- **Compiler optimisation level.** The Arduino default is believed to be `-Os`; it is not
  confirmed. `pio run -e x4pro -v` shows the flags.
- **Flash mode.** `board_build.flash_mode = dio` (`platformio.ini:82`), while the framework libs
  come from the `esp32s3/qio_opi` variant. Which mode the bootloader and app actually run in is
  unconfirmed. OTA never replaces the bootloader, so any change needs care (roadmap R17).
- **Agent-reported, not re-read by the orchestrator:**
  - per-byte SPI transactions for commands and LUT bytes (`EpdBus.cpp:130-146`);
  - UC8279X4 streaming one transaction per row (`Uc8279X4Driver.cpp:149-164`);
  - the UC8179 transition waveform after AA pages (`Uc8179Driver.cpp:174-176`, `:410-459`).

## 7. Already done; do not redo

- **#238 (`08ac400`):** Go to builds the book and chapter grid from TOC data and caches them for
  the reader's life (`BibleNavCache.h`).
  - Its Task 9, a masthead band ink cache, was gated on a device measurement and **not built**
    (`docs/superpowers/plans/2026-09-30-issue-238-plan.md:1465-1480`). Its design is reusable for
    the Home cover (roadmap R9).
  - The `"Band drawn in N ms"` log it planned does not exist in `src/`.
- **The reader already has:**
  - tiled grayscale;
  - async B/W refresh overlapped with gray rendering (F9);
  - an idle prewarm (F10);
  - a page-turn guard that coalesces turns arriving during a render (`EpubReaderActivity.cpp:667`);
  - a home verse cache keyed by date and file size (`src/activities/launcher/HomeVerse.cpp:62-66`).
- **Bible search** already defers its expensive open until after its first frame
  (`BibleSearchActivity.cpp:112-114`).

## 8. Measuring

Build `x4pro` (serial log on, `LOG_LEVEL=2`), flash it, and run
`python3 scripts/debugging_monitor.py`. `pio device monitor` does not work on this board
(`CLAUDE.md`). The useful existing lines:

| Log line | Measures | Where |
|---|---|---|
| `Page render (tiled async): prewarm= bw_render= display= gray_render= wait= gray_write= gray_display= cleanup= total=` | the whole page turn, by phase | `EpubReaderActivity.cpp:1772-1776` |
| `Page render (tiled): ...`, and the non-tiled and no-AA variants | the same when overlap or tiling is off | `:1822-1826`, `:1856-1864` |
| `Idle prewarm: page N in N ms` | the wasted prewarm (F10) | `:475` |
| `Time = N ms from clearScreen to displayBuffer` | CPU render time for any screen | `lib/GfxRenderer/GfxRenderer.cpp:1724-1727` |
| `Book loaded in N ms` | Home → reader load | `src/activities/reader/ReaderActivity.cpp:60` |
| `Go to: ... +N ms` | the #238 path | `BibleNavigationActivity.cpp:91-109` |
| `New max loop duration: N ms` | the worst synchronous stall in `loop()` | `src/main.cpp:803-809` |
| `MEM Free: ...` (every 10 s) | internal heap; tells whether F9's gate can trip | `src/main.cpp:662-668` |

Missing, and worth adding in Phase 0:
- input edge → `displayBuffer` start;
- Home `onEnter` duration;
- catalog `store.load()` duration;
- cover band draw time.
