# Responsiveness roadmap

A map for making bereanOS feel faster, written so that another agent can pick it up cold.

- **Evidence:** `docs/superpowers/research/2026-10-11-responsiveness-research.md`. Findings are
  cited below as **F1–F18**; read that file first.
- **Base:** `1ba1cd3`. `freeink-sdk` is pinned at `67f7e012` (branch `berean` of
  `victorstein/freeink-sdk`).
- **State:** research done. Nothing implemented, nothing measured on a device.

## What this roadmap is, and is not

This is a sequence of work items (**R0–R19**), each sized as one GitHub issue. It is **not** an
implementation plan. Each item still goes through the loop in
`docs/contributing/development-workflow.md`:

- **Trivial items:** issue → plan → scoped agent.
- **Anything marked _Spec_:** issue → research → spec → two adversarial reviews → plan → scoped
  agent.

Every item names the evidence it rests on, the files it touches, the log line that proves it
worked, and which scoped agent (`.claude/agents/*.md`) owns it.

### The one rule that orders everything

**Measure first.** The panel waveform dominates every interaction (F1, §1 of the research). The
items here shave the firmware's own time around it, and the right order depends on numbers nobody
has yet. Phase 0 produces them. Re-rank Phases 2–4 by what Phase 0 shows; Phase 1 is cheap enough
to do regardless.

## Summary

| ID | Item | Evidence | Expected effect | Effort | Risk | Owner agent |
|---|---|---|---|---|---|---|
| R0 | Instrument + device baseline | §8 | Makes everything else rankable | S | none | ui-dev / hal-dev |
| R1 | Fast-DU shortcut flag (SSD1677) | F1 | ~85 ms off every refresh (SDK estimate) | XS | ghosting, panel-dependent | hal-dev |
| R2 | SD card at 40 MHz | F16 | ~2× raw SD throughput everywhere | XS (SDK) | card compatibility | hal-dev |
| R3 | Shorter idle input wait | F13 | ~50 ms off nearly every page turn | XS | battery | hal-dev |
| R4 | Reader overlap gate checks PSRAM | F9 | Gray render overlaps waveform 1 again | S | low | ui-dev |
| R5 | No redundant writes before a book's first frame | F18 | One fewer atomic SD save per book open | S | low | data-dev |
| R6 | Registry reads: cache + streamed JSON reads | F17 | Faster Home, Publications, meetings | M | stale cache | data-dev |
| R7 | Home: drop the redundant recents reload | F17 | One fewer JSON read per Home entry | XS | missed external change | ui-dev |
| R8 | Resident glyph groups in PSRAM | F10 | Lower `prewarm=` per turn; idle prewarm becomes useful | M, _Spec_ | memory budget | ui-dev |
| R9 | Home cover band cache | F17 | No SD stream on Home selection moves | S | low | ui-dev |
| R10 | Keep the catalog index resident | F17 | Instant re-entry to Buscar | S | memory | net-dev |
| R11 | Keep the Bible search index open | F17 | Instant re-entry to search | S | memory, stale index | data-dev |
| R12 | Home ↔ Bible session retention | F17 | Lower `Book loaded in` on re-entry | L, _Spec_ | invalidation | epub-dev + ui-dev |
| R13 | Verse anchors cache | F17 | Instant verse grid on revisits | S | low | ui-dev |
| R14 | Pre-render the next page in PSRAM | F11, F8 | Page turn starts its waveform almost immediately | L, _Spec_ | correctness of invalidation | ui-dev + epub-dev |
| R15 | SSD1677 seamless wake (FAST first paint) | F4 | ~1.2 s off wake on SSD1677 units | M (SDK) | ghosting on wake | hal-dev |
| R16 | Halve the post-refresh RAM resync | F2 | ~19 ms off every blocking refresh | S (SDK) | baseline corruption | hal-dev |
| R17 | Flash mode audit (DIO vs QIO) | §6 | Faster code/font fetch on cache miss | S investigate | OTA/bootloader mismatch | hal-dev |
| R18 | `-O2` on render hot paths | §6 | Lower `bw_render=`/`gray_render=` | S | flash size | ui-dev |
| R19 | Interrupt-driven input wake | F13 | R3's win with no battery cost | M (SDK) | ISR rules | hal-dev |

