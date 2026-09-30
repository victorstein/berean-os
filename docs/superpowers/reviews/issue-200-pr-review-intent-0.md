Tier: heavy

# PR #217 intent review, pass 0 (issue #200)

- **Scope:** intent only. I checked the PR against issue #200, the spec
  (`docs/superpowers/specs/2026-09-30-issue-200-design.md`) and the plan
  (`docs/superpowers/plans/2026-09-30-issue-200-plan.md`).
- **Diff base:** `f855887f`, the merge of `main` into the branch. The local `main` ref is stale, so
  `git diff main...HEAD` wrongly includes #212's chip rewrite.
- **Evidence:** every `file:line` below was read on `7621596b`.

## Findings

### MINOR 1: under "Tags here", the tag chip counts cover the whole publication, not the chapter

- **Evidence:** `HighlightsActivity::rebuildChips` counts `STUDY.passages()` and ignores `spineFilter_`
  (`src/activities/reader/HighlightsActivity.cpp:134`). Meanwhile `computeVisibleIndices` narrows the rows to the
  chapter (`:271-278`).
- **What the user sees:** when the list opens from **Tags here**, the header reads "Isaiah 40" and there are 3 rows,
  but the chips read "All 37", "hope 12" and so on. A chip for a tag that has no passages in this chapter still
  appears, and tapping it gives an empty list.
- **Why this is not a silent reduction:**
  - The chips came from #212, which merged after the spec was written.
  - The PR body lists this under "Known limitation, for a follow-up".
  - Spec A-14's actual requirement holds: the chapter filter is ANDed with the tag filter at every rebuild site
    (`:43`, `:197`, `:230`, `:530`, `:580`).
- **Fix, either one:**
  - Inline: count only over the spine-filtered candidates when `spineFilter_` is set.
  - Or file the follow-up issue before merge, so the limitation is tracked somewhere other than a PR body.

### MINOR 2: PR body V-6 says the count is per page, but it is per chapter

- **The PR body's V-6:** "The count on Tags here matches the passages highlighted on the page."
- **What the code counts:** every passage that `STUDY.passagesInDocument(currentSpineIndex)` resolves in the spine
  document, not only those on the visible page (`src/activities/reader/EpubReaderActivity.cpp:1401-1405`).
- **What the other documents say:** spec V-6 ("the chapter's painted passages", spec line 507) and the user guide
  ("the passages you tagged in this chapter") both describe it correctly.
- **Risk:** a tester following the PR body, in a chapter with passages on several pages, will see a count higher than
  the page shows and report a bug that does not exist.
- **Fix:** reword V-6 in the PR body to say "in this chapter".

## Issue acceptance criteria

| Criterion | Status | Evidence |
|---|---|---|
| Every item is reachable without scrolling, and the page stays visible above the sheet | Met (host); device V-1 | `ReaderMenuSheetLayoutTest.cpp:44-53` fits 4 + 11 with 6 rows per column. No list scrolls: `navigateButtons` registers release steps only (`EpubReaderMenuActivity.cpp:364-369`), and the rows use an explicit `rowHeight` with no scroll indicator (`:428-433`). The page is restored with `writeFramebufferRegion` (`:444-445`). |
| PSRAM returns to baseline; logs before, while open and after; internal heap unchanged | Implemented; device V-2 | `logHeap` prints internal free heap and `MALLOC_CAP_SPIRAM` (`:193-196`). It logs `before` and `open` in `decideMode` (`:232,265`) and `closed` after `reset()` in `onExit` (`:168-171`). The ~21.6 KB allocation goes through `makeUniqueNoThrow` (`:253`). |
| Non-Bible books keep all their items, including Go to % | Met | `ReaderMenuModel.h:93`. `ReaderMenuModelTest.cpp:135-152` checks all 64 flag combinations against a copy of the old `buildMenuItems` (checked against `f855887f:EpubReaderMenuActivity.cpp`). |
| Snapshot OOM falls back | Met, as amended by d1 | `EpubReaderMenuActivity.cpp:255-260` handles a null allocation or a 0-byte read with `LOG_ERR` and Cleared mode. The dev-only `READER_MENU_FORCE_SNAPSHOT_OOM` at `:250` exercises it (V-7). |
| No ghosting across ten cycles | Device only (V-3) | The first OverPage paint is FAST and the first Cleared paint is HALF (`:458-460`). |

## Spec requirements, checked for the hard half as well as the easy one

