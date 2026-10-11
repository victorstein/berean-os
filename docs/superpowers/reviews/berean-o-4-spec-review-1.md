Tier: standard

# berean-o-4 spec review 1

Reviewed: `docs/superpowers/specs/2026-10-11-berean-o-4-design.md` against the brief
(`hpipe brief --task t4`) and `docs/superpowers/research/2026-10-11-berean-o-4-research.md`. Base
`47aea306` (on `478cf1ed`). Every `file:line` below was re-read in this worktree.

## What was checked and holds

I tried to break each part of the design against the code. These parts held.

- **The close chain** (spec, Problem): `closeCancelled` `EpubReaderMenuActivity.cpp:200-206`; pop, then
  `exitActivity` `ActivityManager.cpp:106`, then the snapshot freed at `EpubReaderMenuActivity.cpp:180`, then the
  handler `ActivityManager.cpp:127` with the lock released at `:126`, then `requestUpdate()` `:131-133`;
  `ReaderActivity.cpp:189` → `renderBook`. All accurate.
- **The UC8179 replay is the same call sequence, so it gives the same result** (A-17). `copyGrayscaleLsb` and
  `copyGrayscaleMsb` take `const` masks and mutate only the driver's `_grayBase`
  (`Uc8179Driver.cpp:473-515`). So copying the framebuffer *after* `copyGrayscale*Buffers()` captures exactly
  the bytes the driver was given. `displayGray` ignores `fb` (`:576`). `cleanupGrayscaleBuffers` returns
  early once `_bwPlanesSynced && _oldPlaneValid` (`:590`). `displayStart` re-snapshots `_grayBase` from the
  displayed frame (`:273-276`), and that frame is the captured B/W page in both paths. The facade's
  `copyGrayscaleBuffers(lsb, msb)` is the LSB call followed by the MSB call (`FreeInkDisplay.cpp:796-801`). The
  menu's own first FAST consumes `_redriveAfterGray` (`Uc8179Driver.cpp:174-176`, `:204`). That makes the close's
  B/W step the ordinary DU, today and after the change.
- **The SSD1677 replay.** A blocking FAST in single-buffer mode re-syncs BW and RED from `fb`
  (`Ssd1677Driver.cpp:495-499`). The full-height strip writes go to BW and RED (`:592-600`), `displayGray`
  ignores `fb` (`:602-604`), and cleanup writes RED from the B/W page and clears `_inGrayscaleMode`
  (`:638-647`). Each half already runs live: the blocking strip branch whenever `fadingFix` is on
  (`GfxRenderer.cpp:1730-1742`), and full-height strips on the buffered branch (`EpubReaderActivity.cpp:1756-1758`).
- **The flag and the lock.** The reader's `loop()` does not run while the menu is current
  (`ActivityManager.cpp:89`). No build task exists (`grep -rn xTaskCreate lib/Epub src/activities/reader` finds
  nothing). The only out-of-reader `buildSomeMore` caller is `PassageSelectActivity.cpp:142`, which is a
  sub-screen that re-renders on return. Some loop-side changes land in the same tick as the menu tap and
  lose their render to the push. Those are a background build that resets or repositions
  (`EpubReaderActivity.cpp:496-509`), toast expiry (`:574-576`), and a queued manual turn (`:668-676`).
  `fit()`'s position and section checks catch each one, or the restore is correct without them (an
  expired toast is not redrawn).
- **The decision's inputs.** The menu starts auto turn at option 0 (`EpubReaderMenuActivity.h:85`), and
  `start(0)` turns it off (`AutoPageTurn.h:14-22`), so A-11.4 is right that any close with auto turn on
  changes the page. The Home-key hold opens the menu before `handleEndOfBookMenu`
  (`EpubReaderActivity.cpp:606-627`), so A-11.2 is right. Every reopen path renders before reopening, except
  night mode (`:306-326`), and night is excluded.
- **Memory.** The `PsramFree` idiom is at `CatalogIndexStore.h:83-89` and `.cpp:40-46`. `ESP.getFreeHeap()`
  and `getMaxAllocHeap()` are internal-only (`framework-arduinoespressif32/cores/esp32/Esp.cpp:163-173`), so the
  cache does not move the SSD1677 internal-heap gate (`EpubReaderActivity.cpp:1739-1746`). The handler type is
  the existing `std::function` (`ActivityResult.h:106`). Adding two `bool`s to `[this]` keeps the closure at 8
  bytes, the same as `[this, cancelTo]` (`:963`).
- **Scope against siblings.** `ReaderActivity.cpp` is not touched (t3). t3's spec marks `EpubReaderActivity`
  as held by t4 and serialises behind it
  (`../perf-r12-bible-open/docs/superpowers/specs/2026-10-11-berean-o-3-design.md:193`). After a restore the
  driver state matches a render, so t1's reasoning about the panel state after the reader still holds.
