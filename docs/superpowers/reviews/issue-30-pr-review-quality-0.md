# PR review — issue #30, PR #44, stage 2 (code quality), pass 0

**Branch:** `fix/30-launcher-wake-refresh`
**Reviewed:** `gh pr diff 44`, code files only — `src/activities/ActivityManager.cpp`,
`src/activities/launcher/LauncherActivity.{h,cpp}`,
`src/activities/launcher/LauncherRefresh.h`, `test/CMakeLists.txt`,
`test/launcher_refresh/{CMakeLists.txt,LauncherRefreshTest.cpp}`.
**Not reviewed:** intent, scope and completeness — stage 1 covered those
(`docs/superpowers/reviews/issue-30-pr-review-intent-0.md`, `VERDICT: CLEAR`).

## What was checked and what it was checked against

- **Pattern mirroring.** `src/activities/home/HomeActivity.{h,cpp}` — the
  reference implementation this change was modelled on.
- **Host-tested helper beside an activity.** `src/activities/reader/ReturnStack.h`
  with `test/return_stack/`; free functions beside an activity,
  `src/activities/reader/ReaderUtils.h`.
- **Refresh-mode plumbing.** Every `HALF_REFRESH` call site in `src/` and
  `lib/hal/`, plus the sibling `goToReader(path, allowFastInitialRefresh)` chain.
- **Build and suite.** `cmake -S test -B build/test && cmake --build build/test`,
  then `ctest -R LauncherRefresh` — 4/4 pass.
- **Formatting.** `.venv/bin/clang-format` 21.1.8, `-style=file`, against each of
  the five source files: all five are byte-identical to the formatter's output.

## Findings

### MINOR 1 — one "why" sentence is duplicated near-verbatim across two files

`src/activities/launcher/LauncherRefresh.h:5-7`:

```
// cleanInitialRefresh is set by the wake path when the panel is still showing a
// frame the launcher did not draw: the sleep screen, after a wake that found no
// Quick Resume frame on the card.
```

`src/activities/launcher/LauncherActivity.h:94-96`:

```
// Set by the wake path when the panel is still showing a frame this activity
// did not draw: the sleep screen, after a wake with no Quick Resume frame.
// Consumed by the first paint only -- see LauncherRefresh.h.
```

The first two lines of the member comment restate the header comment with two
words changed. `test/launcher_refresh/LauncherRefreshTest.cpp:5-8` states the
same rationale a third time in its own words.

The sibling precedent is two statements, not three:
`src/activities/reader/ReturnStack.h:3-8` carries the rationale once, and
`test/return_stack/ReturnStackTest.cpp:5-7` compresses it once for the suite.
There is no third copy on `ReturnStack`'s members.

Only the third line of the member comment — "Consumed by the first paint only --
see LauncherRefresh.h" — is load-bearing where it sits; it is the cross-reference
a reader of the header needs. The first two lines are the duplicate, and they are
the ones that will drift if the wake path in `src/main.cpp:522-542` is ever
reworked. `CLAUDE.md`'s comment rule ("Keep only non-obvious mechanism, field or
parameter meaning, or the reason a special case exists") is satisfied by keeping
the cross-reference and dropping the restatement.

Suggested inline fix: reduce `LauncherActivity.h:94-96` to the cross-reference,
e.g. `// Wake-path flag, consumed by the first paint only -- see LauncherRefresh.h.`
Leave `LauncherRefresh.h` and the test comment as they are; those two mirror
`ReturnStack` exactly.

### MINOR 2 — `ResolvesAtCompileTime` registers a ctest case that cannot fail

`test/launcher_refresh/LauncherRefreshTest.cpp:22-27`:

```cpp
TEST(LauncherRefresh, ResolvesAtCompileTime) {
  static_assert(launcherNeedsCleanPaint(true, false));
  static_assert(!launcherNeedsCleanPaint(false, false));
  SUCCEED();
}
```

`launcherNeedsCleanPaint` is not a template, so both `static_assert`s are
evaluated when `LauncherRefreshTest.cpp` is compiled. If either failed, the build
would fail and ctest would never run. The registered case is therefore an
always-pass entry: `gtest_discover_tests` adds it to the suite count, `ctest`
executes it, and the only assertion it can report is `SUCCEED()`.

`SUCCEED()` itself has precedent here (`test/release_json_parser/ReleaseJsonParserTest.cpp:409`,
`test/streaming_json_parser/StreamingJsonParserTest.cpp:367`), but in both of
those the body does real runtime work first and `SUCCEED()` only marks the
no-`EXPECT` exit. This body does none.

Moving the two `static_assert`s to namespace scope in the same file keeps exactly
the same compile-time guarantee, keeps them visible next to the runtime truth
table, and stops the suite reporting a fourth case that carries no coverage.

This is optional. The `TEST` wrapper does make the constexpr pin discoverable in
ctest output, which is a defensible reason to leave it; the finding is that the
cost — a test result that is a constant — should be a deliberate choice rather
than an accident.

## What was checked and found sound

Recording these because each was a plausible place for this change to go wrong.

- **The pattern is mirrored, not reinvented.** `LauncherActivity.h:25-26` matches
  `HomeActivity.h:70-73` field for field: a defaulted trailing `bool
  cleanInitialRefresh = false` constructor parameter, a `const bool` member of the
  same name, and a separate `bool firstRenderDone = false` latch. The names are the
  ones already in the codebase (`ActivityManager.h:93`, `HomeActivity.h:31`), not
  new ones. `ActivityManager.cpp:254` mirrors the existing
  `goToReader(path, allowFastInitialRefresh)` chain at `ActivityManager.cpp:214-230`.
