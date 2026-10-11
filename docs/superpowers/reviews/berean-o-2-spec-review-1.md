Tier: standard

# berean-o-2 spec review, pass 1

Reviewed: `docs/superpowers/specs/2026-10-11-berean-o-2-design.md` (at `8b742558`), against the
brief (`hpipe brief --task t2`) and `docs/superpowers/research/2026-10-11-berean-o-2-research.md`.
No prior review exists for this bead (pass 0 wrote nothing), so there is no earlier fix to re-check.

## Summary

The design holds. The two load-bearing claims both check out against the code.

- The byte-wise Portrait replay is formula-identical to `drawPixel`.
- The epoch invalidation is sound on every removal and regeneration path I traced.

There are no BLOCKERs. There is one MAJOR: the G3 host test, as written, cannot tell a correct
`&=` replay from one that overwrites whole bytes. Seeding the framebuffer fixes it. The MINORs are
estimate honesty, citation accuracy, a missing fake helper and verification gaps. All of them can
be fixed inline in the spec or the plan.

## Findings

### MAJOR 1: the G3 test cannot detect a replay that writes paper or clobbers neighbouring bits

- **Claim.** §11 case 1: "The framebuffer after the replay is byte-identical to the one after the
  fresh draw." The test proves G3 and A10 ("it only clears bits, so it adds ink exactly as the blit
  does").
- **Problem.** §11 never says what the framebuffer holds before each draw. The natural fixture is
  a cleared (all-`0xFF`) buffer. Against paper, a broken fast path gives byte-identical results to
  the correct `dst[k] &= ~ink[…]`:
  - `dst[k] = ~ink[…]`, a whole-byte store;
  - a restore that writes paper, which is #238's rejected snapshot failure (§13 row 2).
  
  The reason: inside the band the blit only clears bits, and the mask's head and tail bits are 0
  (the §6 invariant), so `~0` also leaves them white. Both bugs would pass G3 on the host. On the
  device they would erase:
  - any ink already inside the band;
  - up to 7 rows above and below the band that share its head and tail framebuffer bytes.
  
  Mastheads paint over a framebuffer that "already holds the list rows" (research §3;
  `UiListActivity.cpp:154-168`, where `drawFooter` runs after `renderUi`). So the bug this test
  exists to catch would ship.
- **Evidence.**
  - The fast path writes whole bytes over `layout.stride` per column (spec §7 `replayInk`, line 273).
  - Head bits `lead = y & 7` and tail bits exist for every alignment except `lead == 0` with
    `height % 8 == 0` (§6).
  - `drawPixel` RMWs a single bit (`GfxRenderer.cpp:589-597`).
- **Fix.** In `CoverBandReplayTest`, fill the 48,000 B buffer with the same seeded pseudo-random
  pattern before both the fresh draw and the replay, then compare all 48,000 bytes. The pattern
  must cover:
  - pixels inside the band;
  - the head and tail bits of every band column's bytes (rows `y-lead … y-1` and
    `y+height … ((y+height-1)|7)`);
  - the rest of the frame.
  
  Do this for every case that compares buffers: 1, 2, 4, 5, 7 and 9. Also add one targeted
  assertion: with `lead != 0`, a black pixel at `(band.x, band.y-1)` survives the replay.

### MINOR 2: "recording adds a few ms" ignores memory placement and access order

- **Claim.** "Recording during the fill adds at most one bit-set per inked pixel to the first
  paint: a few ms on top of ~225 ms" (§4, lines 75-76). Also §10: the masks are kept "out of
  internal SRAM, which the framebuffer and render path need."
- **Problem.** The mask is column-major (A11), but `blitCropped` walks row-major
  (`CoverBand.cpp:39-55`). Each inked pixel's `set` therefore lands `stride` bytes (16 or 35) away
  from the previous one, in PSRAM.
  
  The framebuffer is probably in PSRAM too:
  - `FreeInkDisplay::allocFrameBufferStorage` falls back to plain `malloc(bufferSize)`
    (`freeink-sdk/libs/display/FreeInkDisplay/src/FreeInkDisplay.cpp:368-380`).
  - This build has `CONFIG_SPIRAM_USE_MALLOC=y` and `CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=4096`
    (`framework-arduinoespressif32-libs/esp32s3/sdkconfig:2159-2160`).
  - Sibling discovery `t4/x1` reaches the same conclusion; it is still unverified on the device.
  
  In one blit row, both the band's framebuffer lines and the mask lines go through a 32 KB data
  cache with 32 B lines (`sdkconfig:2208,2215`). On Home that is roughly 464 + 464 lines, about
  29.7 KB. The first-paint cost of recording may be more than "a few ms". The replay estimate
  (≤ 2 ms) still holds, because the replay walks both buffers in physical-row order. The choice of
  mechanism is not in question.
- **Fix.**
  - Label the first-paint delta as unmeasured, and state the PSRAM-framebuffer caveat in §4 and §10.
  - In §12, have the PR report the first-paint `Band drawn in N ms` against the 225 ms baseline.
  - Name the fallback if it regresses visibly: stage 8 rows of a column in one internal byte,
    then flush to the mask once per 8-row group, so mask traffic drops 8×.

