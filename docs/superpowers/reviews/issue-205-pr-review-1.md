Tier: standard

# PR #212 review, pass 1: tag chip row in Highlights (issue #205)

Reviewed: `gh pr diff 212` (branch `feature/205-tag-chip-row`, head `81ae0ab7`) against issue #205,
the spec at `docs/superpowers/specs/2026-09-30-issue-205-design.md`, the plan at
`docs/superpowers/plans/2026-09-30-issue-205-plan.md`, and pass 0
(`docs/superpowers/reviews/issue-205-pr-review-0.md`). This pass concentrates on the two
commits since pass 0 (`89cc819c`, `81ae0ab7`), and re-checks the rest of the diff. I rebuilt
`TagRowsTest` locally and all 38 tests pass (39 minus the deleted tautology).

## Intent

**Acceptance criteria**

- *The counts match the passages in the list.* Met.
  - `rebuildVisibleIndices` filters through `TagChips::passageMatches`
    (`HighlightsActivity.cpp:55`).
  - `rebuildChips` counts with `TagChips::count` (`:113`).
  - `EveryCountEqualsTheRowsItsFilterShows` pins the agreement between the two against 300
    normalised random passages.
- *Filtering by chip matches today's filter behaviour, with a host test for counting and
  overflow.* Met.
  - `selectChip` (`:164-184`) stores the same three values the picker path stores
    (`:193-199`): `nullopt`, `UNLABELLED` or the id.
  - `test/tag_rows/TagChipRowTest.cpp` covers the counting, overflow and hit geometry.
- *Works with 0 tags and with more tags than fit on two lines.* Met.
  - 0 tags gives `All n` plus `Unlabelled m` (`:124-135`).
  - Overflow gives `…` and the 24-chip cap. `NoChipsPlaceNothing`,
    `OverflowPutsEllipsisLastOnLineTwo`, `EllipsisDropsItsNeighbourWhenItDoesNotFit`,
    `TruncatedCandidatesStillOverflow` and `InvariantsHoldForEveryCandidateCount` pin this.
- *Rows use #194's compact metrics.* Met.
  - `syncRingViewport` resolves the height through `ListRowHeight::resolve` (`:298-308`).
  - B1's fix wires `props.nav = &n` (`:328`), so `list()` no longer applies its fixed-height clamp
    (`list.h:312-313`), and the variable-height tail is drawable again.

**B1 fix, verified.**
- With `props.nav` set, `list()` calls `onListRendered` (`list.h:655`). That call sets `top` and
  `drawnRows`, and returns early because `followPending` is false (`list.h:187-188`).
- `follow()` has no caller on this screen. `grep` finds no `moveSelectionTo` call left in
  `HighlightsActivity.*`, and the base `navigateButtons` is overridden (`:274-286`). So the
  ring-vs-row off-by-one that the spec feared cannot occur.
- `scrollBy` now clamps with the measured page size (`list.h:216-217`). The swipe path and the
  hold-to-page jump both reach the tail.
- `moveRingTo`'s forward step is only partly fixed. See M1.

**Spec coverage.** A1 to A13 and A11b are implemented as pass 0 recorded. The spec has a
"Changes from PR review pass 0" table (`design.md:392-397`), and the PR body's response section
matches the code. m2 was declined with a stated reason (file lock, and shared base classes), which
is a legitimate scope call for an optional refactor.

**Divergences.** The plan's five declared deviations still hold. The post-plan change from
`visibleRows` to `pageRows()` in `moveRingTo` is explained in the PR body and the spec table.

**Scope.** Nothing expands the scope: no new key, store or format, and `TagFilterActivity` is
untouched. Nothing is silently reduced.

**Tests.** The tautological `SameWidthsGiveTheSamePlacements` is gone. The remaining tests
exercise behaviour: the random equivalence, the pairwise hit-rect intersection sweep, and the
prefix-order invariants.

## Quality

- `TagChipRow.h` follows the `NumberGridLayout.h` shape: pure, inline and host-testable, filling a
  caller-owned `Layout`.
- The ring navigation mirrors `UiTabListActivity`.
- The new `props.nav` comment (`:326-327`) gives the non-obvious reason, not a restatement.
- The row-height block is still duplicated three times. That was pass 0's m2, declined with a
  reason, and I do not raise it again.
- I found no dead code or commented-out code.
- Error handling keeps the file's established shape: stale-index bounds checks, popup guards, and
  every rebuild under `RenderLock`.

## Findings

**M1 — MAJOR. A forward button step can still leave the selection on an undrawn row, because
nothing corrects `moveRingTo`'s estimate after the build.**
- **What the code does.**
  - `moveRingTo` places `top` with `listTopIndexFor(selected - 1, top, n.pageRows(), rowCount)`
    (`HighlightsActivity.cpp:266-268`).
  - `pageRows()` is `drawnRows` from the *previous* build, measured from the *old* `top`
    (`list.h:172`, `:183-185`).
  - On a forward step past the bottom row, `listTopIndexFor` moves `top` by one
    (`FreeInkUI.cpp:869-870`). The row that leaves the page is the old top row. The row that
    enters is the newly selected one.
- **How the selection goes undrawn.**
  - Highlights rows have three heights: no snippet, a 1-line snippet, or a 2-line snippet
    (`maxLines = 2`, `:658`). When the entering row is taller than the leaving row, the new page
    can hold one row fewer than `drawnRows`. The selected row is then laid out below the band and
    not drawn.
  - Nothing corrects this. `onListRendered` only advances `top` when `followPending` is set
    (`list.h:187-188`), and only `follow()` sets that flag. This screen never calls `follow()`,
    by design.
  - The render loop at `:758` therefore has nothing to consume.
- **Why this is a regression.** On `main`, Highlights stepped through
  `UiListActivity::moveSelectionTo` → `follow()` (`UiListActivity.cpp:72-82`). That path
  guaranteed that the selection is drawn, using the `onListRendered` correction and the rebuild
  loop.
- **Effect.**
  - After such a step, the highlighted row is off-screen.
  - A Confirm then jumps to a passage the user cannot see.
  - The next step corrects the view, because it uses the new, smaller `drawnRows`.
- **Why it matters.** Button access was kept on purpose (A10). Pass 0's B1 named this exact path
  ("a button step can put the selection on a row that is not drawn"), and the fix claims to keep
  "the selected row inside the rows actually drawn". That claim holds only when the entering and
  leaving rows are the same height.
- **Why it is not a BLOCKER.** It is not a data-loss or reachability failure: touch and swipe are
  unaffected, and every row is still reachable. It is a technical fix with no owner decision
  involved.
- **Fix, inline.** Correct `top` after the build, in ring terms.
  - In `buildScreen`, after `screen.list(props)` (`:660`), add a check:
    `if (n.selected > 0 && n.drawnRows > 0 && n.selected - 1 >= n.top + n.drawnRows)`.
  - When the check is true, advance `n.top` to `max(n.top + 1, n.selected - n.drawnRows)` and set
    `n.rebuildNeeded = true`.
  - This mirrors `onListRendered`'s own correction with the ring offset applied, so the existing
    loop at `:758` converges as it does for `follow()`. Gate it on a member flag set in
    `moveRingTo` so that swipes, which may leave the selection off-screen by design, stay
    untouched.
- **Verify on device.** In a publication whose highlights mix 1-line and 2-line snippets, step
  Right past the last visible row several times. The highlighted row must be visible after every
  step.

VERDICT: CLEAR
