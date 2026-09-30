Tier: standard

# Issue #240 — spec review 0

Reviewed: `docs/superpowers/specs/2026-09-30-issue-240-design.md` against issue #240
(`gh issue view 240 --repo victorstein/berean-os`) and
`docs/superpowers/research/2026-09-30-issue-240-research.md`, at `779131e7`.

Overall the design is sound. The candidate rule (A1), the id-captured retire, passing the scope in
(A2), the page cap (A5) and the chip-size formula (A6) all check out against the code. Passage
indices do survive a retire: `PassageDoc::removeTagEverywhere` only edits tag lists
(`lib/StudyStore/StudyStore/PassageDoc.cpp:201-207`). The picker still lists every active tag, so
its retire path still reaches tags with a zero count (`TagPickerActivity.cpp:259-272`). The main
problem is one wrong claim about locking, which the spec turns into a contract. The rest are
precision gaps that can be fixed inline.

## MAJOR

### M1 — `buildChipEntries`' "no RenderLock" contract contradicts today's code and the spec's own retire flow

**Claim** (spec :155-157): `buildChipEntries`' caller "must hold no `RenderLock` while it runs,
and must swap the result into its member under the lock, as `rebuildRowItems` does today
(`HighlightsActivity.cpp:199-206`)". A2 (:75-76) repeats the no-lock rule.

**Problem.** Three separate problems:

1. Today's code does not work that way. `rebuildChips` runs *inside* the lock. `rebuildRowItems()`
   ends in `rebuildChips()` (`HighlightsActivity.cpp:126`), and every caller runs it under the
   lock: `onEnter` (`:44-46`), `selectChip` (`:202-205`), the filter result handler (`:234-236`),
   and `:538-540` and `:584-586`. With `spineFilter_` set, `rebuildChips` also calls
   `computeVisibleIndices(std::nullopt)` under that lock (`:135`). The header comment says that
   call must run with no lock held (`HighlightsActivity.h:133-134`). That is an existing quirk, and
   the spec presents it as the pattern to copy.
2. The spec contradicts itself. The grid's `retireTag` "rebuilds `chips_` under the render lock"
   (spec :202-203). That is the opposite of the contract at :155.
3. The contract guards the wrong function. `buildChipEntries` receives the scope already computed.
   It reads only `STUDY.passages()`, the palette, `STUDY.tagName` and `tr()`, and resolves no
   units. The lock-sensitive step is computing the scope (`passagesInDocument` → `unitsFor`,
   `StudyStore.cpp:233-237`), and that happens in the caller. An implementer who takes :155
   literally has two options. They can restructure four Highlights call sites that do not need it,
   or they can call `buildChipEntries(…, chips_)` outside the lock and write straight into a member
   the render task is reading, which is a data race.

**Evidence.** Cited above. `RenderLock` is a plain, non-recursive `xSemaphoreTake` on
`renderingMutex` (`ActivityManager.cpp:328-336`).

**Fix.**
- Drop the no-lock requirement from `buildChipEntries`. State that it does no unit resolution and
  is safe either inside or outside the lock. Its output must reach any member the render task
  reads while the lock is held.
- Put the no-lock rule on computing the scope only: `openTagFilter` (spec :219-220), which is
  correct as written.
- Say that Highlights' `rebuildChips` keeps its current structure: scope computed inside
  `rebuildRowItems`, as today, with the existing quirk not widened. Changing that is out of scope
  for #240.
- Remove "as `rebuildRowItems` does today".

## MINOR

### m1 — The `OptionPopup` does not share the `UiAppHost` interaction table

**Claim** (A5 :89-90; Error handling :248-249): the grid registers the popup's buttons in the
96-entry table, and 64 leaves room for them.

**Problem.** `OptionPopup` has its own table: `InteractionBuffer<INTERACTION_CAPACITY>`, with
`INTERACTION_CAPACITY = MAX_OPTIONS + 1` (`src/components/OptionPopup.h:233`, `:244`), and it
builds its own `fui::Frame` over that table (`:164`). The cap of 64 is still safe. It is just
more conservative than the reason given, since `NumberGrid::MAX_CELLS = 70` already runs under
the same 96 (`NumberGridLayout.h:15-18`).

