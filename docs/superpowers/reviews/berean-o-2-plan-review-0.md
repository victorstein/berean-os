Tier: standard

# berean-o-2 plan review, pass 0

Reviewed: `docs/superpowers/plans/2026-10-11-berean-o-2-plan.md` (at `15a8ea7c`), against
`docs/superpowers/specs/2026-10-11-berean-o-2-design.md` and its review
`docs/superpowers/reviews/berean-o-2-spec-review-1.md`.

## How this was checked

I executed the plan literally, in an isolated copy of this tree with its own git repository
(scratchpad, not this worktree). Every code block was extracted from the plan verbatim, and every
`old` → `new` edit was applied with an exact-match replace that fails unless `old` occurs exactly
once. Each task was committed with the plan's own `git add` list.

- Every `old` snippet matched exactly once. After each task's `git add` and commit, `git status`
  was clean, so the add lists are complete.
- Task 1: Step 1.3 fails with `'components/BandInk.h' file not found`; Step 1.5 passes 6 tests.
- Task 2: Step 2.2 fails with an undefined `HalFile::seekCur(long long)`; Step 2.4 passes 69 tests.
- Task 3: `CoverBandReplayTest` passes 14, `BandInkTest` 6 and `CoverBandGeometryTest` 16. Step 3.9's
  mutation (`&=` → `=`) gives `4 FAILED TESTS`, and the restore gives 14 passed. The only host
  warning is the expected `unused variable 'MODULE'` in `CoverBandCache.cpp`.
- Step 3.1's line numbers hold: `drawPixel` 561–600, `rotateCoordinates` 283–313, the strip pair
  1687–1703.
- Task 5.1: the whole host suite builds and passes **1,574/1,574**, as the plan states.
- Task 5.2: the whole-tree `./bin/clang-format-fix` (clang-format 21.1.8) changes none of the
  plan's files.
- Firmware: I applied the full Tasks 1–4 patch to this worktree, ran `pio run -e x4pro`, and
  reversed the patch with `git apply -R`. Afterwards, `git status --short` shows only this review.
  - **`x4pro SUCCESS`**: RAM 20.2%, flash 84.6% (5,544,190 B).
  - All nine touched firmware units compiled with **no warnings**: `CoverBand`, `CoverBandCache`,
    `Masthead`, `LauncherActivity`, `CoverThumb`, `BookCacheUtils`, `CardBooks`,
    `ClearCacheActivity` and `EpubReaderActivity`.
  - The first attempt failed on missing framework headers (`esp8266-compat.h` in `Arduino.h`), and
    only in untouched files. A rerun with no change succeeded. I put that down to the shared
    `~/.platformio` package changing mid-build, not to the plan.

Verdict in brief: the design is carried over faithfully, and the code works. Two problems in
Task 3 need fixing inline:

- one test does not test what it says (MAJOR 1);
- two steps do not run as written (MAJOR 2).

## Findings

### MAJOR 1: the replay test never draws a Home-sized band; its 272-row branch is dead code

- **Claim.** `AReplayLeavesTheFrameAFreshDrawLeavesForEveryStyleAndAlignment` is the plan's version
  of spec §11 case 1, the G3 proof. It loops over the masthead, Home and Meetings styles, and it
  gives the Home style the Home hero's height of 272 rows (plan `:797-801`).
- **Problem.** `for (const CoverBand::Style& style : {MASTHEAD, HOME, MEETINGS})` iterates over a
  `std::initializer_list` whose backing array holds **copies** of the three globals. `style` never
  aliases `HOME`, so `&style == &HOME` (plan `:800`) is always false and every band is 120 rows. The
  Home style is only ever replayed on a 120-row band: a 90-row plate over 30 rows of art.
  
  So the G3 comparison never covers the band that motivates the design:
  - the Home hero, with a 34–35-byte stride (spec §6, 16,240 B);
  - the shape on which G2 is judged ("misses G2 on Home", spec §4);
  - the shape §11 sizes in `BandInkTest`.
  
  The test reads as if it covers Home, and it passes, so nobody would notice.
- **Evidence.** In the scratch run, I added a `printf` of the computed height inside the loop.
  `--gtest_filter='*EveryStyleAndAlignment*'` then printed `height 120` 24 times: 16 for the two
  46-plate styles and 8 for `r8 plate90`. It never printed 272.
