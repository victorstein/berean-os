Tier: standard

# PR #212 review, pass 0: tag chip row in Highlights (issue #205)

Reviewed: `gh pr diff 212` (branch `feature/205-tag-chip-row`, head `60138879`) against issue #205,
the spec at `docs/superpowers/specs/2026-09-30-issue-205-design.md` and the plan at
`docs/superpowers/plans/2026-09-30-issue-205-plan.md`. I built and ran `TagRowsTest` locally: 39/39
pass.

## Intent

**Acceptance criteria**

- *The counts match the passages in the list.* Met. `rebuildVisibleIndices` now filters through
  `TagChips::passageMatches` (`HighlightsActivity.cpp:55`). `TagChips::count` agrees with it
  because of the dedup invariant. `EveryCountEqualsTheRowsItsFilterShows`
  (`TagChipRowTest.cpp:104-131`) pins that agreement against normalised random data, which is the
  right test for this criterion.
- *Filtering by chip matches today's filter.* Met. `selectChip` stores the same values
  `openTagFilter` stores: `nullopt` for All, `UNLABELLED`, or the id (`HighlightsActivity.cpp:163-183`).
  Host tests cover the counting and overflow logic.
- *Works with 0 tags and with more tags than fit on two lines.* The layout is met and tested:
  `NoChipsPlaceNothing`, `AllAndUnlabelledShareOneLine`, `OverflowPutsEllipsisLastOnLineTwo`,
  `EllipsisDropsItsNeighbourWhenItDoesNotFit`, `NeverPlacesMoreThanMaxChips`, and
  `InvariantsHoldForEveryCandidateCount` for 0–60 candidates.
- *Rows use #194's compact metrics.* The row height still comes from `ListRowHeight::resolve`
  (`HighlightsActivity.cpp:296-308`). However, the same change stops the list from being
  nav-managed, and that breaks reachability of the list's tail. See B1.

**Spec coverage.** A1 (order), A2 (zero-count hiding with the active-filter exception,
`HighlightsActivity.cpp:128-135`), A5 (inverted `…`, `:528-538`), A6 (cap), A7, A8 (touch only),
A9 (no new keys), A10 (ring, `buttonFocus_` outline), A11b (`minTouchSize = 0`, tiled
`hitPadding`) and A13 (bold measurement) are all implemented. I found no half-done requirement.

**Divergences.** The plan lists five deliberate deviations (`plan.md:16-35`): width-only
`layout()`, no `moreCandidates_`, no `activeTags()` snapshot, outline cleared by taps rather than
"any routed touch", and rebuilds under `RenderLock` in `onEnter` and in the picker handler. The code
matches each one. `TruncatedCandidatesStillOverflow` pins the reasoning behind dropping
`moreCandidates_`. I found no divergence that the plan does not explain.

**Scope.** There is no expansion: no new key, store or format, and `TagFilterActivity` is untouched.
There is no silent reduction either.

**Tests.** The counting and geometry tests exercise behaviour: the random equivalence check, the
pairwise hit-rect intersection sweep across gaps and counts, and the prefix-order invariants. One
test is a tautology (m1).

## Quality

- `TagChipRow.h` follows the `NumberGridLayout.h` pattern: a pure, inline, host-testable header. It
  fills a caller-owned `Layout` so the render task's stack stays small, and the activity keeps
  `chipWidths_` and `chipLayout_` as members. Both choices fit the resource rules.
- The ring navigation mirrors `UiTabListActivity` (`moveRingTo`, `navigateButtons`,
  `syncRingViewport`), and it improves on it: writes to `moveRingTo` happen under `RenderLock`,
  which the tab version lacks. However, it also copies the tab version's `props.nav = null`
  decision into a list whose rows are variable-height, which the tab version's lists are not. See
  B1.
- The inverted chip styles mirror the tab bar's pills. Chips are drawn with `fui::button(frame, …)`
  directly, with the reason stated at `HighlightsActivity.cpp:549-551`. That comment explains why,
  not what, so it earns its place.
- `filterSubtitle_` and `computeFilterSubtitle()` are removed cleanly, the class comment is
  rewritten, and I found no dead or commented-out code.
- Error handling matches the file: stale chip indices are bounds-checked (`:166`), handlers are
  guarded while a popup is open, and there is no new failure path.
- The row-height block is now duplicated in three places (m2).

