Tier: standard

# PR #213 review: square 7×10 chapter grid with tag and bookmark markers (#199)

Reviewed at `28cbf749` against issue #199, the spec
`docs/superpowers/specs/2026-09-30-issue-199-design.md` and the plan
`docs/superpowers/plans/2026-09-30-issue-199-plan.md`. I re-ran the host tests in this worktree
(`NumberGridLayoutTest`, `BookGridLayoutTest`, `GridMarksTest`): all 66 passed. I did not rebuild
the firmware; the PR reports `pio run -e x4pro` SUCCESS.

## Intent

**Acceptance criteria**

- *Isaiah and Genesis on one page, Psalms 3.* `NumberGrid::MAX_CELLS` 70
  (`NumberGridLayout.h:18`) with `geometryFor` unchanged gives 7 × 10 on the 480 × 743 and
  480 × 695 bodies (`NumberGridLayoutTest.cpp` `PortraitBodyIsSevenByTen`,
  `PreCompactBodyIsSevenByTenWithoutTheClamp`). The page counts are asserted in
  `ChapterListsFitTheirPages` (50 → 1, 66 → 1, 150 → 3, last Psalms page 10 cells).
  `UiAppHost::MAX_INTERACTIONS` 96 (`UiAppHost.h:35`) keeps 70 cells within budget, and the
  comment gives the new per-host cost correctly (96 × 32 B = 3 KB).
- *Square cells, centred, never below the tap floor.* `gridRect` (`NumberGridLayout.h:59-66`)
  builds a rect of whole `cellSizeFor` cells, centred on both axes. `buildGrid` hands that rect
  to `keyGrid` at the chapter and verse levels (`BibleNavigationActivity.cpp:547-558`), and
  `keyGrid`'s own division (`key-grid.h:54-55`) then gives `cellW == cellH`. The test
  `KeyGridArithmeticOnTheRectGivesSquareCells` checks this for every height in 632-900.
  `minTouchSize` stays at the cell size, so the enlarged hit rects never overlap.
- *• for a tagged passage, a mark for a bookmark, current chapter stays inverted.* Marks are
  drawn after `keyGrid` (`BibleNavigationActivity.cpp:561-606`). Ink is resolved through
  `frame.stateFor(props.action, row, base)`, the same call keyGrid's Space glyph makes
  (`key-grid.h:86`), and `row` is the same absolute value each key carries as `key.value`. The
  mark therefore turns white on the selected, pressed and tap-flash cells. The bitmap is BW1 and
  MSB-first (`FreeInkUICore.h:410-412`), which matches how `BaseTheme.cpp:38-39` reads
  `BookmarkStatusIcon`, so the ribbon renders the same as the status bar's.
- *`test/number_grid` extended for 70 cells and paging; marker mapping host-tested.* Done. The
  paging tests that used a literal 48 are restated at 70. `RowClampFiresInsteadOfOverflowing`
  moves to the 743 fixture, where the clamp still fires (`unclampedRows == 11`), as spec §9
  requires. The new `GridMarksTest` covers every case listed in spec §9.
- *Heap log; device tap test.* `onEnter` logs internal free, the largest internal block and
  PSRAM free at `LOG_INF` (spec §5, plan Task 8). The heap comparison and the tap test are on the
  PR's human checklist. Neither can be checked here.

**Spec requirements.** A1-A12 are all implemented:

- A3: the book grid has its own 48-cell cap, and `CellCapLimitsRowsWhenManyColumnsFit` still
  expects 6 × 8.
- A6: four 22 B bitsets, bounds-checked, with the `LOG_DBG` above capacity
  (`BibleNavigationActivity.cpp:199-201`).
- A7: bits are cleared at the top of `loadChapters` and `loadVerses`, so a failed load leaves
  them empty. They are built before `enterLevel` publishes the level under `RenderLock`. Every
  caller of `loadChapters` and `loadVerses` runs while a different level is on screen, so the
  render task never reads half-built bits.
- A8: `spanFor`, including the collapse of a reversed end or an end in another book. The verse
  level compares against each anchor's own chapter, so single-chapter books work.
- A9: the constructor copies the bookmarks with one `makeUniqueNoThrow` allocation, allocates
  nothing when there are none, and logs `LOG_ERR` on OOM.
- A10: placement and state-resolved ink.