- **A-1 to A-3 (region snapshot sized for either orientation):**
  - `snapshotBytes` implements the orientation-proof `max` (`ReaderMenuSheetLayout.h:113-117`).
  - T-2 checks both copy shapes for 1 to 14 rows at two widths (`ReaderMenuSheetLayoutTest.cpp:138-153`).
- **A-5 (handoff):**
  - `openReaderMenu(true)` comes only from the reading surface (`EpubReaderActivity.cpp:525,541`).
  - All six sub-screen reopen sites pass `false` (`:682,737,754,766,780,807`). A grep finds no other callers.
  - `ActivityManager` pushes without clearing (`ActivityManager.cpp:139-161`).
- **A-7 and A-25 (degrade-only mode):** every reason the spec names is present and logged at `LOG_DBG`
  (`EpubReaderMenuActivity.cpp:233-264`): `rotated`, `not-on-page`, `night`, `no-fb`, `no-fit` and `oom`.
- **A-9 (rotation):** under `RenderLock`, the rotation handler frees the snapshot, sets `rotated` and resets the mode
  (`:280-293`).
- **A-10 to A-13 (model):** the tile set, tile order, row order and clamping match the spec's tables.
  - Tests: `ReaderMenuModelTest.cpp:65-131,154-168`.
  - The model also gates Tags here on `hasHighlights` (`ReaderMenuModel.h:84`). That is equivalent to the spec,
    because the count is 0 unless highlights are loaded, and the test pins it.
- **A-14 (count and target):**
  - The count comes from the existing render-task loop (`EpubReaderActivity.cpp:1405`) and resets in `renderBook`
    (`:1212`).
  - It is read under `RenderLock` at menu open (`:271-275`).
  - `TAGS_HERE` opens `HighlightsActivity` with the spine filter and the reference as its title (`:822-825`,
    `HighlightsActivity.cpp:255`).
  - All five rebuild sites compute unlocked and swap under the lock.
- **A-17 to A-23 (rendering, routing, closing):**
  - The opaque plate and rule are drawn at `:450-451`.
  - Hits are registered newest-wins: page close, then the plate guard, then the controls (`:399-409`).
  - Tiles are hand-drawn with a 3 px border when selected and 2 px otherwise (`:371-389`).
  - Each column has explicit props (`:420-435`).
  - The ways to close are the page tap and the ✕ (`ACTION_CLOSE`, `:165,174-178`), Back (`:350-353`) and Home
    (`:188-191`).
- **A-21 (row values):** Night mode and Frontlight are toggles, Auto turn shows a number or "–", and Tags here shows
  its count (`:122-161`).
- **A-26 (icons):**
  - The manifest adds four entries.
  - The `listIcons.h` diff removes only the header count line, so the existing 26 definitions are unchanged.
  - The orphaned `search24.h` and `search32.h` are deleted.
- **Hand-offs:** the translation keys match the spec's list, including Spanish `STR_TAG`. `test/CMakeLists.txt` is
  untouched.

## Divergence from the issue and plan

- **Changes from the issue's wording, all settled upstream:** each of these is recorded in the spec's revision
  history, from reviewed spec passes or from the owner's decision d1, so none is silent:
  - `readFramebufferRegion` of the page band (~21.6 KB) replaces the issue's 48 KB `storeBwBuffer`;
  - the fallback is the same sheet on a cleared screen, not the old full-screen scrolling list;
  - Tags here is a label with a value slot, not "Tags here (n)".
- **Plan divergence:** the code for Task 6 matches the plan's listing. All four plan-level departures are explained in
  the PR body:
  - `handleCustomInput` returns `true` only on dispatch;
  - the snapshot is 21,600 B;
  - the icons are spliced in;
  - the translation keys are edited in the branch.

  The post-plan merge of #212 and its effect on the filter sites is also explained.
- **Scope expansion:** none. The deleted orphan icons, the toggle switches and the chapter filter in
  `HighlightsActivity` are all in the spec. The user-guide edit documents the change.

## Tests

- **`ReaderMenuModelTest`** tests behaviour against an independent oracle, a transcribed copy of the old item builder
  run over all 64 flag combinations. It does not restate the implementation.
- **`ReaderMenuSheetLayoutTest`** asserts the properties that matter:
  - fit and no-scroll;
  - bottom anchoring;
  - containment and no overlap;
  - even tile spacing;
  - a second screen size;
  - degradation to not fitting over the page, then not fitting at all;
  - snapshot capacity in both orientations.

  One test pins exact pixel arithmetic (`:51`), but only as an addition to the property checks.
- **Not covered on the host:** snapshot mode selection, routing and the refresh choice. All three are rightly left to
  device checks V-1 to V-8.

VERDICT: CLEAR
