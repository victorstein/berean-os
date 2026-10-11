Tier: standard

# berean-o-2 (R9) PR review 1

PR #262, `perf/r9-cover-band-cache` at `9fde5a94`, against `main`. I reviewed it against the brief
(`hpipe brief --task t2`), the spec (`docs/superpowers/specs/2026-10-11-berean-o-2-design.md`) and
the plan (`docs/superpowers/plans/2026-10-11-berean-o-2-plan.md`). Every claim below was checked in
the source at HEAD, not taken from the PR body. Review 0 covered `f1743e8d`. This review covers the
whole PR again, with extra attention on the three commits since then (`8c44075f`, `9995c979`,
`9fde5a94`).

## How this was checked

- **CI at `9fde5a94`.** Every check is green: `Build x4pro`, `unit-tests`, `cppcheck`, `clang-format`,
  `script-tests`, `lint-title`, `Title Check` and `Test Status`. Review 0's `cppcheck` failure is
  fixed.
- **Host suites, run locally.** `BandInkTest` passed 6/6, `CoverBandReplayTest` 15/15 and
  `CoverBandGeometryTest` 16/16. `StorageIoTest` also built.
- **Mutation runs.** For each mutation I compiled a changed copy of `CoverBand.cpp` or
  `CoverBandCache.cpp` in the scratchpad and linked it against this build's other objects. The
  worktree was not edited.

  | Mutation | Result |
  |---|---|
  | `&=` changed to `=` in the replay | 4 tests fail. This is the PR's own claim, and it holds. |
  | FIFO: `find` no longer refreshes `lastUse` | The LRU test fails (review 0 finding 2, fixed) |
  | `victim` always takes slot 0 | The LRU test fails (fixed) |
  | `insideScreen` dropped from the fast path | The bottom-edge test fails (review 0 finding 3, fixed) |
  | Epoch ignored in `isLive` | 1 test fails |
  | `memset` dropped in `reserve` | 6 tests fail |
  | `commit` instead of `abandon` on a failed blit | The truncated-cover test fails |
  | Over-cap check removed | The cap test fails |
  | Empty-path early return removed | The empty-path test fails |
  | Stale-epoch slots treated as live in `victim` | 15/15 pass. This is an equivalent mutant: `lastUse` is monotonic, so LRU picks a stale slot first anyway. Not a gap. |
  | **Strip row clip removed from the fast path** (`CoverBand.cpp:103`) | **15/15 pass.** See finding 1. |

- **Plan conformance.** I extracted the plan's whole-file code blocks and diffed them against HEAD.
  - `BandInk.h`, `BandInkTest.cpp`, `CoverBandCache.h` and `CoverBand.cpp` match byte for byte.
  - `CoverBandCache.cpp` differs only in the three `std::` algorithm rewrites.
  - `CoverBandReplayTest.cpp` differs only in the bottom-edge test and the strengthened LRU sequence.
  - Both differences are review 0's fixes, and the PR body's "Review" section explains them.

## Intent

**The brief's acceptance criteria.**

- **Home gets a band timing log: met.** `drawHero` brackets `CoverBand::draw` with `millis()` and
  logs `[LAUNCH] Band drawn|replayed|declined in N ms` (`LauncherActivity.cpp:250-255`, with
  `MODULE = "LAUNCH"` at `:52`). That is the same shape as `Masthead.cpp:83-85`.
- **Later paints replay in a few ms, on Home and every masthead: met in code.**
  - Both callers go through `CoverBand::draw`, which tries `CoverBandCache::find` before touching
    the card (`CoverBand.cpp`, `draw`).
  - The cache is a function-local singleton, so it outlives the activities that navigation destroys.
  - The Portrait replay is one `&=` per mask byte through the raw-writer accessors
    (`GfxRenderer.h:228-230`). I checked it line by line against `GfxRenderer::drawPixel`
    (`GfxRenderer.cpp:561-600`): same target, strip clip, byte, bit and polarity.
  - The device figure belongs to the owner. The PR body says "No device result is claimed here".
