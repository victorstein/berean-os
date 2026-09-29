Tier: standard

# PR #189 review 0 — open Select chapter at the current book's chapter grid (issue #187)

Reviewed: `gh pr diff 189` (commits `25438088`, `9220cabe`, `0b57f6fb`, `69897b7f` over `main`),
against issue #187, the spec `docs/superpowers/specs/2026-09-29-issue-187-design.md` and the plan
`docs/superpowers/plans/2026-09-29-issue-187-plan.md`. Host suite re-run locally:
`ctest --test-dir build/test -R BibleEntry` gives 15 of 15 passed.

## Intent

**Acceptance criteria.**

1. *Opens on the book's chapter grid with the current chapter selected and its page shown.*
   Met. `EpubReaderActivity.cpp:729` now passes `spineIdx`. `enterAtPosition`
   (`BibleNavigationActivity.cpp:157-181`) classifies it, loads that book's chapters and calls
   `enterLevel(Level::Chapter, row)`. On the fresh activity `grid` is `{}`
   (`BibleNavigationActivity.h:98`), so `enterLevel` sets `nav.visibleRows = 1`
   (`BibleNavigationActivity.cpp:227`). The first `buildGrid` then sees
   `nav.visibleRows != grid.cellsPerPage()` and re-pages to
   `pageStartFor(nav.selected, …)` (`:444-446`). `pageOfIndex` guards `cellsPerPage <= 0`
   (`NumberGridLayout.h:56-59`), so the zero-geometry call in `placeSelectionLocked` is safe. The
   Psalm 119 page is left to device check 2, which is correct because it depends on geometry.
2. *Back goes to the book grid with the current book selected, and a second Back cancels.* Met with
   no new code. `selectedBook` is set before `enterLevel` (`:179-180`). Back from Chapter is
   `enterLevel(Level::Book, selectedBook)` (`:383-384`), and Book cancels (`:380-381`). The
   unbuilt-book-layout path is sound: `enterLevel` stores `nav.selected = selectedBook` under
   `listCount() == bookCount`, `placeSelectionLocked` returns early at `last < 0` (`:294-295`), and
   `buildGrid` pages from `BookGrid::pageOf(bookLayout, nav.selected)` once the layout exists
   (`:435`). The swipe reaches the same `onBackButton` through `UiListActivity::handleButtons`
   (`UiListActivity.cpp:47-49`). Device check 3 covers it, including Revelation on the New
   Testament page.
3. *Falls back when the position can't be mapped.* Met. Front matter, the book-nav page, and
   Genesis's outline return `None` (`BibleEntryPosition.h:43`). Any other outline, and the
   appendices, miss in `chapterRowFor` and return with the navigator untouched (`:173-177`).
   `enterLevel` is never called on a miss, so the state is exactly today's. A failed `loadChapters`
   logs with `LOG_ERR` and falls back too. The single-chapter book and nav-page case (`SelectBook`)
   goes beyond "fall back", because it preselects the book. The spec decided that explicitly as A3,
   and the issue's wording ("falls back to today's book-level entry") still holds for the level.
4. *Reuses `bookTargetSpine` / `chapterSpine`, no second SD sweep, no new persistent state.* The
   book comes from `bookTargetSpine` with no I/O. The one `loadChapters` is the load the owner's
   chosen chapter grid can't be drawn without. The PR description raises the extra load as a
   decision in its own section, with the Back-to-another-book and outline/appendix cases named,
   exactly as the criterion asks. No persistent state was added.
5. *Host test for spine → (book, chapter), including the five single-chapter books.* Met.
   `test/number_grid/BibleEntryPositionTest.cpp` round-trips all 1,189 chapters over the measured
   NWT layout (`EveryChapterOfEveryBookRoundTrips`), checks each single-chapter book
   (`EachSingleChapterBookSelectsItself`) and the space past one (`PastASingleChapterBookNeedsNoLoad`),
   and checks the outline, appendix, front-matter and degenerate inputs. The fixture is page numbers
   only, which satisfies the public-fixture rule.
