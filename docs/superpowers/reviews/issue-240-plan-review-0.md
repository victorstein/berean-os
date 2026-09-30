Tier: standard

# Issue #240 — plan review 0

Plan: `docs/superpowers/plans/2026-09-30-issue-240-plan.md`
Spec: `docs/superpowers/specs/2026-09-30-issue-240-design.md`
Checked against the tree at `7f66e2b1`.

## Summary

The plan is sound. Every spec requirement maps to a step. The pure layer (Tasks 1–4) is TDD with
tests that fail to compile first, and the expected values check out by hand. Tasks 5–6 give complete,
literal code whose API usage matches the live tree. Every touched file is on a column-0 `FILES:`
line. There are no BLOCKERs and no MAJORs. The three MINORs below can be fixed inline or while
implementing.

## Coverage: spec → plan

| Spec item | Plan step |
|---|---|
| A1 candidate set, zero-drop unless filter, uncapped grid | Task 1 `candidates`; Task 6 `rebuildChips` with `SIZE_MAX` |
| A2 scope passed in, grid recounts; scope computed without lock | Task 6a ctor, 6c `openTagFilter` (callers at `HighlightsActivity.cpp:184`, `:269` hold no lock) |
| A3 one builder, pure half host-tested | Task 1 + Task 5 `TagChipView::buildEntries`; Highlights `rebuildChips` stays in-lock (M1) |
| A4 paging, swipe/hold turns, release steps ±1, indicator only when >1 page | Task 4; Task 6 `handleCustomInput`, `navigateButtons`, `stepSelection`, `turnPage`, `drawFooter` |
| A5 `GRID_MAX_CHIPS = 64`, 24 chips on one page | Task 4 `GRID_MAX_CHIPS`, `NarrowChipsStopAtTheInteractionCap`, `OwnersTwentyFourChipsFitOnePage` |
| A6 `spaceMd`/`spaceLg`, `max(..., minTouchSize)`, `minTouchSize = 0` on props | Task 2 `chipHeight`; Task 5 `metricsFor`, `draw` |
| A7 inverted selection, bold-measured, button-only outline, entry page holds filter | Task 5 `measure`/`draw`; Task 6 `onEnter` (`selectedChipIndex`), `buildScreen` outline, `onRowAction` clears focus |
| A8 no new strings | Task 6 reuses `STR_FILTER_BY_TAG`; Task 5 reuses `STR_TAG_FILTER_ALL`, `STR_TAG_UNLABELLED` |
| A9 existing margins, two-step pagination around `tabBarHeight` strip, `body.width - 2*listInset` | Task 6 `buildScreen` (plan `:1030-1040`), property test `ShorterBodyNeverHasFewerPages` |
| Retire by tag id, `saveDisabled` bail, save-failure message, reset filter to All | Task 6 `onRowLongPress`, `showRetireConfirmation`, `retireTag` |
| `FilterRows` deleted with tests; guard replaced | Task 6d; Task 1 `OnlyTagCandidatesNameAPaletteSlot` |
| Stale 64 → 96 comment; `TagFilterActivity.h:62` comment gone | Task 3b; Task 6a replaces the header |
| Build + full-tree format | Task 5/6 `pio run`; Task 7 `git add` then unsuffixed `clang-format-fix` |

## Checks that found nothing

- **FILES lock.** Plan `:6-10` lists all ten touched files at column 0, outside any fence, as
  repo-relative paths. Every edit in Tasks 1–7 lands in one of them. `TagChipView.cpp` needs no
  CMake or `platformio.ini` change. `test/tag_rows/CMakeLists.txt:1-5` already builds
  `TagChipRowTest.cpp` into `TagRowsTest`.
- **Test arithmetic.**
  - `linesPerPage(600,44,5)` = 605/49 → 12.
  - Three 140 px chips fit 440 (430 ≤ 440), so 24 chips take 8 lines.
  - With 10 px chips and a 5 px gap, 29 fit per line, so the cap binds at 64.
  - A 2000 px chip clamps to 440 and the second chip wraps past `maxLines = 1`, so 2 pages.
  - `ChapterScopedCounts` on `CHAPTER {2,1,0}` gives HOPE 2, MINISTRY 1, NAME 0 and Unlabelled 1,
    so 4 candidates.
  - All of these match the plan's expectations. The fixtures the tests reuse (`HOPE`, `MINISTRY`,
    `NAME`, `PUBLICATION`, `CHAPTER`, `layoutOf`, `GAP`, `toTagId`) exist
    (`TagChipRowTest.cpp:11-19`, `:133-179`). The new helper names do not collide with anything
    already there.
- **Behavioural equivalence of `candidates`.** It stops at the cap before each tag push and before
  Unlabelled, as today's `push`/`break` does (`HighlightsActivity.cpp:137-158`). The Unlabelled chip
  keeps `id = UNLABELLED` from the `ChipEntry` default, so `activateIndex` still returns
  `{UNLABELLED}`.