- **Replayed pixels are identical, proven by a host test: met.**
  - Fresh draw and replay compare all 48,000 bytes from the same seeded frame.
  - The comparison covers all three styles × 8 alignments (Home at 272 rows), both BMP row orders,
    strip mode, and the `drawPixel` fallback for another orientation, x off-screen and y off-screen.
  - The OOM test shows that a first paint without recording matches one with it.
- **PSRAM held is bounded and stated: met.**
  - The hard cap is 4 × 24,576 = 98,304 B, enforced by `MAX_SLOT_BYTES` in `reserve`.
  - The expected figure, 38,512 B, is in the PR body.
  - `bytesHeld()` counts every slot buffer, including stale ones.

**The brief's other requirements.**

- PSRAM is explicit, through `heap_caps_malloc(SPIRAM|8BIT)` with the `PsramFree` deleter, as in
  `CatalogIndexStore.h:83-89` and `PageFrameCache.h:33-36`.
- There is no `std::function`, and the timings are `LOG_DBG`.
- Neither `test/CMakeLists.txt` nor `src/main.cpp` is edited, and the PR body has a "For the
  orchestrator" section.
- The PR body has the before figure, the exact after log lines and the device steps.
- The title is a conventional `perf:` title, and the body ends `Refs berean-o-2`.

**Two brief suggestions the spec changed, both with reasons.**

- **File size is not in the key.** Spec A2 shows that a 1-bit thumbnail's size is fixed by its
  dimensions, so it cannot tell two covers apart. The epoch takes its place, which fits the brief's
  "or another cheap identity".
- **Orientation needs no handling of its own.** The PR body's Invalidation section names only sleep
  and theme, but spec A5 covers orientation. It is also correct by construction: the mask is held in
  logical band coordinates, and only the choice of the fast path depends on orientation.

**Spec coverage.**

- **Implemented in full.**
  - A1: the key is every argument of `draw`, plus the epoch. `EveryArgumentIsPartOfTheKey`
    exercises all eight fields.
  - A3 and A4: the five bump sites each come after their removal or write:
    - `BookCacheUtils.cpp:30`;
    - `CardBooks.cpp:59`;
    - `CoverThumb.cpp:48`, only on a successful generation;
    - `ClearCacheActivity.cpp:138`;
    - `EpubReaderActivity.cpp:916`.
  - The epoch is read in `reserve` before the blit opens the file.
  - A12 and A13: over the cap, or out of PSRAM, the band is still drawn.
  - A15: `Result` replaces `bool`.
  - The constraint comment is at `drawPixel`.
  - All five §8 log lines are present.
  - All twelve §11 cases are present.
- **Thumbnail generation does not bump on every lookup.** I re-checked every call site of
  `CoverThumb::pathFor` and `pickCover`: Launcher `:121`, Publications `:111,122`, Meetings
  `:164,253`. Each runs at load time, and `pathFor` returns before the bump when the thumbnail
  already exists (`CoverThumb.cpp:36`).
- **One removal path does not bump, and it is harmless.** With hidden files shown, the file browser
  can delete a thumbnail directly. `clearBookCache` returns early for a `.bmp`, so no bump follows.
  But every band-painting screen re-resolves its cover on entry (A6). A missing thumbnail is
  therefore either regenerated, which bumps, or gives an empty path, which is `Declined` before the
  cache is consulted.

**Scope.**

- There is no reduction and no expansion. The files touched are spec §14's.
- The follow-ups, `t2/x1` and `t2/x2`, are filed as discoveries, not folded in.

**Tests exercise behaviour.** They do. The replay suite runs the real `CoverBand`, `CoverBandCache`
and `Bitmap` code against a device-sized framebuffer, and the mutation table shows the equality,
key, epoch, LRU, OOM, cap and failure-path tests all fail when the code they guard is mutated. The
one exception is finding 1.

## Quality

