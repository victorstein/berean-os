Tier: standard

# berean-o-2 (R9) PR review 0

PR #262, `perf/r9-cover-band-cache` at `f1743e8d`, against `main`. I reviewed it against the brief
(`hpipe brief --task t2`), the spec (`docs/superpowers/specs/2026-10-11-berean-o-2-design.md`) and
the plan (`docs/superpowers/plans/2026-10-11-berean-o-2-plan.md`). I checked every claim below in the
source at HEAD rather than taking it from the PR body.

## How this was checked

- **Host suites.** I built and ran `CoverBandReplayTest` (14 passed), `BandInkTest` (6) and
  `CoverBandGeometryTest`, with `StorageIoTest` built alongside.
- **Mutation runs.** I rebuilt a mutated copy of `CoverBandCache.cpp` or `CoverBand.cpp` in the
  scratchpad and linked it against the build's other objects. The worktree was not edited. Results:
  - The PR's own mutation, `&=` → `=` in the replay, fails 4 tests.
  - Recording one row off fails 5.
  - Dropping the `memset` in `reserve` fails 5.
  - Ignoring the strip target fails 1.
  - Ignoring the epoch in `isLive` fails 1.
  - Three mutations pass all 14: FIFO eviction, evict-slot-0, and no `insideScreen` guard. See
    findings 2 and 3.
- **Plan conformance.** I extracted the plan's code blocks and compared them with HEAD. Every
  whole-file block matches its file byte for byte: `BandInk.h`, `CoverBandCache.{h,cpp}`,
  `CoverBand.{h,cpp}`, `BandInkTest.cpp`, `CoverBandReplayTest.cpp` and the fakes. Every `new`
  snippet is present.
- **CI at review time.**
  - Passing: `Build x4pro`, `unit-tests`, `clang-format`, `script-tests`, `lint-title` and
    `Title Check`.
  - Failing: **`cppcheck`** (finding 1). The merge state is `UNSTABLE`.
  - `main`'s last CI run (38156295019, `9d6f0c0d`) is green, so the failure comes from this PR.

## Intent

**Acceptance criteria (brief).**

- **Home gets a band timing log.** Met. `drawHero` brackets `CoverBand::draw` with `millis()` and
  logs `[LAUNCH] Band drawn|replayed|declined in N ms` (`LauncherActivity.cpp:250-255`). It uses
  `MODULE = "LAUNCH"` (`:52`) and the same `[[maybe_unused]] startMs` shape as `Masthead.cpp:83-85`.
- **A repeat paint replays in a few ms, on Home and on every masthead.** Met in code. Device
  confirmation is the owner's, and the PR claims none.
  - Both callers go through `CoverBand::draw`, which consults the cache first (`CoverBand.cpp`,
    `draw`). The three mastheads share `Masthead::draw` (`Masthead.cpp:84`).
  - Callers are destroyed on navigation, but the cache is a singleton, so it outlives them.
  - The Portrait replay is one `&=` per mask byte through the raw-writer accessors
    (`GfxRenderer.h:228-230`). I checked it against the real `drawPixel` (`GfxRenderer.cpp:561-600`)
    for the same target, strip clip, byte, bit and polarity.
- **Replay pixels are identical, proven by a host test.** Met. The test compares all 48,000 bytes
  of a seeded, non-blank frame after a fresh draw and after a replay, across:
  - 3 styles × 8 alignments, including the 272-row Home band;
  - both BMP row orders;
  - strip mode;
  - the `drawPixel` fallback.
  The OOM case also shows that a draw without recording matches a draw with it. So the first
  paint's pixels are today's.
- **PSRAM held is bounded and stated.** Met. The hard cap is 4 × 24,576 = 98,304 B, enforced by
  `MAX_SLOT_BYTES` in `reserve`. `bytesHeld()` counts every slot's buffer, held or stale. The PR body
  states the cap and the expected 38,512 B.

**Spec coverage.**

- **Implemented in full.**
  - A1–A15: the key is every argument plus the epoch.
  - Five bump sites, each placed after its removal or write completes:
    - `BookCacheUtils.cpp:30`, after `clearCache`;
    - `CardBooks.cpp:59`, after `removeDir`;
    - `CoverThumb.cpp:48`, only on successful generation, not on every lookup;
    - `ClearCacheActivity.cpp:138`, after the loop. No early return skips it once anything was
      removed.
    - `EpubReaderActivity.cpp:916`.
  - The epoch is read in `reserve` before the blit opens the file.
  - Stale-epoch slots count as invalid.
  - OOM and over-cap draw anyway.
  - `Result` replaces `bool`.
  - The constraint comment sits at `drawPixel`.
- **Other removal paths.** I grepped every `clearCache()`, `removeDir` and thumbnail `remove` in
  `src/` and `lib/`. None removes a thumbnail without passing a bump.
- **Render task only.** Every `CoverBand::draw` caller runs inside `render()` on the render task
  (`ActivityManager.cpp:47-60`). The A7 claim holds.
- **Logging.** All five §8 lines are present with the specified wording.
- **Tests.** All 12 §11 cases plus the ink-above-the-band check exist, and `BandInkTest` covers §11's
  list.

**Scope.** There is no reduction and no expansion. The files touched are exactly spec §14's. Neither
`test/CMakeLists.txt` nor `src/main.cpp` is edited. Follow-ups `t2/x1` and `t2/x2` are recorded as
discoveries (`hpipe discoveries`).

**Plan divergence.** None in code. The PR body adds a "Verified here" section to the plan's text.
One claim in it is wrong (finding 1).