- **API existence.**
  - The host types and constants exist: `UiAppHost::UiScreen` (`UiAppHost.h:37`), `ACTION_ROW` and
    the virtual `onRowAction` / `navigateButtons` / `onRowLongPress(int)`
    (`UiListActivity.h:27,45,52,55`).
  - The chip helpers exist: `ButtonNavigator` callbacks accept capturing lambdas
    (`ButtonNavigator.h:13-21`), and `fui::button` / `TEXT_ELLIPSIS` are reachable through
    `FreeInkApp.h` → `FreeInkUI.h`.
  - The theme tokens and palette call exist: `spaceLg` / `minTouchSize` (`FreeInkUICore.h:636-637`),
    `TagPalette::isActive` (`TagPalette.h:52`), `stroke(rect, paint, width, radius)`
    (`FreeInkUIGfxRenderer.h:101`).
- **Locking.**
  - `chips_` and `widths_` are written only on the loop task, under `RenderLock`. Page geometry is
    written by the build and read by `turnPage` under the lock. `buttonFocus_` and `filter_` change
    under the lock.
  - `buildEntries` runs outside the lock in `TagFilterActivity::rebuildChips`, and inside it in
    Highlights, as M1 allows.
  - The loop task reads `chips_` without the lock in `onRowLongPress` / `activateIndex`. That is
    safe because the loop task is the only writer.
- **Every task commits a working tree.** After Task 5, the old `TagFilterActivity` still compiles
  against `FilterRows`, which Task 6 deletes. Tasks 5 and 6 each end in `pio run`.
- **No failing test for Tasks 5–6.** This is the spec's own testing boundary: FreeInkUI and
  `STUDY` have no host harness (spec "Testing strategy"). Plan `:581-582` says so, and all logic
  that can be tested was pulled into Tasks 1–4. This is not a finding.

## Findings

### MINOR 1 — the plan renames the spec's `TagChipView` and paging API without saying so

- **Claim:** the plan changes the names and signatures the spec fixes, and does not record the
  change.
- **Problem:**
  - The spec names `buildChipEntries`, `chipIsSelected`, `ChipMetrics chipMetrics(const ThemeTokens&, target)`,
    `measureChips(target, theme, entries, widthsOut)`, `drawChip(frame, theme, …)` and
    `pageStartOf` / `pageCountOf`.
  - The plan ships `TagChipView::buildEntries`, `isSelected`, `Metrics metricsFor(UiScreen&)`,
    `measure(screen, label, metrics)` (one label at a time), `draw(screen, …)` and a `PageSpan
    pageHolding(...)` in place of `pageStartOf`.
  - The plan is consistent with itself from Task 1 to Task 6, so an implementer will not trip. But a
    reviewer checking the diff against the spec will see names that differ and no note explaining
    them.
- **Evidence:** spec `:150-151`, `:160-179`. Plan `:551-570`, `:613-630`.
- **Fix:** add a short "Deviations from the spec" note under the plan's header. It should list the
  renames and say that `pageHolding` returns `{first, next, page}` so the page indicator and
  `turnPage` avoid a second walk.

### MINOR 2 — the grid's focus outline can go past the screen edge

- **Claim:** the grid's button-focus outline can be drawn partly off-screen.
- **Problem:**
  - Task 6 draws the outline `max(gap/2, 1)` px outside the chip on all four sides
    (plan `:1062-1070`).
  - A chip in the first column starts at `body.x + listInset`. On a theme where `listInset` is 0
    and the safe area has no side margin, the outline's left stroke lands at x = −2…−1 and is
    clipped away.
  - A chip that reaches the full line width has the same problem on the right.
  - Highlights deliberately limits its horizontal outset to `std::min(gap, inset)` for exactly this
    reason (`HighlightsActivity.cpp:784-789`).
- **Evidence:** plan `:1064`. Compare `HighlightsActivity.cpp:785-786`, whose comment reads
  "Horizontally the outline may only use the list inset (0 on the base theme)".
- **Fix:**
  - Clamp the outline's left edge to `body.x`.
  - Clamp its right edge to `body.x + body.width`, i.e. `std::max(focused.x - outset, body.x)` and
    the matching width.
  - Alternatively, use Highlights' `std::min(outset, inset)` horizontally.

### MINOR 3 — a swipe after a button step keeps the focus outline

- **Claim:** a swipe page turn leaves the button-focus outline on.
- **Problem:**
  - `turnPage(..., fromButton=false)` does not clear `buttonFocus_` (plan `:1205-1208`).
  - So if the owner steps with a button and then swipes, the new page's first chip is outlined, even
    though touch moved it.
  - Spec A7 says the outline shows "only after a button press moved it".
  - A tap clears it through `onRowAction`, but a swipe does not.
- **Evidence:** plan `:1207`. Spec `:104-107`.
- **Fix:** set `buttonFocus_ = fromButton;` instead of `if (fromButton) buttonFocus_ = true;`.

VERDICT: CLEAR