6. *Device check.* Listed as check 1 and check 3 in the PR body, including the left-edge swipe the
   owner relies on.

**Spec.** Every architecture item is implemented as written: the header's API and `classify` order
(§1), the constructor, member, `enterAtPosition` placement after `loadBooks` with the font-cache clear
still first, and the class-comment sentence (§2), the one-line reader change (§3), the log strings
in the error table, the test cases 1–11, the third executable in `test/number_grid/CMakeLists.txt`,
and the `USER_GUIDE.md` §7 paragraph plus the "scrolling list" → "paged grid" correction.

**Scope.** Nothing beyond the issue and spec. The non-Bible branch, verse search, `onBackButton` and
`activateIndex` are unchanged, and there is no setting, persisted state or input-layer change.

**Tests.** They exercise behaviour against measured data rather than restating the code. The
chapter-list helper builds `chapterSpine` as `BOOK_TARGET + 1 + k`, but that is the measured layout
("gaps [] 0" in the research), not the implementation's rule. The implementation never assumes
contiguity and confirms by equality. The `bookFor` edge tests (unresolved target, unsorted, tie)
pin A2's stated contract. The activity glue in `enterAtPosition` isn't host-tested. That is
consistent with the repo, since no activity is, and the plan states why (Step 3, "No host test in
this step").

**Plan divergence.** None of substance. The header, activity and test code match the plan's listings.
The only differences are clang-format whitespace (the `CHAPTERS` column alignment and the one-line
`classify` signature), which the `style: clang-format` commit accounts for.

## Quality

- **Pattern.** It mirrors what already exists. The constructor-plus-`onEnter` preselection follows
  `EpubReaderChapterSelectionActivity`, which already takes the same `spineIdx` from the same
  handler. The pure header namespace with a host suite in `test/number_grid/` follows
  `NumberGridLayout.h` and `BookGridLayout.h`, and its top comment reuses their "free of FreeInkUI,
  Arduino…" wording. The level switch goes through the existing `enterLevel`, so there is no second
  way to change level or place a selection.
- **Naming and structure.** `BibleEntry::{Kind, Entry, bookFor, classify, chapterRowFor}` reads
  alongside `NumberGrid::` and `BookGrid::`. `entrySpine` is `const int`, set in the initialiser
  list. `enterAtPosition` sits beside the other load helpers.
- **Comments.** They explain why: "a guess from spine order until the caller confirms", "Unresolved
  (< 0) targets never win", "anything past it is an outline or other page". None restates the next
  line, and there is no dead or commented-out code.
- **Error handling.** It matches the file. `LOG_ERR("BNV", …)` covers a failed load, as at `:271`.
  `LOG_DBG` covers an expected non-chapter miss, as `openVerseList` does at `:249`. Every failure
  path leaves the navigator in today's state.
- **Resources.** There is one 4 B member. `loadChapters` keeps its existing `makeUniqueNoThrow` and
  null check, and there are no new allocations or strings.
- **Concurrency.** `loadChapters` in `onEnter` writes `chapterSpine` / `chapterCount` without
  `RenderLock`. Until `enterLevel` switches under the lock, the level is Book, where `listCount()`
  reads only `bookCount` (`:185-194`). A render caught mid-load therefore reads nothing the load is
  writing. This is the same exposure `activateIndex` already has on the loop task.
- **Duplication.** None found. No existing helper maps a spine to a Bible book. `Epub::getTocIndexForSpineIndex`
  was considered and rejected in the spec (D1) for a reason that holds.

## Findings

No BLOCKER, MAJOR or MINOR findings. The mapping is correct over the full measured layout. Every
fallback leaves today's state intact. The one real cost, the extra `loadChapters` on the
Back-to-another-book path, is disclosed in the PR as the criterion requires, and it is for the owner
to accept, not a defect.

Only the human tester can verify the following, and the PR already lists all of it: the device
checks 1–8, especially the Psalm 119 grid page (check 2), the swipe from a never-built book layout
landing on the right testament page (check 3), and the heap across three open/close cycles
(check 8).

VERDICT: CLEAR