- **The test placement.** `test/ui_layout/CMakeLists.txt` is the suite's own file, and the suite is already
  registered (`test/CMakeLists.txt:102`). No shared append point is touched. `test/stubs/` has no
  `heap_caps` shim (`grep -rln heap_caps test/` finds nothing).
- **The citations.** Spot-checked about 60 of them; all are accurate except the two noted in finding 9.

No finding below reverses a decision, changes scope, or needs a human call. All are fixable inline.

## Findings

### 1. MINOR: the record's lifecycle is the real safety mechanism, and it is neither specified nor tested

- **Claim.** Testing strategy: "`PageFrameCache` itself is not host-built … Its logic is the record `fit()`
  reads, which T-4 and T-5 cover."
- **Problem.** T-4 and T-5 hand `fit()` records that are already built. They never test how a record changes
  over time:
  - capture sets B/W and position;
  - a gray capture sets a plane flag only when its allocation succeeded;
  - `noteGrayShown()` sets `grayShown`;
  - `invalidate()` must reset *all* of it.

  The spec never says that `invalidate()` resets the plane flags and `grayShown`. A-8 says only that a
  render that does not end in a captured page "leaves nothing to restore". It does not cover a render that
  *does* capture B/W but no planes: the one-buffer SSD1677 branch (`EpubReaderActivity.cpp:1759-1761`), or a
  skipped gray pass (`:1831-1834`). Suppose an implementation clears only the B/W flag. A restore would then
  apply the *previous* page's gray masks to this page, which is visible corruption. `fit()` cannot detect it,
  and V-1 to V-9 almost never reach those branches on the owner's UC8179.
- **Fix.**
  - State that `invalidate()` resets the whole `CapturedPage` to its default.
  - Move the transitions into the pure header as small methods on `CapturedPage`: `reset()`,
    `beginPage(spine, page)`, `markPlane(plane)` and `markGrayShown()`. That leaves `PageFrameCache` with only
    allocation and `memcpy`.
  - Add T-7: capture both planes with `grayShown`, then `reset()`, then `beginPage` plus `markGrayShown`
    with no planes. Expect `GrayMissing`.

### 2. MINOR: gray captures are not gated on `!pageHasImages`, contradicting A-10 and V-6

- **Claim.**
  - A-10: "48,000 B of PSRAM for an AA-off session."
  - V-6: "PSRAM down by 48,000 B (AA off)."
  - Data flow §1 gates only `captureBw` on `!pageHasImages`.
- **Problem.** With AA off, an image page still runs the gray pass. `needsAnyGrayscale = needsTextGrayscale ||
  pageHasImages` (`EpubReaderActivity.cpp:1643`). On UC8179 that is the non-tiled branch (`:1829-1849`). The
  ungated `captureGrayPlane` calls at `:1839-1840` and `:1845-1846` therefore allocate the 96,000 B gray pair
  on the first image page of an AA-off session, and pay two copies on every image page, for a record that
  `fit()` always rejects as `Empty` (A-6). The tiled branch is unaffected, because `overlapRefresh` already
  requires `!pageHasImages` (`:1650`).
- **Fix.** Gate `captureGrayPlane` and `noteGrayShown` on `!pageHasImages`, the same as `captureBw`. Optionally
  skip all captures while `SETTINGS.screenInverted` is set too, since A-12 never restores them.

### 3. MINOR: A-5 shows a flat page where AC3 asks for grays, and today's close would retry them

- **Claim.** A-5: "A page whose gray pass was skipped is shown flat … the restore shows it flat, as the panel
  showed it."
- **Problem.** AC3 says "the restored page keeps its gray levels (no flat B/W page left behind)." When
  `storeBwBuffer` fails (`:1831-1834`) or the strip scratch runs out of memory (`:1778-1790`), today's close
  re-renders and tries the gray pass again. The design instead pins the flat page until the next turn. Both
  allocations are 8,000 B, so they go to PSRAM by size (`GfxRenderer.cpp:2310`, `framework-arduinoespressif32-libs/esp32s3/sdkconfig:2160`), and this is
  rare. It is still a literal miss of AC3 that costs almost nothing to avoid.
- **Fix.** Record `grayExpected` (`needsAnyGrayscale && !inverted`) at capture. Have `fit()` return
  `GrayMissing` when `grayExpected && !grayShown`, so the close re-renders and retries the grays. Add the
  case to T-5.

### 4. MINOR: A-20 and A-21 miss that a background build changes the status bar and the progress save

- **Claim.**
  - A-20: "The progress save is skipped, because the page did not change (`:1498-1505` would find nothing to
    save)."
  - A-21 names only the battery as stale.