### MINOR 3: the fake's `drawPixel` copy also needs `rotateCoordinates`

- **Claim.** §11: the fake's `drawPixel` is "a line-for-line copy of `GfxRenderer.cpp:559-598`".
- **Problem.** That copy calls `rotateCoordinates` (`GfxRenderer.cpp:568`). It is a file-`static`
  function at `GfxRenderer.cpp:283-311`, so linking cannot supply it. Case 4 (`PortraitInverted`)
  goes through it.
- **Fix.** Name `:283-311` as copied too.

### MINOR 4: A3's reasoning about the non-bumping removals is imprecise; two cheap bumps make it unconditional

- **Claim.** A3: "The paths that remove or move without a bump each leave the **same book's**
  cover at that path." The paths are `ClearCacheActivity.cpp:97-130` and
  `EpubReaderActivity.cpp:898`.
- **Problem.** Both paths leave **no** file until something regenerates it. Correctness then rests
  on three things:
  - A6, which replays without checking existence;
  - every band-painting screen re-resolving before it paints;
  - regeneration going only through `CoverThumb::pathFor`, which bumps.
  
  I checked all three, and they hold today:
  - `generateThumbBmp` has one caller (`CoverThumb.cpp:42`).
  - Go to checks `Masthead::fits` (`BibleNavigationActivity.cpp:88-89`).
  - Publications and Meetings go through `pickCover` → `fits` (`Masthead.cpp:66-75`).
  - Home goes through `resolveTargets` → `thumbPathFor` (`LauncherActivity.cpp:117-128`).
  
  It is still a chain of conditions where a single line would do.
- **Fix.** Optional hardening. Either reword the sentence to say what actually happens, or add
  `CoverBand::noteCoversChanged()`:
  - after the loop in `ClearCacheActivity::clearCache`;
  - after `epub->clearCache()` at `EpubReaderActivity.cpp:898`.
  
  Both files are on ui-dev's surface. Then A3's table can read "every removal bumps".

### MINOR 5: device verification never exercises invalidation, and one expected log can mislead

- **Problem.**
  - The behaviour most likely to be wrong on the device is a stale cover after a replacement. §12
    checks only that `replayed` appears.
  - The global epoch is bumped by **any** thumbnail generation (A3 row 3). That includes
    Publications' row thumbnails (`PublicationsActivity.cpp:92,124`) and Meetings' card cover
    (`MeetingsActivity.cpp:253`). So the first Home paint after a first visit to either screen
    logs `drawn`, not `replayed`. §12's step-by-step expectations do not say so, and the owner
    could read it as a failure.
- **Fix.** Add a step to §12: delete a publication whose cover is on a masthead (Publications →
  delete, through `CardBooks::remove`), or re-download it. The next paint of that band must log
  `drawn` and show the current cover, or the fallback header. Add one sentence: "any thumbnail
  generation flushes every held band, so the first paint after a screen that generated thumbnails
  logs `drawn`."

### MINOR 6: some baseline figures are cited to lines that do not contain them

- **Claim.** §1 and §12: "`[MASTHEAD] Band drawn in 223–229 ms` on every Go to step,
  Publications paint and Meetings paint, n=11 (baseline note `:44-46,50,72`)." Research §9 says
  the same.
- **Problem.** The note
  (`git show docs/252-responsiveness-baseline:docs/superpowers/notes/responsiveness-baseline.md`)
  does not contain them:
  - `:45-46,50` give 225 ms, and `:72` gives the 228 ms line;
  - "223–229", "n=11" and Meetings appear nowhere in it.
  
  Those figures come from the bead brief. The brief requires the PR's before-figure "from the
  baseline".
- **Fix.** Cite the brief for n=11 / 223–229 / Meetings. Or quote only what the note states:
  225 ms for Go to and Publications, and the representative `[155060] … Band drawn in 228 ms`.

### MINOR 7: nothing ties the fast path and the fake to `drawPixel` if it changes

- **Problem.** `replayInk`'s Portrait path re-implements `drawPixel`'s mapping
  (`GfxRenderer.cpp:564-566,589-597`). The fake is a copy of it. A future change to `drawPixel`
  would break the device replay while the host test, comparing against its stale copy, stays
  green. §11 admits the gap, but nothing guards it.
- **Fix.** Put a one-line constraint comment at `GfxRenderer::drawPixel` naming
  `CoverBand.cpp`'s replay and `test/cover_band/GfxRendererFake.cpp` as copies of its Portrait
  mapping. This is a non-obvious "why", so it passes the repo's comment rule.

### MINOR 8: `reserve` can evict a valid band for a draw that then declines, and it treats stale slots as live

- **Problem.**
  - `reserve` runs before the file is opened (§7). A cover that declines on every paint still
    claims a slot each time. Examples: an unsupported cover leaves an empty BMP
    (`Epub.cpp:760-763`), and Home's hero path is never checked with `fits`
    (`LauncherActivity.cpp:248`). Each time it `memset`s the slot, and it reallocates when the
    size differs.
  - After a bump, every held slot is stale but still "valid". So `reserve` evicts by LRU instead
    of reusing a stale slot first, and `bytesHeld()` counts stale masks.
  
  The damage is small. Reserve prefers invalid slots, so at most one valid band is lost.
