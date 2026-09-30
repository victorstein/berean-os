Tier: heavy

# PR #217 code quality review, pass 0 (#200, reader menu sheet)

Scope: `git diff f855887f..HEAD`, which covers 8 commits on `feature/200-reader-menu-sheet`. I read every source,
header and test hunk, and checked them against the siblings they mirror: `UiListActivity`, `OptionPopup`,
`PassageSelectActivity`'s framebuffer snapshot, `StudyStore::passagesInDocument` and the `test/ui_layout` suites.

Host check: `ReaderMenuModelTest` (12 tests) and `ReaderMenuSheetLayoutTest` (11 tests) build from `test/` and pass.

## Findings

No BLOCKER and no MAJOR. Four MINORs, all fixable inline.

### MINOR 1: two i18n keys left orphaned by the menu rewrite

The old `buildMenuItems` was the only user of `STR_TOGGLE_BOOKMARK` and `STR_SEARCH_VERSES`. The new `labelFor`
(`src/activities/reader/EpubReaderMenuActivity.cpp:27-65`) uses `STR_MARK` and `STR_SEARCH` instead. A `grep -rnw` over
`src/` and `lib/` `*.cpp`/`*.h` now finds no users of either key, but both are still in the YAML:

- `STR_TOGGLE_BOOKMARK` is in every translation file (for example `lib/I18n/translations/english.yaml:218`).
- `STR_SEARCH_VERSES` is in `english.yaml:21` and `spanish.yaml:20`.

`gen_i18n.py` still compiles both into the flash string tables. The PR removed its other orphans (`search24.h` and
`search32.h`), so leaving these two is inconsistent with that cleanup. `STR_AUTO_TURN_PAGES_PER_MIN` is still used, as
the popup title (`EpubReaderMenuActivity.cpp:301`), and `STR_HIGHLIGHT_PASSAGE` is still used by
`PassageSelectActivity.cpp:313`, so those two stay.

### MINOR 2: a member comment is now detached from its member

In `src/activities/reader/HighlightsActivity.h:167-173`, the comment "Indices into STUDY.passages(), most-recent-first,
filtered by filterTagId_. See computeVisibleIndices()'s comment…" describes `visibleIndices_`. The two new members,
`spineFilter_` and `title_`, were inserted between the comment and `visibleIndices_`, so it now reads as documenting
`spineFilter_`.

Fix: move the new members above the comment, next to `filterTagId_`. The comment could also mention the spine filter.

### MINOR 3: sort and dedup that cannot do anything

`src/activities/reader/HighlightsActivity.cpp:62-63` sorts the candidates descending and then runs
`std::unique`/`erase`. `StudyStore::passagesInDocument` (`src/study/StudyStore.cpp:211-262`) loops `i` upward over
`passages_` and pushes at most one `PaintedPassage` per `i`. So `painted` is already strictly ascending with no
duplicates:

- the dedup is dead;
- the sort only reverses the list.

The dedup also suggests to the next reader that a passage can paint twice, which it cannot.

Fix: replace both lines with `std::reverse(...)`, or walk `painted` backwards, which mirrors the unfiltered branch's
reverse walk at `:67-68`. Then drop `#include <functional>` (`:9`).

### MINOR 4: a test comment narrates review history

`test/ui_layout/ReaderMenuSheetLayoutTest.cpp:140` ends with "(review 1, MAJOR 1)". The project's comment rules say
that process narration belongs in the commit message, not the code. The rest of that comment states the invariant
correctly and should stay.

## Checked and sound

- **Pattern reuse:**
  - The snapshot follows `PassageSelectActivity`'s pattern: `makeUniqueNoThrow`, then `readFramebufferRegion` with a
    capacity and a 0-return check, then `writeFramebufferRegion`. Both functions already exist
    (`GfxRenderer.h:274-275`), so nothing new was added to the renderer.
  - The `ACTION_CHROME` gap guard mirrors `OptionPopup.h:86,218`.
  - Routing through `UiAppHost::routeTouch` inside `handleCustomInput` has the same shape as
    `EpubReaderPercentSelectionActivity.cpp:104` and `EndOfBookOptions.cpp:98`.
  - Row heights come from `ListRowHeight::resolve`, and the two `test/ui_layout` targets copy the existing
    `TypedReferenceTest` block.
- **Structure:**
  - The item model (`ReaderMenuModel.h`) and the geometry (`ReaderMenuSheetLayout.h`) are pure, host-testable
    headers.
  - The activity maps actions to labels and icons, and nothing else.
  - `using MenuAction = ReaderMenuAction` keeps every `EpubReaderActivity` call site unchanged.
  - The move of the `HighlightsActivity` filter from `rebuildVisibleIndices` to `computeVisibleIndices` was made
    consistently at all five call sites. Each one computes outside the lock and swaps under `RenderLock`, and the header
    explains why the lock must not be held.
- **Error handling:** it follows the established shape, `LOG_ERR` then a fallback:
  - an over-capacity model: `EpubReaderMenuActivity.cpp:152-155`;
  - a sheet taller than the screen: `:211-213`;
  - a failed snapshot allocation or read: `:255-260`.

  The `#ifdef READER_MENU_FORCE_SNAPSHOT_OOM` hook at `:250` is a development-only switch the spec asks for (A-2, V-7).
  It is not dead code.
- **Tests:**
  - `ReaderMenuModelTest` checks the new model against an independent oracle: a copy of the old builder, run over all
    64 flag combinations.
  - `ReaderMenuSheetLayoutTest` asserts properties: containment, no overlap, even spacing, bottom anchoring,
    degradation, a second screen size, and snapshot capacity in both orientations. Only one test pins exact pixel
    arithmetic.
  - Neither suite restates the implementation.
- **Not raised as findings:**
  - The 24 px variants of `grid`, `search` and `tag`, and the 32 px `close`, are unused. `gen_icons.py` emits every
    listed size for every icon, so this is the generator's convention.
  - The hand-built `ListRowHeight::Inputs` at `EpubReaderMenuActivity.cpp:205-211` is the fourth copy of that block,
    after `UiListActivity.cpp:129`, `UiTabListActivity.cpp:80` and `HighlightsActivity.cpp:323`. It follows the
    existing pattern. A shared helper would be a separate refactor, not something this PR must fix.

VERDICT: CLEAR
