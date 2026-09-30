Tier: standard

# PR #242 review (pass 0): Home breathing room and week-strip spacing

Reviewed: `gh pr diff 242` (code: `src/activities/launcher/HomeLayout.h`, `src/activities/launcher/LauncherActivity.cpp`, `test/home_layout/HomeLayoutTest.cpp`), issue #237, the spec `docs/superpowers/specs/2026-09-30-issue-237-design.md` and the plan `docs/superpowers/plans/2026-09-30-issue-237-plan.md`. I rebuilt `HomeLayoutTest` locally and ran it with `ctest -R HomeLayout`: all 19 discovered cases pass. The executable also holds the `HomeTargetsTest` and `HomeVerseCacheTest` sources, 41 `TEST`s in all, which matches the PR's count.

## Intent

Issue #237 acceptance criteria:

- **Room at the top and above Continue / Go to.** Met. `compute` adds `PAD` to `top` and passes `insets.top + PAD` to `MastheadLayout::band` (`HomeLayout.h:118`, `:125-126`). `plateHeight` gains a `PAD` term (`:110`), and `buttonRow` starts `PAD` below the plate header (`:136`). Continue and Go to take their y from `buttonRow.y`, so both move. The issue suggested taking the space "from the empty area under Recent". The cover gives up the space instead, as settled in decision d1. The PR description states this and explains why, so the scope change is disclosed, not silent. The icon row stays put: `TheLastSectionEndsAtTheViewableFoot` is unchanged and `TheGapsLeaveTheSectionsBelowTheHeroInPlace` pins the hero's foot at its old value.
- **Day numbers at least one digit apart, measured, each centred under its letter.** Met. `computeLayout` measures the ink width of `"0"` and `"00"` at UI_10 (`LauncherActivity.cpp:96-97`), and `stripCellWidth` makes each cell the width of `"00"` plus one digit (`HomeLayout.h:89-91`). The letter and the number are both centred in the same `stripDay` box widened by `PAD` on each side (`LauncherActivity.cpp:332-337`), so they share a centre. The highlight is inset 1 px inside its own column. Using ink widths for both measurements is consistent with how `drawCentredIn` centres text (`LauncherActivity.cpp:67-72`).
- **No hardcoded 480/800; everything goes through theme metrics.** Met. Every new value comes from `PAD`, `ThemeMetrics`, `LineHeights` or the measured `TextWidths`. The 26 px `STRIP_CELL_MIN` is only a fallback for when the font is missing (spec A7).
- **Host tests updated, and the screen still fits at 480×800.** Met. The pins are updated in both themes. `TheHeroKeepsItsArtAboveThePlate`, `SectionsStackWithoutOverlap` and `TheLastSectionEndsAtTheViewableFoot` pass unchanged.
- **Device comparison.** Left to the owner. The PR gives the checklist and the expected serial line (`LOG_DBG` at `LauncherActivity.cpp:101`).

Spec coverage: A2–A10 are all implemented, including the parts that are easy to skip.
- **A5:** `fallbackHeader` is a new field, it is drawn from `layout` (`LauncherActivity.cpp:254`), and the dead `metrics` local in `drawHero` is removed.
- **A6/A7:** the measured inputs and the floor are both implemented.
- **A10:** the highlight geometry moved into `HomeLayout::stripHighlight`, so the host tests check the same code the launcher draws with.

Every test named in the spec is present. `StripNumbersCentreUnderTheirLetters` ships as `StripDaysTileTheStripSoLettersAndNumbersShareACentre`, which is the name the plan already uses (plan Task 3). The diff matches the plan task for task, and the one merge from `origin/main` is explained in the PR. There is no scope expansion: `MeetingWeekView`, themes, i18n and `test/CMakeLists.txt` are untouched.

The tests check behaviour, not implementation details. The digit-gap and neighbour-clearance tests work in ink coordinates (`numberInkLeft`/`numberInkRight`, `HomeLayoutTest.cpp:37-38`), not by comparing field values. `WIDTHS{11, 23}` is derived from the glyph table in a comment, the same way `LINES` is.

## Quality

- **Follows existing patterns.** `TextWidths` mirrors `LineHeights`: a measured-input struct passed into the renderer-free `compute`. `stripDay`/`stripHighlight` follow the file's existing style of free `constexpr` helpers (`bottomOf`, `contains`). There is no second way of doing the same thing. `STRIP_CELL` has no remaining references (`grep` over `src` and `test`).
- **Naming and structure** are consistent with the rest of the file. The new comments explain *why* (tabular digits make "00" the widest day number; the 1 px inset stops the highlight meeting a neighbour; the fallback header sits lower so Home has room at the top), which is what the repository's comment rules ask for.
- **Error handling:** there is no new failure path. A missing font makes `getTextWidth` return 0, and the floor covers that; `TheStripCellHasAFloor` tests it.
- **Resources:** no allocations beyond two `getTextWidth` calls at layout time, which already existed as a pattern. `std::max` is covered by `#include <algorithm>` (`HomeLayout.h:3`). One `int` and one `Box` are added to the cached `Layout`.
- **Test design:** the tests are well designed on the whole. Two small places restate the implementation; see the findings.

## Findings

1. **MINOR:** `test/home_layout/HomeLayoutTest.cpp:79-84`. `TheGapsLeaveTheSectionsBelowTheHeroInPlace` hardcodes the pre-change hero heights (258/280) behind a theme-pointer ternary. The claim it makes, that sections below the hero keep their y, would be stated more directly by comparing `recent.y` (or the icon row) against a layout computed with `PAD` removed. As written, it is a second copy of the old numbers from `TheHeroTakesTheRemainder`. It still guards the right property, so this is optional to change.
2. **MINOR:** `test/home_layout/HomeLayoutTest.cpp:188`. `EXPECT_LE(box.x + box.width - 1, cell.x + cell.width - 2)` restates the 1 px inset rather than checking behaviour. The neighbour-ink assertions on the lines after it already carry the requirement from the issue. This line can go.

Neither finding changes behaviour, scope or a decision. The implementation is sound.

VERDICT: CLEAR
