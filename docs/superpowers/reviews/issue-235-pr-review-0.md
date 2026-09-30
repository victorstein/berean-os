Tier: standard

# PR #236 review, pass 0: Bible-only tag actions (issue #235)

Reviewed `git diff main...HEAD` at `301e043b` against issue #235, the spec
(`docs/superpowers/specs/2026-09-30-issue-235-design.md`) and the plan
(`docs/superpowers/plans/2026-09-30-issue-235-plan.md`). I ran the `ReaderMenuModelTest` and
`ReaderEntryIntentTest` host suites locally, and all 23 tests passed.

## Intent

**Acceptance criteria**

- *Non-Bible: both entries visible, each opens the Bible's tag list, and no passage is written.*
  Met. `ReaderMenuModel::build` shows `HIGHLIGHT_PASSAGE` and `HIGHLIGHTS` whenever
  `tagTarget(...) != Hidden` (`src/activities/reader/ReaderMenuModel.h:89,95,99`). In a non-Bible
  book with a Bible, both menu cases call `openBibleTags()` and return
  (`src/activities/reader/EpubReaderActivity.cpp:841-856`). `openBibleTags` makes the same
  `goToReader(..., ReaderEntryIntent::of(Kind::Tags))` handoff that Home uses (`:350-353`). I
  checked the write path. `PassageSelectActivity` is constructed in only one place (`:395`, inside
  `openHighlightPassage`), and `STUDY.addPassage` is called only from
  `PassageSelectActivity.cpp:355`. The new guard at the top of `openHighlightPassage` (`:370-374`)
  therefore stops every route that could write a passage for a non-Bible book, including
  `openHighlightPassageAt` (`:323`).
- *In the Bible, unchanged.* Met. `tagTarget` returns `ThisBook` whenever `isBible`, so the menu
  cases fall through to `openHighlightPassage()` and `openHighlights()` as before. The touch
  long-press keeps its `openHighlightPassageAt` call behind `isBible()` (`:514-518`).
  `route(Tags, true)` is still `Highlights`.
- *Host tests: Bible and non-Bible, with and without a resolvable Bible.* Met. The model is covered
  by `TagTargetRule` (all 8 inputs), `NonBibleWithABibleKeepsBothTagEntries`,
  `NonBibleWithoutABibleHidesBothTagEntries`, `BibleIgnoresBibleReachable`,
  `BibleReachableDefaultsToHidden`, and the flag sweep widened to 128 combinations
  (`test/ui_layout/ReaderMenuModelTest.cpp:138-160,177-222`).
- *No Bible found: hide both entries outside the Bible and log it.* Met: `bibleReachableForTags`
  logs once per reader (`EpubReaderActivity.cpp:328-343`), and `tagTarget` then returns `Hidden`.
- *Reading position saved on the handoff.* Handled by the existing exit path, as spec A12
  describes. `goToReader` takes the path by value (`src/activities/ActivityManager.h:86`), so the
  member string can safely outlive the reader being replaced.
- *Keep the Bible lookup the launcher's.* Met. The lookup moved into `BibleFinder` with its
  registry, card scan and recents order unchanged. The launcher's only behaviour change is the
  default `exclude` argument, which is a no-op.

**Scope**

The PR drops nothing the issue asked for. Two changes go beyond the issue's letter, and the spec
and the PR body name both:

- A4: a touch long-press on a word does nothing outside the Bible. The issue talks about the menu
  entries and the Tag quick action, not the word gesture, and the spec gives a sound reason for
  it.
- A6: `route(Tags, false)` becomes `None`, which reverses #221's
  `TagsOpenTheHighlightsEverywhere`. It removes the same bug the issue targets, a Tags intent
  listing a non-Bible book's own passages, and it only matters when the recents step guesses the
  wrong book. The PR body has a whole section on it ("Reverses a #221 decision"), so the human sees
  it before merge. I don't count it as a finding.

**Tests vs. implementation.** The model tests exercise behaviour, meaning which items appear for
which inputs. They don't mirror how the code is written. The activity dispatch has no host test
because it links Arduino and the HAL. The spec and plan say so, and device checks 1-6 in the PR
cover that gap.

**Plan divergence.** The code follows plan Tasks 1-8 closely. The one gap: step 7.4 says to delete
the includes the lift leaves unused, and the PR doesn't delete them or explain why (finding 1).

## Quality

- **Mirrors existing patterns.** `BibleFinder` is a namespace of free functions doing card I/O next
  to a pure rule header, following the `CardBooks` model. Its bodies were moved from
  `LauncherActivity`, not rewritten. The dispatch-then-`return` in `onReaderMenuConfirm` follows
  the `GO_HOME` case. The `BOARD_HAS_PSRAM` constant was lifted to file scope instead of being
  duplicated.
- **Naming and structure** match their neighbours: `TagTarget` / `tagTarget` sit beside `Inputs`
  and `build`, and the `ERS` log tag is used throughout.
- Switching the `Inputs` construction to designated initialisers
  (`EpubReaderMenuActivity.cpp:112-119`) is a real safety gain. Inserting a `bool` field would
  otherwise have silently shifted every later argument by one.
- **Comments** give reasons, not narration: the fail-closed default, the `exclude` rationale, and
  the Bible-only guard.
- **Error handling** has the established shape: `LOG_ERR` + return at the guard, and
  `LOG_INF`/`LOG_DBG` on the lookup and the no-op paths.
- **Dead code.** The lift left includes behind in the launcher (finding 1). Nothing else is dead or
  commented out.
- `EpubReaderActivity::tagTarget` checks `!isBible()` before calling `bibleReachableForTags()`,
  which checks it again. The spec asked for this form, and it is harmless.

## Findings

1. **MINOR: stale includes left by the lift, against plan step 7.4.**
   `src/activities/launcher/LauncherActivity.cpp:11` (`<algorithm>`), `:42`
   (`study/PubKeyRegistry.h`) and `:44` (`util/CardBooks.h`) have no users left. Evidence:
   `grep -n "CardBooks::\|PubKeyRegistry::\|std::find_if"` and the plan's own `std::(find|...)`
   grep both return nothing on the file. Plan step 7.4 (plan lines 299-304) says to delete exactly
   these three when those greps come back empty. The same is true of `<HalStorage.h>` (`:5`):
   the moved `findBibleInRecents` held the file's only `Storage.` call. In
   `src/activities/launcher/LauncherActivity.h`, `<optional>` (`:4`), `<vector>` (`:6`) and
   `"RecentBooksStore.h"` (`:9`) served only the two static declarations that were removed.
   Delete the `.cpp` includes the plan names, and `<HalStorage.h>`. Trim the header's includes too
   if the build stays green without them.

VERDICT: CLEAR