**Fix.** Justify 64 by the chrome and a margin under 96, using `MAX_CELLS = 70` as the precedent.
Drop the popup argument.

### m2 — The sub-header strip makes the page count depend on itself

**Claim** (spec :84-85, :211-213): the `"x/y"` strip appears only when there is more than one
page, and its height comes out of the body.

**Problem.** The page count depends on the body height, and the body height depends on whether
the strip is shown. The model does not have this loop: `BibleNavigationActivity` shows its strip
based on the level alone (`subHeaderHeight()`, `BibleNavigationActivity.cpp:281-283`) and adds it
to the top margin (`:476`).

**Fix.** Say it explicitly. Paginate against the full body first. If that gives more than one
page, take out `metrics.tabBarHeight`, which is the model's strip height, then paginate again.
Once there is more than one page, removing space can only keep it at more than one, so this
settles in one step. The strip is drawn at
`safe.y + topPadding + headerHeight`, as at `:712-714`.

### m3 — The page geometry the loop task needs is not listed as members

**Claim** (spec :179-184, :205-210): swipe and held-button paging move `nav.selected` to the first
chip of the adjacent page, under the lock.

**Problem.** `nextPageStart`/`pageStartOf` need `lineWidth`, `gap` and `maxLines`. Only
`buildScreen` knows these, on the render task, from `screen.body()` and the theme. The member
list only has `chips_`, `widths_`, `page_`, `buttonFocus_` and "the page count". `GridPage.lines`
also doesn't say whether it is relative to the page, and `hitPadding` (`TagChipRow.h:178-180`)
and `lineTop` both need it to be.

**Fix.**
- Add `gridLineWidth_`, `gridGap_` and `gridLinesPerPage_` members. `buildScreen` writes them and
  the loop task reads them under `RenderLock`, as `moveGridPage` reads `bookLayout`/`grid`
  (`BibleNavigationActivity.cpp:372-389`).
- State that `Placed.line` in a `GridPage` counts from the top of the page.

### m4 — Retiring the active filter on the grid leaves no chip inverted

**Problem.** When the owner long-presses the currently active tag and retires it, the tag leaves
`activeIds`, so it has no candidate. The grid's saved `filter` still names it, so
`chipIsSelected` matches nothing and no chip is inverted until the owner leaves. Highlights resets
the filter to All only on return (`HighlightsActivity.cpp:81-87`, `:228`). This does not change
the result, but the screen shows an impossible state.

**Fix.** In the grid's `retireTag`, reset the grid's filter when it names the retired id, as
`dropRetiredFilter` does. Then All is inverted.

### m5 — Deleting `FilterRows` drops the test for "never retire through All/Unlabelled" with no replacement

**Problem.** The `FilterRows` tests (`test/tag_rows/TagRowsTest.cpp:58-72`) exist because an
adversarial review found an off-by-one row bug (`TagRowMapping.h:3-7`, `:25-27`). The new mapping
from `Candidate.slot` to `activeIds[slot]` to a chip id is the same kind of offset. Nothing
host-side pins that a non-Tag candidate never yields a tag to retire. The All chip's id is
`study::UNLABELLED` (`HighlightsActivity.cpp:147`). `StudyStore::retireTag` refuses UNLABELLED
(`StudyStore.h:112-117`), so the risk is small, but the guard should be tested, not assumed.

**Fix.** Add a Candidates test: every `Kind::Tag` candidate's `slot` is less than
`activeIds.size()` and maps to the id in that slot, and no All or Unlabelled candidate is
`Kind::Tag`.

### m6 — Citation drift

- `minTouchSize` is at `FreeInkUICore.h:637`, not `:638`, which is `rowHeight`. The research note
  has the same error.
- The no-lock comment is at `HighlightsActivity.h:133-134`, not `:127-133`.
- `ChipEntry` is about 44 B, not 40 B, once `kind` and `id` are counted. 202 entries is about
  8.9 KB, which is above the 4096 B limit where this build places allocations in PSRAM, so the
  larger figure does no harm.

Fix the three figures in place.

VERDICT: CLEAR