`selectedBook` is set before every `loadVerses` path that reads it (`activateIndex` Book sets it
before `openVerseList`). `markChapters` takes `bookIndex` as a parameter, because
`enterAtPosition` sets `selectedBook` only after `loadChapters`. Bookmarks are loaded on reader
entry (`EpubReaderActivity.cpp:209`), so `entries()` is populated when the navigator opens.

**Scope.** The PR's "Deviations from the issue" section lists three departures, each with its
reason:

- The ribbon replaces ⌂. U+2302 is in none of the built-in fonts, and the ribbon matches the
  status bar.
- Per-level bitsets replace one whole-Bible set. They cost 88 B with no heap allocation.
- The optional book-level dot is deferred.

None of these reduces scope silently. Nothing was added beyond what the issue asks for.

**Plan conformance.** The code follows plan Tasks 1-8 nearly line for line. The plan's ground
rules record the one naming change from the spec, `Box` and `gridRect(bodyX, bodyY, …)`. The PR
body states that Tasks 1-2 skipped a separate red run and that the plan review had already
confirmed those reds. That is a disclosed process gap, not an unexplained divergence.

**Tests.** `GridMarksTest` checks outcomes (which cells are set, how many), not how the code
reaches them. It covers the boundary cases: before the first anchor, between anchors, past the
last anchor, a pre-offset bookmark, a different spine, and a span entering from the previous
chapter. The square-cell tests re-derive keyGrid's arithmetic on the returned rect instead of
re-reading `gridRect`'s own formula. Fixtures are synthetic offsets and spine numbers, with no
publisher text.

## Quality

- **Existing patterns reused:**
  - `GridMarks.h` is a pure, header-only helper built the way `BibleEntryPosition.h` is, and
    reuses its `chapterRowFor` instead of repeating the lookup.
  - Marks are drawn on the target after the component, as `UiSliderDialog` does.
  - State-resolved ink copies `key-grid.h:86`.
  - The heap log follows `BibleSearchIndexer`'s `heap_caps_*` usage.
  - `STUDY.passages()` is iterated on the loop task, as `HighlightsActivity` does.
- **Naming and structure** match the neighbouring code: `markChapters`/`markVerses` next to
  `loadChapters`/`loadVerses`, and `drawCellMarks` next to `buildGrid`. The new comments explain
  why (the book-index correspondence, why a copy of the bookmarks, the render-task hand-off)
  rather than restating the code.
- **Error handling** has the established shape: `makeUniqueNoThrow`, a null check, `LOG_ERR`,
  then a degraded result (no bookmark marks). No path writes to SD.
- **Resource use** stays within the rules: the per-object growth is justified in spec §8,
  `reserve` comes before `push_back` in the tests, nothing allocates in the render loop, and
  there is no `std::function`.
- **Duplication:** one small instance (finding 1 below).
- There is no dead code and no commented-out code.

## Findings

1. **MINOR:** the ribbon's geometry is duplicated.
   - `BibleNavigationActivity.h:53-55` redeclares `RIBBON_W = 16`, `RIBBON_H = 14` and
     `RIBBON_TOP_CROP = 2`.
   - These copy `bookmarkStatusIconWidth`, `bookmarkStatusIconHeight` and
     `bookmarkStatusIconTopCrop` from `src/components/themes/BaseTheme.cpp:29-32`, which are
     private to that file's anonymous namespace.
   - The comment names the coupling, so this is a drift risk, not a bug. The fix is to move the
     three constants next to `BookmarkStatusIcon` in `src/components/icons/bookmark.h` and have
     both sites use them.
2. **MINOR:** `BookGridLayoutTest.cpp:89`, `KeepsItsOwnCellCap`, only asserts
   `EXPECT_EQ(BookGrid::MAX_CELLS, 48)`, which restates the constant. The behaviour is already
   pinned by `CellCapLimitsRowsWhenManyColumnsFit` (`BookGridLayoutTest.cpp:83`,
   `rows == 8`), which would fail if the book grid followed the 70 cap. The plan specified this
   test (plan Step 1.1), so it adds no risk. It is simply redundant, and can be dropped.

No BLOCKER or MAJOR findings. The device items (tapping the edge cells of a full 7 × 10 page,
the markers on the inverted cell, the internal-heap delta against `main`) remain the human
tester's, as the PR's checklist states.

VERDICT: CLEAR
