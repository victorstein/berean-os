Tier: standard

# PR #245 review 0 — tag chip grid (issue #240)

Reviewed `git diff main...HEAD` on `fix/240-tag-chip-grid` (fbce420b) against issue #240, the design
spec `docs/superpowers/specs/2026-09-30-issue-240-design.md` and the plan
`docs/superpowers/plans/2026-09-30-issue-240-plan.md`. The `TagRowsTest` suite was rebuilt and run
locally (`ctest -R "TagChip|TagRows"`): 50/50 pass.

## Intent

**Acceptance criteria**

- *20+ tags on one grid screen, host test for the layout, scroll only past one screen.*
  Met. `TagFilterActivity::buildScreen` (`src/activities/reader/TagFilterActivity.cpp:57-131`) lays out
  every uncapped chip (`rebuildChips` passes `SIZE_MAX`, `:630-632` in the diff) through
  `TagChips::layoutPage`. It pages only when `pageCountOf` is above 1, and the `x/y` strip is drawn only
  then (`drawFooter`, `:736`). The host tests `TagChipGrid.OwnersTwentyFourChipsFitOnePage`,
  `AFullPaletteSplitsIntoPagesThatCoverEveryChipOnce`, `NarrowChipsStopAtTheInteractionCap` and
  `ShorterBodyNeverHasFewerPages` cover the one-page case, the overflow case and the two-step strip
  pagination. The 600 px test body is conservative: the Lyra metrics give about 700 px
  (`LyraTheme.h:11-38`).
- *Tap height at least `minTouchSize`, padding visibly larger, from theme metrics.* Met.
  `TagChipView::metricsFor` (`TagChipView.cpp:476-484`) uses `spaceLg` for horizontal padding and
  `spaceMd` for vertical padding. `TagChips::chipHeight` clamps the height to `theme.minTouchSize`
  (`TagChipRow.h:288-290`). Both screens size their chips through this one function, and nothing is
  hardcoded.
- *Filtering behaves exactly as today.* Met for the result contract. `activateIndex` returns an
  empty `TagSelectionResult` for All, `{UNLABELLED}` for Unlabelled and `{id}` for a tag. Back and Home
  still set a cancelled result. The Highlights result handler is unchanged. Long-press retire is now
  keyed by tag id and guarded to `Kind::Tag`.
- *Selected chip inverted, counts chapter-scoped in "Tags here" (#226).* Met. `openTagFilter` computes
  the scope with no lock held and passes it in together with `filterTagId_`
  (`HighlightsActivity.cpp:172-181`). The grid recounts after a retirement, and it falls back to All
  when the active filter is retired.

**Spec coverage.** A1–A9 are all implemented, and the plan's list of renamed functions matches the
code. That covers the candidate picker, `chipHeight`, the generalised `hitPadding`, the
`GRID_MAX_CHIPS = 64` cap, `GridPage` as a member, two-step pagination around the strip, swipe and
hold page turns, clamped button steps with `buttonFocus_`, entry on the page holding the filter,
removal of `FilterRows` together with its tests, and the corrected 96-interaction comment. Nothing
from the harder half was dropped. The chapter scope, the recount after retirement, the fallback to
All, and page-follows-selection are all present.

**Scope.** One visible behaviour change is not literally "as today". The **…** screen no longer
offers zero-count tags or a zero-count Unlabelled, so those tags can no longer be retired from that
screen. Spec A1 makes this change on purpose. The spec review accepted it, the PR body states it,
and the issue's own example lists only non-zero chips. Retirement is still available from
`TagPickerActivity`. I am not raising a finding for it. `activateIndex` also gains a
`confirmPopup_.isActive()` guard and `app.clearTapFlash()`. Both are small hardening steps that the
base contract asks for (`UiListActivity.h`, the `activateIndex` doc comment), so they do not expand
scope.

**Plan divergence.** I compared the plan's code blocks for `buildScreen`, `drawFooter`, `retireTag`,
`handleCustomInput` and `turnPage` with the implementation. The only difference I found is the
indicator buffer size, 16 bytes in the plan against 24 in the code. The PR states and explains that
change. Its new size also matches `BibleNavigationActivity`'s `pageIndicator[24]`.

**Tests.** The tests check properties rather than restate the code. They check that every chip lands
on exactly one page and in order, that each page stays within the line and chip caps, that pages
always advance, that `pageHolding` agrees with the laid-out pages, that grid hit rects never
intersect, and that the array and layout overloads of `hitPadding` agree. The candidate tests keep the
old guard's intent that All and Unlabelled can never be retired
(`TagChipCandidates.OnlyTagCandidatesNameAPaletteSlot`).

## Quality

- **Existing patterns.** The grid follows `BibleNavigationActivity` closely. Swipes are consumed as page
  turns in `handleCustomInput`, `navigateButtons` is overridden with the lock taken per handler, a
  single `RenderLock` covers the read and write in `turnPage`, and the `%d/%d` sub-header sits at the
  same position. The pure paging logic lives in the host-tested `TagChipRow.h`, as `NumberGridLayout.h`
  does for the number grid. `onEnter` follows the order used in `HighlightsActivity::onEnter`: base
  `onEnter` first, then the rebuild under the lock.
- **One implementation.** The chip building, the measuring and the pill style are extracted once into
  `TagChipView` and called from both screens. They are not copied. The old
  `HighlightsActivity::rebuildChips` and `chipIsSelected` bodies are deleted, not left beside the new
  code.
- **Locking.** Chips are built off-lock and then swapped under `RenderLock`
  (`TagFilterActivity::rebuildChips`). `STUDY.retireTag` now runs outside the render lock, which fixes
  the old code's comment and behaviour disagreeing ("The SD write happens after, outside it" while
  it ran inside). The render task no longer reads `STUDY` at all. `filter_`, the page geometry and
  `buttonFocus_` are written only under the lock.
- **Resources.** `GridPage` (~1 KB) is a member. `widths_` and `chips_` are sized once per rebuild,
  and neither is allocated per frame. The per-chip `StyleSet` and `ButtonProps` locals in
  `TagChipView::draw` are the same locals `buildChipRow` already had, so stack use does not grow.
- **Comments.** They explain reasons, such as why a swipe is consumed, why `minTouchSize = 0`, and why
  the outline is clamped. They are written for the merged state, and none restates the next line. No
  dead or commented-out code remains, and `grep` finds no reference to `FilterRows` or
  `pendingRetireRow_`.
- **Small duplication.** The "retired filter falls back to All" check appears twice. See MINOR 1.

## Findings

1. **MINOR** — The retired-filter predicate is duplicated. `TagFilterActivity.cpp` `retireTag`
   (diff line 813):
   `if (filter_ && *filter_ != study::UNLABELLED && !STUDY.palette().isActive(*filter_)) filter_.reset();`
   It repeats `HighlightsActivity::dropRetiredFilter` (`HighlightsActivity.cpp:84-88`) word for word,
   including the UNLABELLED exemption, and the comment next to it points at that function. The
   pattern is one line and the comment names its twin, so any drift would be visible. Still, the PR
   already created `TagChipView` as the shared home for chip and filter logic that depends on
   `STUDY`. A `TagChipView::dropRetired(std::optional<study::TagId>&)` there would keep the
   UNLABELLED exemption in one place. Optional; no behavioural impact.

No BLOCKER or MAJOR findings.

VERDICT: CLEAR