- **The refresh-mode expression matches its sibling.**
  `LauncherActivity.cpp:475-476` resolves to the same
  `HALF_REFRESH : FAST_REFRESH` choice `HomeActivity.cpp:306` makes, and
  `HALF_REFRESH` is the mode every other "clear what is physically on the panel"
  call site uses (`main.cpp:536`, `SleepActivity.cpp:655,861,867`,
  `EpubReaderActivity.cpp:1443`). No new refresh idiom was introduced.
- **The helper is not a duplicate of an existing one.** A repo-wide grep for
  `cleanInitialRefresh` / `allowFastInitialRefresh` / `needsWakeRefresh` finds no
  existing shared predicate; the only other expression of this decision is the
  inline one at `HomeActivity.cpp:306`, which belongs to the dead reference
  implementation owned by issue #29 and is correctly left alone.
- **The test suite's CMake is a copy of the sibling's, not a new shape.**
  `test/launcher_refresh/CMakeLists.txt:6-19` is `test/return_stack/CMakeLists.txt`
  with the target renamed — same single `${REPO_ROOT}/src` include directory, same
  `crosspoint_test_common` + `GTest::gtest_main` link, same
  `gtest_discover_tests`. Its header comment explains the one thing that is not
  obvious (why no `test/stubs` entry is needed), which is the right use of a
  comment here.
- **The test names the risk it exists to catch.** The four runtime cases are the
  complete truth table of a two-`bool` predicate, and
  `LauncherRefreshTest.cpp:10-13` says which of the four is the one a caller can
  silently invert. That is a designed suite, not coverage theatre — and the spec
  records that this exact polarity bug was found once already during review
  (`docs/superpowers/specs/2026-09-16-issue-30-design.md:199`).
- **`(void)cleanInitialRefresh;` was removed, not left behind.**
  `ActivityManager.cpp:250-253` keeps `(void)initialMenuItem;` — still required —
  and the reworded comment's factual claim is accurate: every `onGoHome()` call
  site in `src/` is argument-less, and both `goHome()` calls in
  `ActivityManager.cpp:81,114` and `main.cpp:566,571` pass `HomeMenuItem::NONE` or
  nothing. No dead code, no commented-out code, no stale `(void)` cast.
- **`#include <HalDisplay.h>` at `LauncherActivity.cpp:8`** replaces a transitive
  dependency through `GfxRenderer.h` with a direct one, matching `HomeActivity.cpp:7`.
  Correct direction, and it sits in the right block of the file's existing
  include ordering.
- **Include spelling.** `"activities/launcher/LauncherRefresh.h"` at
  `LauncherActivity.cpp:24` is the fully-qualified form while line 1 uses the bare
  same-directory form. Both spellings exist in `src/activities/` already
  (`activities/network/MeetingDownloadActivity.h` is included by full path from
  within `src/activities/network/`), so this is not an inconsistency worth a
  finding.
- **Comment dash style.** The new comments use ASCII `--`, which is what the tree
  uses (159 occurrences in `src/`, zero em-dashes). Consistent.
- **No resource-protocol exposure.** The change adds two `bool` members and one
  `constexpr` predicate; no allocation, no string, no stack growth, nothing that
  touches the storage or ISR rules.

## Points of contact with ratified decisions

Neither finding contradicts the spec or plan. Two things that look like findings
were checked against them and are not:

- **The per-render `LOG_DBG` at `LauncherActivity.cpp:477-478`** fires on every
  launcher paint, including every selection move, to report a one-shot decision,
  and its `mode=` field is derivable from the two fields beside it. This is
  ratified as A8 (`docs/superpowers/specs/2026-09-16-issue-30-design.md:154-164`)
  with a sound mechanism: `LOG_DBG` compiles out at `LOG_LEVEL=1`
  (`lib/Logging/Logging.h:57-60`), which is the release env
  (`platformio.ini:188`), so the shipped build pays nothing, and on a default
  unit there is no optical difference for the human tester to observe. The
  redundancy is what makes the polarity readable in the log. Not raised.
- **The helper returning `bool` rather than `HalDisplay::RefreshMode`**, which
  leaves the `? HALF : FAST` mapping at the call site and untested, is ratified as
  A10 (`design.md:186-196`) with the cost recorded explicitly. Not raised.

## Verification performed

```
cmake -S test -B build/test && cmake --build build/test
ctest --test-dir build/test --output-on-failure -j -R LauncherRefresh
  4/4 passed (FirstPaintOfAFlaggedEntryNeedsACleanPaint, LaterPaintsOfAFlaggedEntryDoNot,
  AnUnflaggedEntryNeverDoes, ResolvesAtCompileTime)

.venv/bin/clang-format --version -> 21.1.8
clang-format -style=file <file> | diff - <file>  -> no diff, all five source files
```

`GfxRenderer::displayBuffer(HalDisplay::RefreshMode)` (`lib/GfxRenderer/GfxRenderer.h:189`)
confirms `const auto mode` at `LauncherActivity.cpp:476` deduces the parameter
type exactly; no firmware build was needed to settle a finding.

`git status --short` is clean; `build/` is gitignored.

## Verdict

Two MINORs, both fixable inline, neither reversing a decision or needing a human
judgment. The change mirrors its reference implementation closely, introduces no
second way of doing something the repo already does, adds no dead code, and its
test is designed around the specific failure it is exposed to.

VERDICT: CLEAR
BLOCKERS: 0
MAJORS: 0
