Tier: standard

# Issue #237 plan review, pass 0

Plan: `docs/superpowers/plans/2026-09-30-issue-237-plan.md`
Spec: `docs/superpowers/specs/2026-09-30-issue-237-design.md`

## Summary

The plan is sound and can be executed as written. Every spec requirement maps to a step:

- A3 header→button gap: Task 1.
- A2, A4 and A5 top gap, thumbnail key and fallback header: Task 2.
- A6–A10 measured strip cell, floor, centring and today box: Task 3.
- Measurement plus `LOG_DBG`: 3.3.
- Format, host suite and firmware build: 3.4 and Task 4.
- The PR wording for decision d1: "PR description notes".

Every quoted "replace" snippet matches the current source byte for byte:

- `HomeLayout.h:32`, `:93-95`, `:97-98`, `:100`, `:109`, `:117` and `:137`.
- `LauncherActivity.cpp:95-97`, `:278`, `:286-287` and `:359-364`.
- `HomeLayoutTest.cpp:23`, `:38`, `:50-51`, `:55-56`, `:92` and `:111-112`.

The names and signatures agree between spec and plan, and from task to task:

- `TextWidths{digit, twoDigits}`
- `stripCellWidth(const TextWidths&)`
- `STRIP_CELL_MIN`
- `Layout::stripCell` and `Layout::fallbackHeader`
- `stripDay(const Layout&, int, int)`
- `stripHighlight(const Box&)`
- `compute(..., lines, widths)`

`compute` has exactly one production caller (`LauncherActivity.cpp:95`) and one test caller (`HomeLayoutTest.cpp:23`), and the plan updates both. `HomeTargetsTest.cpp` and `HomeVerseCacheTest.cpp` do not call it.

I checked the arithmetic by hand against the code:

| Quantity | Result |
|---|---|
| `band()` (`MastheadLayout.h:22-27`) | y = 9 + 8 + 5 = 22; x and width unchanged, so the thumbnail key stays 773 |
| Hero height | 258 − 8 = 250 (Lyra) and 272 (Base) |
| Plate | 93 + 8 = 101; `bottomOf(buttonRow) + PAD == bottomOf(plate)` still holds (plate.y + 53 + 40 + 8 = plate.y + 101) |
| Strip | 34 × 7 = 238 |
| Title | 464 − 8 − 238 − 8 − 56 = 162 |
| Number-ink gap | (34 + 5) − 28 = 11 ≥ 11 |
| Today box | ends at cell + 33, and the neighbour's ink starts at cell + 39 |

Each task starts red, either through an updated pin or through a compile error on a symbol that does not exist yet. Each commit leaves the tree building: Task 3 changes `compute`'s signature in 3.2 and fixes its only caller in 3.3, before the 3.5 commit.

`drawHero` uses `metrics` only at `:286-287`, so the plan is right to delete it at `:278`. `toRect` is already in scope (`LauncherActivity.cpp:56`). `LOG_DBG` compiles out at `LOG_LEVEL=0`, but `widths` is still passed to `compute`, so the release build gets no unused-variable warning.

The `FILES:` line is at column 0 and outside any fence (`plan:7`). It names all three touched files as repo-relative paths. No other file is touched: the test target is already registered (`test/CMakeLists.txt`, `add_subdirectory(home_layout)`), and `test/home_layout/CMakeLists.txt` already builds `HomeLayoutTest`.

No BLOCKER or MAJOR. The three MINORs below can be fixed inline.

## Findings

### MINOR 1: the 3.4 warning check fails open

- **Claim.** 3.4 says the build has "no new warning from `HomeLayout.h` or `LauncherActivity.cpp`" and proves it with `pio run -e x4pro 2>&1 | grep -E "warning:.*(HomeLayout|LauncherActivity)"` printing nothing.
- **Problem.** The plan itself says `pio` is not on PATH (`plan:20`). The check command calls bare `pio` (`plan:504`), which fails with "command not found", and `grep` then prints nothing: a false pass. Even with the full path, the check is a second, incremental build straight after the first (`plan:501`). It recompiles nothing, so it cannot print a warning for files already compiled. That also breaks the repo rule against repeating a target that already passed.
- **Evidence.** `plan:20`, `plan:501`, `plan:504`.
- **Fix.** Capture the single build's output and check that: `~/.platformio/penv/bin/pio run -e x4pro 2>&1 | tee "$TMPDIR/pio-237.log"`, then `grep -E "warning:.*(HomeLayout|LauncherActivity)" "$TMPDIR/pio-237.log"`. Also confirm the log contains `SUCCESS`.

### MINOR 2: some new comments restate the code

- **Claim.** The code-style section says "no comments that restate the code" (`plan:15`).
- **Problem.** Some of the snippets the plan prescribes do exactly that:
  - `// Header, its rule, a gap, the button row, and the foot pad.` paraphrases the expression on the next line (`plan:81`).
  - `// One day's width in the strip.` restates the name `stripCell` (`plan:393`).

  The others are worth keeping, because each gives a reason:
  - The `STRIP_CELL_MIN` fallback comment (`plan:368`).
  - The `twoDigits` note that digits are tabular (`plan:378`).
  - The fallback-header note that it is PAD lower (`plan:211`).
  - The `stripHighlight` comment (`plan:441`).
- **Evidence.** `plan:81`, `plan:393`. See also the user's global code-style rule and CLAUDE.md "Comments".
- **Fix.** Drop the comments at `plan:81` and `plan:393`.

### MINOR 3: `STRIP_DAY_HEIGHT` is a bare `23 + 24`

- **Claim.** The test helper hardcodes `constexpr int STRIP_DAY_HEIGHT = 23 + 24;` (`plan:278`).
- **Problem.** These are the `LINES.small` and `LINES.ui10` values already defined in the test (`HomeLayoutTest.cpp:20`). This mirrors `drawMeetings`' `letterHeight + dayHeight` (`LauncherActivity.cpp:355-356`). If `LINES` changes, the helper silently drifts from it.
- **Evidence.** `plan:278`, `HomeLayoutTest.cpp:20`.
- **Fix.** Use `constexpr int STRIP_DAY_HEIGHT = LINES.small + LINES.ui10;`.

## Observations (no action required)

- `TheGapsLeaveTheSectionsBelowTheHeroInPlace` (`plan:160-165`) and `TheTodayBoxClearsItsNeighbours` (`plan:329-343`) already pass against today's geometry.
  - At a 26 px pitch the box ends at cell + 25 and the neighbour's ink starts at cell + 27.
  - Both are regression pins for invariants the spec asks to keep, not red tests for new behaviour. Each task still has a genuine red, so this is fine.
- The test's `numberInkLeft` ignores the glyph's 1 px left bearing, which `drawText` applies. The offset is the same in every cell, so the gap and clearance results are exact.

VERDICT: CLEAR