- **Fix.** In `reserve`, treat a slot whose epoch differs from the current one as invalid. Keep
  the epoch read before the open, as A4 requires. Optionally defer zeroing until after
  `parseHeaders` and `crop.fits` succeed.

## Checked and sound

- **Fast-path equivalence (A10, §6, §7).** Portrait maps `phyX = y`, `phyY = panelHeight-1-x`,
  with the bit `7 - phyX%8` (`GfxRenderer.cpp:564-566,590-597`). The strip clip is
  `rowY = phyY - _stripY0` within `_stripRows` (`:580-587`). Both match `getWriteTarget`,
  `getWriteOriginY` and `getWriteRows` (`GfxRenderer.h:228-230`), and `getDisplayHeight` and
  `getDisplayWidthBytes` exist (`GfxRenderer.h:374-375`). With the band ⊂ screen
  (`getScreenWidth` = `panelHeight` in Portrait, `GfxRenderer.cpp` `getScreenWidth`), the highest
  byte is `((y+h-1)>>3) ≤ 99`. `drawPixel` reads neither `renderMode` nor the scan state, and scan
  mode is reader-only (`PrewarmScope` callers are all in `src/activities/reader`).
- **Mask sizes (§6, §10).** 464×16 = 7,424 B; 464×35 = 16,240 B; 464×33 = 15,312 B. Total
  3×7,424 + 16,240 = 38,512 B. Band and hero dimensions: `HomeLayoutTest.cpp:45-66`,
  `BaseTheme.h:201`, `LyraTheme.h:79`.
- **The key covers the decode (A1).** The 1-bit and 2-bit path is
  `adjustPixel(lum) >> 6`, and `USE_BRIGHTNESS` is `constexpr false`
  (`Bitmap.cpp` `packPixel`; `BitmapHelpers.cpp:8,43-44`). `quantize` is a pure hash of (x, y)
  (`BitmapHelpers.cpp:71-93`). Only two themes exist, and their plate heights differ (46 and 45),
  so a theme switch changes the key (A5).
- **Every thumbnail goes through `CoverThumb::pathFor`.** `generateThumbBmp` has one caller
  (`CoverThumb.cpp:42`). The file is never overwritten in place (`Epub.cpp:670-674`). A failed
  generation removes the partial file (`Epub.cpp:717,752`).
- **The A3 bump sites.** Every `clearBookCache` caller listed matches the grep (FileRoutes, WebDAV,
  CrossPointWebServer, PublicationDownloader, FileBrowser). The downloader's cache-directory move
  follows its `clearBookCache` (`PublicationDownloader.cpp:110,116`). Web routes refuse
  dot-directories (`FileRoutes.cpp:124,179,315`). The firmware does not use USB mass storage
  (`grep UsbMassStorage src lib`: none).
- **A7, the single render task.** `ActivityManager.cpp:32-38,47-60`. The only callers of
  `CoverBand::draw` are `Masthead.cpp:84` and `LauncherActivity.cpp:248`. `Masthead::draw` is
  reached only from the three `drawFooter`s, and `drawFooter` only from `UiListActivity::render`
  (`:168`).
- **The host harness is feasible.**
  - `Bitmap.cpp` and `CoverBand.cpp` pass `c++ -std=gnu++20 -fsyntax-only` against `test/stubs`.
  - `HalFile::seekCur` is declared (`test/stubs/HalStorage.h:82`) but not defined in the fake.
  - `storage_fake::readCallsOn` exists (`HalStorageFake.h:46`).
  - `test/cover_band` is registered at `test/CMakeLists.txt:126`.
  - No host test compiles `BookCacheUtils.cpp`, `CardBooks.cpp` or `CoverThumb.cpp`, so the new
    bump calls break no existing link.
- **The cost basis.**
  - `GfxRenderer::drawPixel` is at `0x420468d0`. Its Portrait in-range path is about 35–40
    instructions (`xtensa-esp32s3-elf-objdump -d`, `.flash.text`).
  - `CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ 240` (`sdkconfig.h:1046`), and the render task holds
    `HalPowerManager::Lock` (`ActivityManager.cpp:54`).
  - The finishing primitives are byte-wise: `fillRect` and `fillRectDither` reach `fillRectImpl`
    (`GfxRenderer.cpp:935-987`). So a logged `replayed` time is not dominated by `finishBand`.
- **The brief's rules.**
  - Explicit PSRAM uses the `PsramFree` idiom (`CatalogIndexStore.h:83-89`, `.cpp:40-47`).
  - No `std::function`, and timings use `LOG_DBG`.
  - Neither `test/CMakeLists.txt` nor `src/main.cpp` is touched.
  - Departing from the brief's file-size key is justified (A2: `JpegToBmpConverter.cpp:88-90`,
    `PngToBmpConverter.cpp:129`), and the brief allows "another cheap identity".
  - The departure from #238's `drawPixel` replay is driven by the G2 acceptance criterion, not by
    scope.

VERDICT: CLEAR