- **Problem.**
  - The reader's loop advances section builds between renders without re-rendering
    (`EpubReaderActivity.cpp:496-509`). It requests a render only on failure or a reposition.
  - The status bar shows `estimatedTotalPages()` with a `~` prefix while `isBuilding()`
    (`:1904`, `:1930-1931`, `BaseTheme.cpp:605-606`). After a build tick, a restored close therefore shows the
    old `~N/M` and the old "estimated" state. Today's close refreshes them.
  - The save condition compares `section->pageCount` with `lastSavedPageCount`, but stores
    `estimatedTotalPages()` there (`:1498`, `:1503`). While a section is building or partial, that differs on
    every render, so today's close *does* save. A-20's statement is false in that state.
- **Fix.** Either add `pageCount` and `isBuilding` to `CapturedPage` and refuse on a mismatch (a new `Fit`
  reason such as `stale`), or accept it explicitly. If accepted, state it in A-21 and Risks next to the
  battery, and correct A-20's wording. Accepting is the cheaper choice and consistent with A-21.

### 5. MINOR: the third X4 Pro controller, UC8279, is not mentioned

- **Claim.** G-4 and V-9 treat the X4 Pro as UC8179 or SSD1677, and A-17's equivalence argument is
  UC8179-specific.
- **Problem.** The X4 Pro build links a third driver: `FREEINK_DRIVER_UC8279_X4 1` for
  `FREEINK_DEVICE_X4PRO` (`freeink-sdk/libs/hardware/BoardConfig/include/BoardConfig.h:110-131`), selected at
  boot (`FreeInkDisplay.cpp:150-158`). The design is still correct there. `Uc8279X4Driver` has no strip
  support, so it takes the non-strip branch. Its `copyGrayscale*` stream the masks directly
  (`Uc8279X4Driver.cpp:276-282`), `displayGray` ignores `fb` (`:329`), and cleanup re-seeds DTM1 from the B/W
  frame (`:333-343`). But the spec gives no reason to believe that.
- **Fix.** Add a line to A-17 giving that evidence, and name UC8279 in G-4 and V-9 as untested, like SSD1677.

### 6. MINOR: V-5 cannot measure the per-turn cost it is meant to check

- **Claim.** V-5: "`Page render ... total=` medians within ~10 ms of the baseline (1,378 / 741 ms)."
- **Problem.** The effect A-2 predicts is 2–6 ms. The FAST waveform alone varies by 15 ms (551–566 ms,
  `responsiveness-baseline.md`), and the comparison is against another session's median. ±10 ms is mostly
  noise, so V-5 can neither confirm nor refute A-2.
- **Fix.** Time the captures directly: add a `capture=%lums` field to the existing `Page render` lines, or a
  `LOG_DBG` per page. Make V-5's criterion that field (≤ 2 ms per plane), not the total.

### 7. MINOR: A-4 says the one-buffer tiled branch has no full plane, but it has both, one after the other

- **Claim.** A-4: "**Tiled with one buffer** (`:1759-1761`) … no full plane exists, so nothing is captured."
- **Problem.** On that branch `lsbPlaneBuf` holds the complete LSB plane after `:1749`, and the complete MSB
  plane after `:1760`, which renders the MSB into the same buffer. Both could be captured.
- **Fix.** Either capture there (LSB after `:1749`, MSB after `:1760`), or correct the reason: the branch is
  rare, it exists only under internal-heap pressure, and it is left out for simplicity.

### 8. MINOR: the `fit()` call in the restore pseudo-code dereferences a null section

- **Claim.** Data flow §3: `fit(pageCache.captured(), {section != nullptr, currentSpineIndex, section->currentPage})`.
- **Problem.** `NoSection` is a listed outcome (T-4, the error table), but this expression evaluates
  `section->currentPage` when `section` is null. A plan that copies it verbatim crashes the render task
  instead of refusing.
- **Fix.** Write it as
  `section ? Position{true, currentSpineIndex, section->currentPage} : Position{false, currentSpineIndex, -1}`,
  or have `fit()` take the section's presence and read the page only when it is present.

### 9. MINOR: two citations

- **Problem.**
  - `HalDisplay::copyGrayscaleBuffers` is at `HalDisplay.h:88`, not `:87` (Architecture and A-19).
  - A-7 cites `restoreBwBuffer` as precedent for writing the framebuffer "outside the renderer". It is inside
    `GfxRenderer`. The real outside precedent is `loadSleepFrameBuffer`, which reads straight into
    `display.getFrameBuffer()` (`src/main.cpp:228-240`).
- **Fix.** Correct both.

## Verdict

The design answers every question in the brief, and its central claim holds against the driver code: the
restore replays the panel calls the render already makes, with saved masks and no renders. All nine findings
are MINOR and can be fixed inline. Findings 1 to 3 should be applied before the plan, because they decide
what the record holds and what T-5 and T-7 assert.

VERDICT: CLEAR