- **It mirrors existing patterns.**
  - The singleton shape follows `HomeVerse`.
  - The per-class `PsramFree` deleter matches `CatalogIndexStore` and `PageFrameCache` exactly,
    down to the comment.
  - The raw-writer replay uses the accessors that `GfxRenderer.h:223` documents for that purpose.
  - The link-time renderer fake follows `test/pagination/GfxRendererFake.cpp`.
- **Duplication.**
  - The fake's copies of `drawPixel`, `rotateCoordinates` and `begin/endStripTarget` are necessary,
    because no host target compiles `GfxRenderer.cpp`.
  - The fake cites the right lines at HEAD: `:561-600`, `:283-313` and `:1687-1703`.
  - No other test stubs `<esp_heap_caps.h>`, and no other fake carries a pixel framebuffer, so
    nothing is duplicated.
- **Naming and structure.**
  - `Result` and `resultName` sit naturally in `CoverBand`.
  - `finishBand` is today's corner-and-plate code moved into a helper, unchanged.
  - The `noteCoversChanged` forwarder keeps `src/util` on the feature's public header.
- **The `cppcheck` rewrite.**
  - `std::find_if`, `std::accumulate` and `std::generate` read cleanly.
  - `generate` is the right choice in `clear()`: `Slot` holds a `unique_ptr`, so `std::fill`
    cannot copy it.
- **Comments.** Each carries a reason: the epoch ordering, the deleter, the panel size, the slot
  count and the cap. None paraphrases its next line.
- **Error handling.** It is "LOG_ERR + fallback" throughout. No path refuses to paint.
- **Resources.**
  - No bare `new`.
  - About 0.3 KB of `.bss` and one small path string per slot in internal SRAM.
  - A handful of stack scalars.

## Findings

1. **MINOR — The strip test does not pin the fast path's strip clip. Removing the clip passes every
   test and turns the replay into an out-of-bounds write.**
   - **Where.** `AStripTargetClipsTheReplayAsItClipsTheDraw`
     (`test/cover_band/CoverBandReplayTest.cpp:193-214`) sizes the strip scratch to exactly
     `STRIP_ROWS` rows (`:196`), and passes its start as the target (`:200`).
   - **Which rows the clip drops.** The band's columns map to physical rows 8..471, so `rowY` runs
     from −92 to 371 against `rows = 200`. In those rows the clip at `CoverBand.cpp:103` is all
     that stops the replay writing:
     - 9,200 B before the scratch;
     - up to about 17,200 B past it.
   - **What happens without it.**
     - The uninstrumented build that CI runs passes 15/15.
     - The compared strip rows are unaffected.
     - The writes land in heap memory that nothing compares.
     - An ASan build of the same mutant reports `heap-buffer-overflow` in `CoverBand::draw` during
       this test.
   - **Why it matters.**
     - The PR's own drift comment at `GfxRenderer.cpp:559-560` names "the strip clip" as something
       the replay copies.
     - The test's name claims it is checked, and no test guards it.
     - The strip target is active only in the reader's grayscale loop
       (`EpubReaderActivity.cpp:1819,1888,1899`), so no band is painted under it today. That makes
       this a test gap, not a live bug.
   - **Fix inline.** Size the scratch as a whole frame, `std::vector<uint8_t>
     strip(gfx_fake::FRAME_BYTES)`, and pass
     `strip.data() + STRIP_Y0 * gfx_fake::PANEL_WIDTH_BYTES` to `beginStripTarget`. Leave the
     existing `freshStrip == strip` assertion as it is.
     - The rows outside `[STRIP_Y0, STRIP_Y0 + STRIP_ROWS)` then act as seeded guard rows. The fresh
       draw's `drawPixel` never touches them, but an unclipped replay writes into them.
     - I built this variant in the scratchpad. The real code passes 15/15, and the no-clip mutant
       fails exactly this test.

No BLOCKER and no MAJOR. Review 0's MAJOR (`cppcheck`) and both MINORs are fixed, and the mutation
runs above confirm each fix. Finding 1 is a one-hunk test change. The design, the replay arithmetic,
the invalidation sites and the pixel-identity proof are sound.

VERDICT: CLEAR