Effort: XS < 1 h, S ≈ half a day, M ≈ 1–2 days, L ≈ multi-day with a spec.

## Before starting any item

1. `git submodule update --init freeink-sdk`. SDK items (R2, R15, R16, R19) are PRs against
   `victorstein/freeink-sdk` branch `berean`, followed by a submodule bump PR here.
2. Build: `pio run -e x4pro`. PlatformIO is not installed in the Claude Code cloud container used
   for the research. The owner's local `pio` path is in recent plans, e.g.
   `docs/superpowers/plans/2026-09-30-issue-238-plan.md` ("Tools").
3. Host tests: `cmake -S test -B build/test && cmake --build build/test --target <Target>`
   (`test/README`).
4. **Device runs are the owner's.** Agents must say exactly what to flash and which log lines to
   read, and must not claim a device result they have not seen.
5. **Rules that bite here** (all from `CLAUDE.md`):
   - Allocate with `makeUniqueNoThrow`.
   - Use **explicit PSRAM** for every cache this roadmap adds: `heap_caps_malloc(...,
     MALLOC_CAP_SPIRAM)` with the `PsramFree` deleter idiom (`src/network/CatalogIndexStore.h:86-89`,
     also used by #238 and #204). Do not rely on the 4 KB `malloc` threshold.
   - Justify every new allocation in the PR body.
   - No `std::function`.
   - Timings go in `LOG_DBG`.
   - Stores follow the storage discipline section.
   - Run `./bin/clang-format-fix` over the whole tree.
   - Use a `perf:` conventional PR title targeting `main`.
6. **Before/after protocol** for every item:
   - Same card, same book, same settings, battery above 50 %.
   - At least 10 samples per scenario; report the median and the worst.
   - Paste the raw log lines into the PR.

---

## Phase 0 — Instrument and baseline (do first)

### R0. Instrument the gaps, then take a baseline

**Why.** The research has no device numbers (§0). Four timings that matter have no log line
(§8).

**Instrumentation PR** (small; `ui-dev` for `src/`, `hal-dev` for `lib/hal`):

1. **Input → refresh start.**
   - Stamp the time of the last input edge where `main.cpp` already detects it
     (`src/main.cpp:691-695`).
   - Log the elapsed time at the start of `GfxRenderer::displayBuffer`
     (`lib/GfxRenderer/GfxRenderer.cpp:1724-1727`), beside the existing
     `Time = N ms from clearScreen` line.
   - Keep it to one `uint32_t` and a free function. No new class or allocation.
2. **Home entry.** `LOG_DBG` the duration of `LauncherActivity::onEnter`
   (`src/activities/launcher/LauncherActivity.cpp:76`).
3. **Catalog load.** The duration of `store.load()` (`src/activities/catalog/CatalogSearchActivity.cpp:68`).
4. **Cover band.** `"Band drawn in N ms"` around `CoverBand::draw`
   (`src/components/CoverBand.cpp`). #238 planned it, but it is not in `src/`.
5. **Board facts at boot.** The panel controller (SSD1677 / UC8179 / UC8279) and the SD card's
   negotiated clock. Check whether `XteinkDetect` or the SD mount already logs these before
   adding anything.

**Baseline run** (owner, on device, `x4pro` build).

Record in a new standing note, `docs/superpowers/notes/responsiveness-baseline.md`. Scenarios:

| Scenario | Lines to capture |
|---|---|
| 20 page turns in a Bible chapter, AA on | `Page render (...)`, input → refresh start |
| Same, AA off | same |
| 20 page turns with night mode on | same |
| Home entry from the reader (×10) | Home `onEnter`, first frame |
| Home selection moves (×10) | `Band drawn in`, `Time = N ms from clearScreen` |
| Home → Bible (×10) | `Book loaded in`, input → refresh start |
| Go to from the reader menu (×10, 1st and 2nd) | `Go to: ... +N ms` |
| Catalog entry (×5) | catalog load |
| Bible search open (×5) | search open timings |
| Reader menu open/close (×10) | input → refresh start |
| Wake from sleep (×5) | time to first frame |

Also note:
- the panel controller and card model;
- the `MEM Free` internal-heap figure while reading, which decides R4;
- whether `Page render` lines say `tiled async` or `tiled`.

**Done when** the note exists and Phases 2–4 below are re-ranked from it.

---

## Phase 1 — Configuration-level wins

Each is independent and touches a different file. They can run in parallel.

### R1. Fast-DU refresh shortcut (SSD1677 units)

- **Evidence:** F1.
- **Change:** add `-DFREEINK_X4PRO_FAST_DU_SHORTCUT` to `[env:x4pro]` in `platformio.ini`
  (`:164-177`) **only**, not the release env, for a soak period.
- **Soak test:**
  - Several long reading sessions (100+ pages), including cold (<10 °C) and warm conditions.
  - Watch for ghosting or blotching between the HALF cleans (every 15 pages, F8).
  - Compare side by side with a unit on the release build.
- **Verify:** the `display=` field of `Page render` drops by about 80 ms; the HALF cadence still
  cleans the page.
- **Then:** add it to `[env:x4pro-gh_release]` in a separate `perf:` PR, so release-please records
  it.
- **Does nothing on UC8179/UC8279 units.** The R0 baseline says which units these are.
- **Rollback:** remove the flag.

### R2. SD card at 40 MHz (SDK)

- **Evidence:** F16.
- **Change:** in `freeink-sdk/libs/hardware/SDCardManager/src/SdmmcBlockDevice.cpp:25`, set
  `SDMMC_FREQ_DEFAULT` → `SDMMC_FREQ_HIGHSPEED` and correct the comment. Then bump the submodule
  here.
- **Check:**
  - What ESP-IDF does with a card that lacks high-speed mode. The negotiated speed shows in
    `sdmmc_card_print_info` output, or log `card->max_freq_khz` after init.
  - Test with at least three cards, including an old Class 4 or 10 card.
  - Watch the mount retry loop in the same file for new failures.
- **Verify:** `Book loaded in`, catalog load, search open and section build times drop. No mount
  failures or read errors across a day of use.
- **Risk:** signal integrity on 1-bit SDMMC. The stock firmware reportedly runs 40 MHz
  (comment at `:20-23`).

### R3. Shorter idle input wait

- **Evidence:** F13.
- **Change:** at `src/main.cpp:821`, reduce `delay(50)` to about 15–20 ms, and keep the 80 MHz
  drop.
- **Alternative:** keep 50 ms but raise `IDLE_POWER_SAVING_MS` (`lib/hal/HalPowerManager.h:33`)
  above a typical page-read time. This costs more power.
- **Power check:** battery-percentage drop over one hour of reading (or idle current on a
  meter), before and after. The CPU sits in the idle task either way, so the difference should
  be small, but measure it.
- **Verify:** the R0 input → refresh start median drops about 30–50 ms on page turns taken after
  3 s of reading.
- **Superseded by R19** if that lands.

---

## Phase 2 — Small code fixes

### R4. The reader overlap gate should check PSRAM

- **Evidence:** F9.
- **Gate:** do it if the R0 baseline shows any `Page render (tiled)` lines, or an internal free
  heap below ~110 KB while reading. Otherwise it is still a correctness cleanup; low priority.
- **Change:** in `src/activities/reader/EpubReaderActivity.cpp:1739-1746`:
  - gate on `heap_caps_get_free_size(MALLOC_CAP_SPIRAM)` and
    `heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM)`;
  - allocate the two 48,000 B plane buffers explicitly in PSRAM (`PsramFree` idiom).
  Keep the strip fallback for a failed allocation.
- **Memory:** the same 96 KB as today, in the place it already lands. It just stops being refused
  on an internal-heap reading.
- **Same pass:** audit the other internal-only gates F9 lists: the idle prewarm
  (`EpubReaderActivity.cpp:459-460`) and SD-font retention (`lib/EpdFont/SdCardFont.cpp:122`).
  Fix only those whose protected allocation is in PSRAM. Each fix needs its own log evidence.
- **Verify:**
  - every AA page logs `tiled async` with `planes buffered: 2`;
  - `total=` drops by roughly `gray_render=`, because it now overlaps `display=`;
  - `gray_render=` does not regress.
- **Collides with R14.** Land R4 first.

### R5. No redundant writes before a book's first frame

- **Evidence:** F18.
- **Change:**
  - `RecentBooksStore::addBook` (`src/RecentBooksStore.cpp:27-50`) skips the save, and the
    prune's `exists()` sweep, when the book is already first with the same title and author.
  - Leave `APP_STATE.saveToFileAtomic()` (`src/activities/reader/ReaderActivity.cpp:63`) alone.
    It is the crash-loop guard.
- **Optional, and only if measured:** move the recents save after the first paint. Mind
  `storageMutex` contention with the render task's page load.
- **Tests:** `test/recent_books_doc/`.
- **Verify:** input → refresh start for "open the same Bible again" drops; the recents order is
  unchanged.

### R6. Registry reads: cache them, and stop reading byte by byte

- **Evidence:** F17.
- **Change 1, the registry:** `PubKeyRegistry::lookup` and `findBySymbol`
  (`src/study/PubKeyRegistry.cpp:53-93`) re-read and re-parse the whole file on every call;
  Publications calls `lookup` once per book (`src/activities/catalog/PublicationsActivity.cpp:79`).
  Either:
  - hold one parsed copy in PSRAM (ArduinoJson with `PsramJsonAllocator`, which already exists),
    invalidated by the registry's own write path; or
  - add a batch lookup that takes the list of paths.
- **Change 2, the byte-by-byte read:** `PersistableStore` reads through `Storage.readFile`
  (`lib/Serialization/PersistableStore.cpp:128`), which is a byte-per-call `String` build
  (`freeink-sdk/.../SDCardManager.cpp:202-206`).
  - Switch the load path to a chunked read. `Storage.readFileToBuffer` and `readFileToStream`
    exist (`lib/hal/HalStorage.cpp:48-53`).
  - This touches every store, so it needs `data-dev` and `test/persistable_store/`,
    `test/storage_io/` and `test/temp_adoption/` all green.
- **Verify:** Home `onEnter` and the Publications first frame drop. The store tests pass.
- **Do these as two issues.** The second has a wider blast radius.

### R7. Home: drop the redundant recents reload

- **Evidence:** F17.
- **Change:** `LauncherActivity::onEnter` calls `RECENT_BOOKS.loadFromFile()`
  (`src/activities/launcher/LauncherActivity.cpp:105`), although `main.cpp` loaded it at boot.
- **First, find every writer** of the recents file outside `RecentBooksStore` (web server
  uploads and deletes, file browser, clear cache). Only drop the reload if all of them go
  through the in-memory store, or replace it with a "dirty" flag those writers set.
- **Verify:** Home `onEnter` drops. Deleting a book over the web server still removes it from
  Home.

---

## Phase 3 — Use the PSRAM

### R8. Resident glyph groups for built-in fonts (_Spec_)

- **Evidence:** F10.
- **Idea:** stop freeing decompressed groups at every `PrewarmScope` boundary
  (`lib/GfxRenderer/FontCacheManager.cpp:119-141`, `lib/EpdFont/FontDecompressor.cpp:23-46`).
  Keep an LRU of decompressed groups in explicit PSRAM under a byte budget.
  - Sizes: all four Noto Serif 14 styles are ~430 KB uncompressed; the largest, 18 pt, is
    ~686 KB.
  - Suggested budget: ~1 MB.
  - **Prior art in-tree:** SD-card fonts already retain their arenas across scopes, with underuse
    hysteresis (`lib/EpdFont/SdCardFont.cpp:117-137`). Mirror that policy rather than inventing
    one.
- **The spec must settle:**
  - **Eviction** on reader font change, sleep, web server start (`releaseSdFontCaches` callers)
    and low PSRAM.
  - **The existing explicit `clearCache()` calls** (`BibleNavigationActivity.cpp:82`,
    `BibleSearchActivity.cpp:109-111`, `EpubReaderChapterSelectionActivity`). They exist to free
    *internal* heap for C3-era budgets. Decide per call site whether PSRAM-resident groups still
    need dropping.
  - **The page-slot path** (`MAX_PAGE_SLOTS`, `test/font_page_slots/`) versus the resident
    groups. Which lookup wins, and how page slots are rebuilt from resident groups without
    re-inflating.
  - Whether the idle prewarm (`EpubReaderActivity.cpp:457-480`) then needs changing. With
    resident groups it starts paying off for free.
- **Verify:** `prewarm=` and `bw_render=` drop on the second and later turns. `Idle prewarm` stays
  cheap. Internal free heap is unchanged.

### R9. Home cover band cache

- **Evidence:** F17.
- **Change:** reuse #238's gated Task 9 design: a band-exact 1-bit ink bitset recorded during the
  blit, held in explicit PSRAM (`docs/superpowers/plans/2026-09-30-issue-238-plan.md:1465-1480`).
  - Its owner must outlive `LauncherActivity`, which is destroyed on every navigation. Use a
    small session-scoped holder keyed by cover path + band rect + file size.
  - Masthead bands on Bible navigation and Publications can share it.
- **Tests:** `test/cover_band/`.
- **Verify:** `Band drawn in N ms` (R0) on the second and later Home redraws falls to the replay
  cost.

### R10. Keep the catalog index resident

- **Evidence:** F17.
- **Change:** `CatalogSearchActivity::onExit` calls `CatalogIndexStore::release()`
  (`src/activities/catalog/CatalogSearchActivity.cpp:80-83`). Keep the ~217 KB inflated index in
  PSRAM after the first load.
  - Release it when the index is re-downloaded, on sleep, or under PSRAM pressure.
  - Update the comment at `:81`, which states the opposite policy.
- **Tests:** `test/catalog_index/`.
- **Verify:** the R0 catalog load on the second entry is ~0.

### R11. Keep the Bible search index open

- **Evidence:** F17.
- **Change:** `BibleSearchActivity::onExit` closes `BibleSearchStore`
  (`src/activities/reader/BibleSearchActivity.cpp:117-125`). Keep the ~444 KB term index open in
  PSRAM, keyed by the Bible's identity.
  - Close it on a Bible change, an index rebuild, sleep, or PSRAM pressure.
  - The ~2.9 MB index *build* still needs the room. Close the index before a build starts.
- **Tests:** `test/bible_search_index/`.
- **Verify:** the second search open shows no "Opening" delay.

### R12. Home ↔ Bible session retention (_Spec_)

- **Evidence:** F17.
- **Problem:** going Home destroys the reader. Re-entering re-runs `Epub::load`, progress,
  bookmarks and `STUDY.openPublication`, and loses `BibleNavCache`
  (`src/activities/reader/ReaderActivity.cpp:55-66`; #238's spec A1 records this as a known
  limit).
- **Idea:** a session-scoped holder, keyed by book path + `BOOK_CACHE_VERSION` + file identity,
  that keeps in PSRAM:
  - the loaded `Epub` metadata;
  - `BibleNavCache`;
  - the study publication state.
  It survives `replaceActivity` and is dropped on another book, a cache clear, sleep, a web
  upload of that book, and PSRAM pressure.
- **The spec must cover:**
  - ownership across `ActivityManager` replace;
  - the `storageMutex` writers that could change the book underneath (the web server);
  - what the crash-loop guard (`APP_STATE.readerActivityLoadCount`) means for a warm open.
- **Verify:** `Book loaded in` on the second and later Home → Bible drops sharply; the 2nd Go to
  after a Home round trip logs `from cache`.

### R13. Verse anchors cache

- **Evidence:** F17.
- **Change:** extend `BibleNavCache` to hold verse anchors for the last few chapters, so
  `loadVerses` (`src/activities/reader/BibleNavigationActivity.cpp:250-266`) stops re-scanning the
  chapter HTML on revisits.
- **Depends on:** most valuable after R12 makes the cache outlive the reader.
- **Tests:** `test/number_grid/`, `test/verse_anchors/`.

---

## Phase 4 — The page-turn pipeline

### R14. Pre-render the next page in PSRAM (_Spec_)

- **Evidence:** F11, F8; the biggest software lever for page turns.
- **Idea:** after a page settles, the idle hook (`EpubReaderActivity.cpp:457-480`, which already
  loads page N+1 under `RenderLock`) renders page N+1 into three PSRAM frames: B/W, LSB plane and
  MSB plane, 3 × 48,000 B = 144 KB. A forward turn then:
  1. copies the B/W frame into the framebuffer;
  2. redraws only the status bar;
  3. starts waveform 1;
  4. writes the ready planes and runs the gray waveform.

  `bw_render`, `prewarm` and `gray_render` leave the critical path.
- **The spec must cover:**
  - **Invalidation:** settings change, highlights/tags change, bookmark toggle, section rebuild,
    orientation, night mode, footnote state, the `PostedMessage`/toast overlay, image pages.
  - **The status bar:** clock, battery and page number must be fresh at turn time.
  - **The highlight overlay:** `invertRect` replay (`:1676-1704`).
  - **Backward turns:** keep page N too (+144 KB) or not.
  - **Interaction with R4's buffers** (share them) and R8 (cheaper pre-render).
  - **Night mode and UC panels:** non-tiled path (F6).
  - **Holding `RenderLock` while pre-rendering:** a turn arriving mid-pre-render must abandon it.
    Today's prewarm already blocks input while it runs.
- **Memory:** 144–288 KB PSRAM, explicit allocation, freed in `onExit` and before section builds
  (which need ~2.9 MB for search, and the framebuffer loan).
- **Verify:**
  - R0 input → refresh start on forward turns falls to a few ms plus the SD-free copy;
  - `total=` falls by `bw_render + gray_render + prewarm`;
  - pixel-identical output: compare `CMD:SCREENSHOT` dumps (`src/main.cpp:671-683`) against the
    current path.
- **Collides with R4** (same function). Land R4 first.

---

## Phase 5 — SDK investigations (hardware experiments)

### R15. SSD1677 seamless wake

- **Evidence:** F4.
- **Change (SDK):**
  - implement `skipInitialResync()` for `Ssd1677Driver`;
  - on the first paint after it, seed BW and RED with the retained frame before a FAST, so the
    differential baseline matches the panel;
  - clean up the dead assignment at `Ssd1677Driver.cpp:437`.
- **Verify:** time from wake to first frame drops by about HALF − FAST (~1.2 s by the source
  comments); no ghost of the sleep screen.
- **Ask the owner first** whether wake time matters to them.

### R16. Halve the post-refresh RAM resync

- **Evidence:** F2.
- **Experiment:** after a FAST, does the SSD1677 leave BW RAM equal to what was written? If so,
  the post-refresh resync (`Ssd1677Driver.cpp:488-499`) only needs RED, saving ~19 ms per
  blocking refresh.
- **Check:** this is stock-firmware parity ("Stock X4 syncs both controller RAM planes"). Prove
  it on the panel with a long differential sequence before changing it. Revert on any drift.

### R17. Flash mode audit

- **Evidence:** §6 of the research.
- **Investigate:**
  - `board_build.flash_mode = dio` (`platformio.ini:82`) against the `qio_opi` framework
    variant;
  - what the bootloader and app actually run, from the boot log and the `esptool image_info` of
    the built binary.
- **If it is DIO end to end:** QIO would speed up instruction and font fetches on cache misses.
  But OTA never replaces the bootloader, so cable-flashed and OTA-updated units would diverge.
  Write up the findings; change nothing without the owner.

### R18. `-O2` on render hot paths

- **Evidence:** §6.
- **First:** confirm the current flags with `pio run -e x4pro -v`.
- **Then:** try `-O2` on `lib/GfxRenderer/GfxRenderer.cpp`, `lib/EpdFont/FontDecompressor.cpp`
  and the inflater (`lib/uzlib`, `lib/InflateReader`), either per file in `platformio.ini` or by
  `#pragma GCC optimize`.
- **Verify:** `bw_render=` and `gray_render=` drop. The firmware still fits `app0`
  (6,553,600 B, `partitions.csv:4-5`); record the size delta.

### R19. Interrupt-driven input wake

- **Evidence:** F13.
- **Change (SDK InputManager + `src/main.cpp`):**
  - Replace the fixed end-of-loop delay with a wait that ends early on input:
    `ulTaskNotifyTake` with a timeout, notified from a GPIO ISR on the GT911 INT line and the nav
    buttons.
  - Follow the ISR rules in `CLAUDE.md`: `IRAM_ATTR`, `xTaskNotifyFromISR` +
    `portYIELD_FROM_ISR`, no I2C in the ISR.
  - GT911 INT is currently used only at reset for address selection (F13). Check the INT mode
    register.
- **Verify:** R3's latency win at the 50 ms idle delay, or longer, with no battery cost.

---

## Owner decisions (not agent tasks)

These are product calls the research surfaced. An agent should ask, not decide.

1. **Text anti-aliasing default** (F8). It adds a second waveform and ~12 render passes to every
   turn. The R0 numbers with AA on and off quantify the trade.
2. **Night mode menus** (F5). Every reader↔menu switch is a HALF. Options:
   - accept it;
   - draw menus inverted while night mode is on, so the polarity never flips;
   - flip polarity only on full-screen menus.
3. **Fast-DU ghosting tolerance** (R1). Is a rare ghost between HALF cleans worth ~85 ms on every
   refresh?
4. **Wake time** (R15). Worth an SDK change?
5. **`refreshFrequency`** default `REFRESH_15` (`src/CrossPointSettings.h:311`). A HALF costs
   ~1.7 s every 15 pages.

## Sequencing and collisions

```
R0 ──► re-rank ──► Phase 2–4
R1, R2, R3            (parallel; platformio.ini / SDK / main.cpp)
R4 ──► R14            (both EpubReaderActivity::renderContents)
R5                    (RecentBooksStore; collides with R12 on ReaderActivity.cpp — serialize)
R6a ─► R6b            (registry cache, then store-wide streamed reads)
R7                    (LauncherActivity; serialize with R9 if both touch onEnter/render)
R8 ──► R14            (R14 gets cheaper with R8; not a hard dependency)
R9, R10, R11          (parallel; different surfaces)
R12 ──► R13           (R13 needs the cache to outlive the reader)
R15, R16, R19         (SDK; serialize R15/R16 — same driver file)
R17, R18              (investigations; any time)
```

Two agents never edit the same file at once
(`docs/contributing/development-workflow.md`, "Parallelism and collisions").

## Handoff checklist for the next agent

- [ ] Read the research file and this roadmap. Re-read any cited line before relying on it; the
      code may have moved since `1ba1cd3`.
- [ ] `git submodule update --init freeink-sdk`.
- [ ] Open issue(s) for R0 and the Phase 1 items. The owner runs the R0 baseline and records it
      in `docs/superpowers/notes/responsiveness-baseline.md`.
- [ ] Re-rank Phases 2–4 by the baseline, and update the Summary table here with the measured
      costs.
- [ ] For each item: issue → (spec + 2 reviews if _Spec_) → plan → scoped agent → PR with a
      before/after table.
- [ ] When an item lands, mark it here (`Done: #PR, measured X → Y`) so this file stays the map.