**Tests exercise behaviour.** Mostly, yes. The replay suite runs the real `CoverBand`,
`CoverBandCache` and `Bitmap` against a framebuffer, and the mutation results above show the core
equality tests bite. Two tests are weaker than their names claim (findings 2 and 3).

## Quality

- **Mirrors existing patterns.**
  - The singleton follows `HomeVerse` and `CatalogIndexStore`.
  - The per-class `PsramFree` deleter with `heap_caps_malloc(SPIRAM|8BIT)` is the repo's
    established idiom: `CatalogIndexStore.h:86-89`, `PageFrameCache.h:33-36`.
  - The raw-writer replay follows `DirectPixelWriter`'s use of `getWriteTarget` and friends.
  - The fake follows `test/pagination/GfxRendererFake.cpp`.
- **Duplication.** The fake's copies of `drawPixel` and `rotateCoordinates` are justified, because
  no host target compiles `GfxRenderer.cpp`. A comment at the real `drawPixel` names both copies.
  Their cited line numbers (`:561-600`, `:283-313`, `:1687-1703`) are correct at HEAD.
- **Naming and structure.** Consistent with `CoverBand` and `Masthead`.
- **Comments.** Each one gives a reason: the epoch ordering, why the deleter exists, why the fake
  panel is 800 × 480, why `finishBand` masks the corners. None restates its next line.
  `CoverBandCache::clear()` is a documented test seam, and the linker drops it from firmware.
- **Error handling.** It matches the repo's "LOG_ERR + fallback" shape. OOM and over-cap log and
  draw without recording, and no path refuses to paint.
- **Resource rules.**
  - No bare `new` and no `std::function`.
  - Stack use is a handful of scalars.
  - Timings are `LOG_DBG`.
- **Static analysis.** `pio check` fails in CI on this PR's new file (finding 1).

## Findings

1. **MAJOR — The PR turns CI's `cppcheck` job red. The PR body calls that clean.**
   - CI runs `pio check --fail-on-defect low --fail-on-defect medium --fail-on-defect high`. In run
     38157227448 (job 114522600168), the job fails with exactly three defects, all in this PR's new
     file:
     - `src/components/CoverBandCache.cpp:40: [low:style] Consider using std::find_if algorithm instead of a raw loop. [useStlAlgorithm]`
     - `src/components/CoverBandCache.cpp:108: … std::accumulate …`
     - `src/components/CoverBandCache.cpp:114: … std::generate …`
   - `main` is green at `9d6f0c0d`.
   - The PR body's "Verified here" says "no defects in touched files", then names these three as
     "left as designed". They are defects at the level CI fails on, and `CLAUDE.md` says to fix CI
     failures before review.
   - **Fix inline:**
     - Use `std::find_if` in `find`, `std::accumulate` in `bytesHeld`, and `std::generate` or an
       inline `// cppcheck-suppress useStlAlgorithm` in `clear`. `--inline-suppr` is on
       (`platformio.ini:24`), and there is precedent at `src/util/ButtonNavigator.h:20`.
     - Correct the PR body's `pio check` bullet.
   - This is mechanical. It reverses no spec decision and needs no owner judgment.

2. **MINOR — The LRU test cannot tell least-recently-used from FIFO or from a fixed victim.**
   - `TheLeastRecentlyUsedBandIsEvictedAndThePsramHeldIsBounded`
     (`test/cover_band/CoverBandReplayTest.cpp:253-264`) passes 14/14 under two mutations:
     - `find` no longer refreshing `lastUse` (`CoverBandCache.cpp:41`), which is FIFO;
     - `victim` always returning slot 0 once every slot is live (removing the comparison at
       `CoverBandCache.cpp:52`).
   - The only refresh it performs is the replay of `band(4)` at `:261`, and that band is already
     the most recent. Spec §11 case 8 and A8 promise LRU, which matters for Home → Go to → Home.
   - **Fix inline:** after the five draws, replay `band(1)`, the oldest live band, and draw a new
     sixth band. Then assert that `band(1)`, `band(3)` and `band(4)` replay, and that `band(2)` is
     drawn, in that order, with the `Drawn` assertion last. FIFO and the fixed victim each fail it.

3. **MINOR — The off-screen fallback test does not exercise the guard it is there for.**
   - `ABandReachingOffScreenReplaysThroughDrawPixel` (`CoverBandReplayTest.cpp:185-187`) moves the
     band off screen in x only (`Rect{-4, …}`).
   - In x, the fast path's own row clip already drops the out-of-range columns. Their `rowY` falls
     outside `[0, rows)` at `CoverBand.cpp:103`. So removing `insideScreen` from `CoverBand.cpp:95`
     still passes all 14 tests.
   - The guard is load-bearing only in y. There, `band.y >> 3` at `:104` would write before the row
     when y < 0, and past the row's end when the band reaches below the screen.
   - No caller paints such a band today, so this is a test gap, not a live bug.
   - **Fix inline:** add a case that overruns the bottom edge, e.g. `Rect{8, 700, 464, 120}` in
     Portrait. A band at y < 0 would make the unguarded build write outside the vector, which is UB.
     The bottom-edge case avoids that and still fails without the guard.

No BLOCKER. Finding 1 must be fixed before merge, but it is a mechanical rewrite plus a corrected
PR-body line. Findings 2 and 3 tighten two tests. The design, the replay arithmetic, the
invalidation sites and the pixel-identity proof are sound.

VERDICT: CLEAR