## Findings

**B1 — BLOCKER. The oldest passages in a long list can no longer be reached, because the
passage list stopped being nav-managed.**
- **Evidence.** `syncRingViewport` leaves `props.nav` null (`HighlightsActivity.cpp:324-326`). On
  `main`, the list went through `syncListViewport` → `ListNav::syncToProps`, which sets
  `props.nav = this` (`freeink-sdk/libs/ui/FreeInkUI/include/components/lists/list.h:255`).
- **Why rows grow.** Highlights rows are variable-height by design:
  `props.subtitleText.maxLines = 2` (`HighlightsActivity.cpp:656`), and the subtitle is the
  passage snippet. A subtitle that wraps grows the row by one line (`list.h:428-437`), and a
  typical snippet wraps.
- **The fixed-height clamp.** With `props.nav` null, `list()` applies the fixed-height viewport
  clamp `if (overflows && !props.nav && top > props.count - visible) top = count - visible`
  (`list.h:312-313`). It then stops drawing when rows run out of height (`list.h:442-444`). The SDK
  comment directly above that clamp names the consequence: "the last row(s) can never be drawn"
  (`list.h:306-311`).
- **Result.** For example, if 8 rows fit by the fixed estimate but only 5 wrapped rows fit, the last
  3 passages can never be drawn, tapped or selected. The list is newest-first, so these are the
  user's oldest highlights.
- **Other paths.**
  - `drawnRows` is never updated, so `n.pageRows()` stays the fixed estimate for both the swipe
    clamp and the hold-to-page jump.
  - A button step can put the selection on a row that is not drawn:
    - `moveRingTo` sizes the viewport with `visibleRows` (`:265-268`);
    - no follow correction runs, because nothing calls `onListRendered`.
- **Scope of the regression.** The pattern is safe in `UiTabListActivity`'s callers (Settings and
  TextSettings), whose rows are single-line. It is not safe here. The spec (`design.md:229`) and
  the spec review (`issue-205-spec-review-0.md:102`) both carried the decision over without this
  check.
- **Fix.** This is a technical fix, not an owner decision. Set `props.nav = &n` in
  `syncRingViewport`:
  - `onListRendered` reads `selected` only when `followPending` is set (`list.h:188-190`), and only
    `ListNav::follow()` sets it. Neither `moveRingTo` nor `syncRingViewport` calls `follow()`, so
    the off-by-one the spec feared does not arise.
  - Wiring the nav turns off the fixed clamp and feeds `drawnRows` back into `scrollBy` and
    `pageRows()`.
  - Also size `moveRingTo`'s `listTopIndexFor` call with `n.pageRows()` rather than
    `n.visibleRows`, so a button step keeps the selected row inside the rows actually drawn.
- **Verify on device.** Use a publication with more wrapped-snippet highlights than fit on one
  screen. On "All", swipe to the end and confirm the oldest highlight is drawn, then step to it
  with Right.

**m1 — MINOR. `SameWidthsGiveTheSamePlacements` is a tautology.**
- **Evidence.** The test (`test/tag_rows/TagChipRowTest.cpp:213-223`) calls the pure `layout()`
  twice on identical input and compares the results.
- **What it misses.** It cannot fail, and it does not pin A13. A13's guarantee lives in
  `buildChipRow`, which measures every chip with `measureStyle.bold = true`
  (`HighlightsActivity.cpp:491-499`), and no host test can see that.
- **Fix.** Either delete the test, or rename it and drop the A13 claim in its comment. The real
  guard is the one-line bold measurement, which the device check covers.

**m2 — MINOR. The `ListRowHeight::Inputs` block is now duplicated in three places.**
- **Evidence.** The ten-line block in `syncRingViewport` (`HighlightsActivity.cpp:297-308`) is a
  verbatim copy of `UiTabListActivity::syncTabListViewport` (`UiTabListActivity.cpp:79-90`) and
  `UiListActivity::syncListViewport` (`UiListActivity.cpp:132-143`).
- **Why it matters.** A future change to the #194 metric inputs now has three places to update.
- **Fix.** Add a small protected `UiListActivity::resolveRowHeight(screen, hasSubtitle)` and use it
  in all three. That would be optional in this PR, except that B1's fix already reopens this
  function.

VERDICT: BLOCKER
BLOCKERS: 1
MAJORS: 0