- **Fix.** Iterate over addresses, so the identity test means what it says:
  
  ```cpp
  for (const CoverBand::Style* style : {&MASTHEAD, &HOME, &MEETINGS}) {
    for (int lead = 0; lead < 8; ++lead) {
      CoverBandCache::getInstance().clear();
      const int height = style == &HOME ? 272 : 120;
      expectReplayMatchesFreshDraw(COVER, Rect{8, 16 + lead, 464, height}, *style);
    }
  }
  ```
  
  I ran this in the scratch copy. It passes, with 8 of the 24 bands now 272 rows. With Step 3.9's
  `&=` → `=` mutation, the same 4 tests still fail, so Step 3.9's expected `4 FAILED TESTS` holds.

### MAJOR 2: Steps 3.4 and 3.8 do not run as written; the red step fails in CMake, and the green step then cannot find its target

- **Claim.** Step 3.4 (plan `:1000-1007`) runs
  `cmake -S test -B build/test > /dev/null && cmake --build build/test --target CoverBandReplayTest -j 8`.
  It expects "compile errors. `components/CoverBandCache.h` is not found, and `CoverBand::Result`
  is undeclared". Step 3.8 (plan `:1496-1503`) then builds the three targets with `cmake --build`
  alone.
- **Problem.**
  - Step 3.3 has already listed `${REPO_ROOT}/src/components/CoverBandCache.cpp` as a source of
    `CoverBandReplayTest` (plan `:976`), and that file does not exist until Step 3.5. CMake's
    generate step therefore fails before anything compiles:
    `Cannot find source file: …/src/components/CoverBandCache.cpp`, then
    `No SOURCES given to target: CoverBandReplayTest` and `CMake Generate step failed`. The `&&`
    means the build never runs. That is a legitimate red, but it is not the one the plan predicts.
  - The failed generate leaves the Makefile tree without a `CoverBandReplayTest` target. Step 3.8
    does not reconfigure, so its first command stops with
    `make: *** No rule to make target 'CoverBandReplayTest'.  Stop.`
  
  An implementer following the plan literally first sees a red that does not match its
  description, then a green step that errors out. A capable implementer recovers by
  reconfiguring, but the plan should not depend on that.
- **Evidence.** I reproduced both, in order, in the scratch copy. The worktree's own build tree uses
  the same generator: `build/test/CMakeCache.txt` has `CMAKE_GENERATOR:INTERNAL=Unix Makefiles`.
  After a manual `cmake -S test -B build/test`, Step 3.8 builds and passes 14/6/16.
- **Fix.**
  - Step 3.4: change the expected output to "CMake stops at generate:
    `Cannot find source file: …/src/components/CoverBandCache.cpp` and
    `CMake Generate step failed`."
  - Step 3.8: make the first command
    `cmake -S test -B build/test > /dev/null && cmake --build build/test --target CoverBandReplayTest BandInkTest CoverBandGeometryTest -j 8`.

### MINOR 3: two expected outputs differ from what the commands print

- **Task 0, plan `:94`.** The plan expects `clang-format 21 already present in .venv/bin`. This
  worktree's `.venv/bin` is empty (`ls -la .venv/bin`). So `bin/bootstrap` takes its link branch
  (`bin/bootstrap:34-42`) and prints `Linking clang-format from the primary checkout (…)...`, then
  `Done: clang-format version 21.1.8` (`:57`). The "already present" line (`:29`) appears only on a rerun.
  **Fix:** accept either line.
- **Step 3.1, plan `:405-408`.** The pattern `^void GfxRenderer::drawPixel` also matches the five
  `drawPixelDither<…>` specialisations (`GfxRenderer.cpp:946-966`, 948–968 after the edit). The grep
  prints nine lines, not the four the plan lists. The four it expects are correct.
  **Fix:** anchor it as `^void GfxRenderer::drawPixel(`, or say that the `drawPixelDither` lines
  are expected too.

## Checked and sound

- **Every spec requirement maps to a step.**

  | Spec | Where the plan meets it |
  |---|---|
  | G1, §8 Home log | Step 3.7 |
  | G2, A10, A11, §6, §7 replay | Steps 1.4, 3.6 |
  | G3, §11 | Steps 1.1, 3.3; see MAJOR 1 |
  | G4, A12, A13, §10 | Step 3.5 `reserve`, `commit`, `bytesHeld` |
  | A1, A8, A14 key, LRU, path string | Step 3.5 |
  | A3, A4 bump sites, after the change | Task 4 |
  | A15 `Result` | Steps 3.6, 3.7 |
  | §7 drift-guard comment (review 1, MINOR 7) | Step 3.1 |
  | `seekCur` | Task 2 |
  | §12 device steps | Task 6 PR body |

  Every §11 case 1–12 has a test:
  - case 4 is split in two, `PortraitInverted` and `x = -4`;
  - case 1's `band.y - 1` assertion is its own test.

  The plan departs from spec §14 by adding `GfxRendererFake.h` and a `HalStorageFakeTest` case.
  Both are additive, and both are on the `FILES:` lines.
