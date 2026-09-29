Tier: standard

# Issue #187 — plan review 0

Plan: `docs/superpowers/plans/2026-09-29-issue-187-plan.md`
Spec: `docs/superpowers/specs/2026-09-29-issue-187-design.md`
Base checked: `da2831c3` (code unchanged from `a4e2288d`).

## What was verified

- **Spec coverage.** Every spec item maps to a step:
  - Architecture 1 (the header, `bookFor` / `classify` / `chapterRowFor`) → Step 2a.
  - Architecture 2 (constructor, `entrySpine`, `onEnter` → `enterAtPosition`, class comment) → Step 3a/3b.
  - Architecture 3 (the reader call site) → Step 3c.
  - Testing cases 1–11 → the 15 `TEST`s in Step 1a. Registration → 1b. Gates → 3d and 5.
  - Documentation (the §7 paragraph and the "scrolling list" fix) → Step 4.
  - The AC4 SD-cost note → the PR checklist.
- **Signatures stay consistent.** The header in 2a matches the spec's declarations (spec `:195-201`). The test calls `classify(const int16_t*, const bool*, int, int)`, `bookFor` and `chapterRowFor` with the same shapes. `enterAtPosition` in 3b passes the real member types: `int16_t bookTargetSpine[]` and `bool bookIsDirect[]` (`BibleNavigationActivity.h:53,56`), and `int16_t chapterSpine[]` (`:78`). The log strings match the spec's error table (`:297-298`).
- **The fixture is internally consistent.** Checked by script:
  - 66 targets and 66 counts, summing to 1,189;
  - the single-chapter indices are exactly `{30, 56, 62, 63, 64}`;
  - every book `i` satisfies `BOOK_TARGET[i] + CHAPTERS[i] + 2 == BOOK_TARGET[i+1]` (one outline page between books), so no range overlaps;
  - the spot values hold: Gen 32 → 61, Psalm 119 → 662, Psalm 150 → 693, Rev 22 → 1343, Jonah's outline → 978.
- **The expected outcomes follow from the header as written.**
  - 978 resolves to Obadiah (977) and is direct → `None`.
  - 80 → Genesis, `LoadChapters`, row -1.
  - 1344 and 3940 → Revelation, row -1.
  - The tie and unsorted cases pick index 0 and index 1 as asserted.
  - There are 15 `TEST`s, matching "15 in all" (plan `:341`).
- **Runtime reasoning.**
  - `enterLevel` clamps and places under `RenderLock` (`BibleNavigationActivity.cpp:188-204`).
  - Back from Chapter goes to `enterLevel(Level::Book, selectedBook)` (`:353-354`).
  - Member order `epub` then `entrySpine` matches the initialiser order, so there is no `-Wreorder`.
  - `grep` finds the reader as the only construction site (`EpubReaderActivity.cpp:729`).
- **FILES lines** (plan `:6-8`) are at column 0, outside any fence, as repo-relative paths. They cover
  every file a step edits: the new header, the test, `test/number_grid/CMakeLists.txt`, the
  navigator `.h`/`.cpp`, `EpubReaderActivity.cpp` and `USER_GUIDE.md`.
- `test/number_grid` is already registered (`test/CMakeLists.txt:101`), and its CMake block shape
  matches the existing `BookGridLayoutTest` block (`test/number_grid/CMakeLists.txt:16-29`).
- The branch has an upstream (`origin/feature/187-select-chapter-current-book`), so the bare
  `git push` in Step 5 works.

## Findings

### MAJOR 1 — Step 5 runs the full host suite over a build tree that only built one target

- **Claim.** Step 5 runs `ctest --test-dir build/test --output-on-failure -j` (plan `:519`), and says
  "The full host suite must pass" (`:523`).
- **Problem.** The only builds before that line are `--target BibleEntryPositionTest` (Step 1c
  `:258`, Step 2b `:337`). In a fresh worktree, no other executable under `build/test` exists.
  `gtest_discover_tests` registers a failing `<target>_NOT_BUILT` placeholder for every
  discovered-but-unbuilt executable. So the literal command reports failures for every other suite,
  and the gate cannot pass as written. An implementer following the plan will either chase phantom
  failures or wave the gate through.
- **Evidence.**
  - Plan `:258`, `:337` (targeted builds only), `:519` (full ctest).
  - The spec's own gate builds everything first: `cmake --build build/test && ctest …` (spec `:347`).
  - `bin/bootstrap` builds no tests (it only initialises submodules and clang-format).
- **Fix (inline).** In Step 5, run `cmake --build build/test -j` immediately before the `ctest` line.
  This is mechanical and changes no decision.

### MINOR 1 — the include-order instruction is backwards

- **Claim.** "Add `#include "BibleEntryPosition.h"` … alphabetically before
  `#include "BibleBookNameTable.h"`" (plan `:391`).
- **Problem.** "BibleB…" sorts before "BibleE…", so the new include belongs *after*
  `BibleBookNameTable.h` (`BibleNavigationActivity.h:10`). With `SortIncludes` and
  `IncludeBlocks: Regroup` (`.clang-format:152,269`), clang-format will move it anyway, so this only
  creates a spurious formatting diff.
- **Fix (inline).** Say "between `BibleBookNameTable.h` and `BookGridLayout.h`".
  - Optionally, include it from `BibleNavigationActivity.cpp` rather than the header, since nothing
    in the header uses `BibleEntry` types.

### MINOR 2 — Step 3 has no failing test, and the plan does not say why

- **Claim.** Step 3 changes behaviour, the navigator's entry level, and verifies it only by
  `pio run` (plan `:465-472`).
- **Problem.** The brief expects each step to open with a failing test. Step 3's logic is thin glue
  over the Step 1/2-tested helper, and the activity depends on FreeInkUI, `Epub` and `HalStorage`,
  which no host suite links. That is a valid reason, but the plan leaves it unstated. A literal
  implementer may either stall or invent an unbuildable test.
- **Fix (inline).** Add one line to Step 3: "No host test: the activity is not host-linkable. The
  decision logic is covered by Step 1. This step is verified by the build and by device checks 1–6."

## Assessment

The plan is concrete and executable: full code for every edit, exact anchors, exact commands, and
commits that each leave a building tree (Steps 1–2 commit together, deliberately, after green). It
matches the spec on every requirement and decision. The one MAJOR is a missing build command in the
final gate. It is mechanical to fix, reverses no decision and does not change scope.

VERDICT: CLEAR