- **`FILES:` lines.** Plan `:41-44` are at column 0 and outside every fence. They hold repo-relative
  paths and one prefix, `test/cover_band/`, with no globs. I diffed every path the applied patch
  touches against them. All 22 are covered, and no entry goes unused.
- **Names and signatures stay consistent from step to step.** That covers:
  - `BandInk::{Layout, layoutFor(y, w, h), bytesFor, byteIndex, bitMask, set, test}`;
  - `CoverBand::{Result, resultName, draw, noteCoversChanged}`;
  - `CoverBandCache::{getInstance, find, reserve, commit, abandon, bytesHeld, clear, SLOTS, MAX_SLOT_BYTES}`;
  - `gfx_fake::*`, `heap_caps_stub::failAll` and `storage_fake::*`.
  
  The build proves it.
  
  `layoutFor(y, width, height)` instead of the spec's `layoutFor(band)` keeps `BandInk.h` free of
  `BaseTheme.h`, so it stays pure, as §6 asks.
- **The fast path is formula-identical to `drawPixel`.**
  - Row: `panelHeight - 1 - (x + c) - originY` against `GfxRenderer.cpp:566,580-586`.
  - Byte: `(y >> 3) + k`, which is `phyX / 8`, `:590`.
  - Bit: `0x80 >> ((lead + r) & 7)`, which is `7 - phyX % 8`, `:591`.
  - Polarity: an AND with the inverted mask, `:593-594`.
  - Strip clip: one unsigned compare against `getWriteRows()`, `GfxRenderer.h:228-230`.
  
  Bounds hold because `insideScreen` gives `(y + h - 1) >> 3 ≤ 99`.
  
  Recording and the `drawPixel` fallback use the same logical `(column, row)` layout, so a replay is
  correct even under clipping or in another orientation.
- **The cache logic matches spec §7.**
  - The epoch is read in `reserve`, before `blitCropped` opens the file (A4).
  - A stale-epoch slot counts as not live, both in `find` and in `victim` (review 1, MINOR 8).
  - Over the cap and on OOM, `reserve` returns null, logs `LOG_ERR`, and the draw is still `Drawn`.
  - `abandon` on every `Declined` keeps a partial recording from ever going live.
- **The bump sites sit after their change.**
  - `CoverThumb.cpp` bumps after a successful `generateThumbBmp`, `:42-45`.
  - `BookCacheUtils.cpp` bumps after `clearCache()` and skips non-EPUBs, `:24-28`.
  - `CardBooks.cpp` bumps after `removeDir`, `:57`.
  - `ClearCacheActivity.cpp` bumps after its loop and `root.close()`.
  - `EpubReaderActivity.cpp` bumps right after `epub->clearCache()`, `:898`.
- **The only callers of `CoverBand::draw`** are `Masthead.cpp:84` and `LauncherActivity.cpp:248`,
  and both are updated. No host suite compiles any of the changed firmware `.cpp` files, so no
  sibling suite breaks.
- **The Task 4 TDD exception is justified.** The five bump sites live in Epub- and activity-bound
  files that no host target can build. The epoch itself is tested through
  `CoverBand::noteCoversChanged()` in Task 3, and spec §12 step 7 checks the sites on the device.
- **The release build (Step 5.4) is low-risk by construction.** At `LOG_LEVEL=0`, `LOG_ERR` still
  expands (`lib/Logging/Logging.h:45-46`), so `CoverBandCache.cpp`'s `MODULE` stays used. The new
  `startMs` local is `[[maybe_unused]]`, as in `Masthead.cpp:83`. I did not cold-build
  `x4pro-gh_release`.
- **Repo rules.**
  - Explicit PSRAM uses the `PsramFree` idiom, with a comment saying why `makeUniqueNoThrow`
    cannot be used. There is no bare `new`, and no `std::function`.
  - Timings use `LOG_DBG`.
  - Comments explain a why, not the code.
  - `test/CMakeLists.txt` and `src/main.cpp` are untouched.
  - The branch has an upstream, so Task 6's `git push` works.
  - `Claude-Session` trailers and push-in-plan are already established on this branch and in
    earlier plans.

## Verdict

No BLOCKER. Both MAJORs are mechanical and can be fixed inline:

- a loop over addresses (MAJOR 1);
- a corrected expected output, plus a reconfigure before Step 3.8 (MAJOR 2).

Neither reverses a decision, changes scope, or needs the owner's judgment.

VERDICT: CLEAR
