Tier: standard

# Issue #187 spec review, pass 0

Reviewed: `docs/superpowers/specs/2026-09-29-issue-187-design.md`, checked against issue #187
(`gh issue view 187 --repo victorstein/berean-os`), the research note
`docs/superpowers/research/2026-09-29-issue-187-research.md`, and the code at `db534492` (the tree
matches base `a4e2288d`: `git diff a4e2288d --stat` shows only the two docs).

## What was verified and holds

- **Measurements.** I re-ran `spine.py` on both EPUBs in the scratchpad. EN reports spine 3941 and
  ES reports 3937. Both report 66 books, booknav at spine 2, `bookTargetSpine` ascending,
  `gaps [] 0`, `range-test violations []`, first/last 29/1343, `toc covering-entry mismatches 0`,
  and direct books `[(30, 977), (56, 1272), (62, 1315), (63, 1317), (64, 1319)]`. `BT` and `NCH`
  are identical across the two languages. A second probe confirmed the spine numbers the test cases
  use:
  - Gen 32 is 30+31 = 61, and Rev 22 is 1322+21 = 1343.
  - Psalms has 150 chapters, with Ps 119 at spine 662 and Ps 150 at 693.
  - Spine 978 is `1001061331.xhtml`, which sits between Obadiah (977) and Jonah's nav page (979),
    so it is Jonah's outline.
  - Spine 80 is `1001061301.xhtml`, between Gen 50 (79) and Exodus's nav page (81).
- **Code citations in the navigator are accurate**:
  - `BibleNavigationActivity.h:47,53,54-56,78`
  - `.cpp:36-50` (onEnter), `:108-136` (loadChapters), `:125-130`, `:153-163`, `:188-204`
    (enterLevel), `:195`, `:236-238`, `:240`, `:259`, `:265`
  - `.cpp:350-351`, `:353-354`, `:405-407`, `:414-417`, `:511-512`
- **A9 (swipe = Back).** The swipe reaches the same path as a Back press:
  - `MappedInputManager.cpp:265` returns true for `wasReleased(Back)` on the back gesture, and
    `UiListActivity.cpp:48-49` calls `onBackButton()`.
  - The navigator's `handleCustomInput` consumes only Up/Down swipes (`.cpp:316-317`), so the
    left-edge swipe is not intercepted.
- **A10 (Back to an unbuilt book layout).** The code bears this out:
  1. `gridCellsPerPage()` is 0 before the first book build (`.cpp:166`), so `visibleRows` becomes 1
     (`:200`).
  2. `placeSelectionLocked` returns because `min(bookCount, coveredBooks=0)-1 < 0` (`:259`, `:265`).
  3. `nav.selected` was already set at `:195`.
  4. `buildGrid` then picks the page from `BookGrid::pageOf(bookLayout, nav.selected)` (`:405`), and
     `pageOf` is safe on any index (`BookGridLayout.h:104-110`).
  5. `drawFooter` uses the same `pageOf` (`:532`).
- **A11 (first chapter paint on the right page).** `pageOfIndex`/`pageFirstCell` return 0 for
  `cellsPerPage <= 0` (`NumberGridLayout.h:56-64`), and `buildGrid` re-pages when
  `nav.visibleRows (1) != grid.cellsPerPage()` (`.cpp:414-417`).
- **A2's -1 handling** is grounded in `Epub.h:84-88` ("-1 where absent").
- **Classification order.** `classify` checks `spine == target` before `bookIsDirect`, which is
  correct: it sends the five direct books to `SelectBook`, and every other spine in their range to
  `None`.
- **Robustness beyond NWT layout.** The book guess is always confirmed against the loaded
  `chapterSpine` (`chapterRowFor`), so a publication that breaks the range assumption misses and
  degrades to today's entry. It never opens a wrong chapter. That makes D1 safer than the spec
  claims for it.
- **Test registration facts.**
  - `add_subdirectory(number_grid)` is at `test/CMakeLists.txt:101`.
  - `ui-dev.md:22-27` names `test/CMakeLists.txt` as a shared file.
  - CI runs the host suite (`.github/workflows/ci.yml:145-151`), and the spec's build commands match
    CI's.

## Findings

### MAJOR 1 — the SD cost is misstated against the issue's acceptance criterion

**Claim.**

- Resources, "SD": "at most one extra chapter-nav stream per *Select chapter* (A4, A8)".
- Resources, "Heap": the entry-time load and a tap load "never both in one entry path unless the
  user goes Back and re-taps the book, exactly as today".
- A4 prices the miss path as "one chapter-nav stream (at most 13,940 B)".

**Problem.** Two parts of the accounting are wrong.

- **Each `loadChapters` is also a spine sweep, not only a stream.** It ends in
  `resolveFilenamesToSpineIndices` (`BibleNavigationActivity.cpp:131`). That is a forward pass over
  the spine (`Epub.cpp:963-977`), and each item costs two SD seeks plus a read of `book.bin`
  (`BookMetadataCache.cpp:508-524`, `getSpineEntry`; the header says the same at `Epub.h:84-87`).
  - The pass stops early once every filename resolves (`Epub.cpp:969`), so its length is the
    book's last chapter spine.
  - That is 80 entries for Genesis and 1,344 for Revelation. The Revelation case includes the
    appendix miss path in A4.
- **The "exactly as today" claim is false for the issue's own third row (Gen → Exod 3:1).**
  - Today that flow costs one load (Exodus).
  - Under this design it costs two: Genesis at entry, which is then thrown away, and Exodus on the
    tap.
  - The A4 miss paths likewise cost a full load that is thrown away.

The issue's fourth acceptance criterion says: "without a second SD sweep or new persistent state.
If it does need one, raise it as a decision." The spec is required to surface exactly this, and it
under-reports it.

The cost is intrinsic to the owner's decided UX. The chapter grid cannot be drawn without
`chapterSpine`, and nothing else in memory provides it. So this does not reverse a decision or need
a new owner judgment. It does need to be stated honestly and carried to the owner, as the
acceptance criterion asks.

**Evidence.** `BibleNavigationActivity.cpp:117,131`; `Epub.cpp:963-977`;
`BookMetadataCache.cpp:519-523`; issue #187, acceptance criterion 4 and the "Exod 3:1" row of the
UX table.

**Fix (inline).**

1. Rewrite Resources "SD" as follows: "one `loadChapters` per entry from a mapped position. That is
   a chapter-nav stream plus a spine-resolution pass up to the book's last chapter (≤ 1,344 spine
   entries). In the stay-in-book case it replaces the book-tap load. It is extra when the user goes
   Back to another book, and on A4 misses."
2. Correct the "exactly as today" sentence, and update A4's cost to include the sweep.
3. Add a line to the PR-description checklist that raises this to the owner under acceptance
   criterion 4, with the argument that the owner's chapter-grid decision makes it unavoidable.

### MINOR 1 — A8 claims "never" for the book-grid flash, which the class's own header contradicts

**Claim.** A8 says "the user never sees the book grid flash before the chapter grid".

**Problem.** The normal path defers the render correctly:

- The push runs `onEnter` (`ActivityManager.cpp:157-160`) before the notify (`:167-172`).
- The pop handler skips its own `requestUpdate` while a push is pending (`:131`).

But the render task drains a notification count (`:49`), so a notify still queued from an earlier
pass can render the new `currentActivity` while `onEnter` is mid-load. The navigator's header
already says so: "a render can catch it halfway" (`BibleNavigationActivity.h:69-73`). The effect is
cosmetic, since `enterLevel` switches under `RenderLock` (`.cpp:192`). "Never" is still an overclaim
that a device tester could disprove.

**Fix.** Change it to "normally", and cite `.h:69-73` for the stale-notify edge.

### MINOR 2 — the test could avoid the shared-file hand-off entirely

**Claim.** Registration puts the suite in a new `test/bible_entry/`. The implementer adds
`add_subdirectory(bible_entry)` locally, reverts it before committing, and the orchestrator applies
it.

**Problem.** `test/number_grid/CMakeLists.txt` already hosts two sibling suites for this activity's
pure headers, `NumberGridLayoutTest` and `BookGridLayoutTest`. It is not a shared file. A third
target there needs no shared-file line, no local add-and-revert, and no reliance on the orchestrator
applying a hand-off before CI runs host tests (`ci.yml:145-151`). If the hand-off is dropped, the
new suite silently never runs in CI.

**Fix.** Put `BibleEntryPositionTest.cpp` in `test/number_grid/`, add a third
`add_executable`/`gtest_discover_tests` block there, and drop the shared-file step. Alternatively,
keep the new directory but state why it is worth the hand-off.

### MINOR 3 — the reader line numbers are off by one

**Claim.** The spec cites `EpubReaderActivity.cpp:714` (the `SELECT_CHAPTER` case), `:715`
(`spineIdx`, including in A12), `:722` (`releaseSectionKeepingPosition`) and `:727` (the Bible
gate).

**Problem.** `grep -n` gives 715, 716, 723 and 728. The construction line `:729` and `:731` are
right. The spec's preamble promises that every cited line was read on this branch.

**Fix.** Renumber to 715, 716, 723 and 728.

### MINOR 4 — a front-matter label in test case 9 is inaccurate

**Claim.** Case 9 treats spine 28 as "front matter".

**Problem.** Spine 28 is `1001061300.xhtml` (`spine.py`: `before gen ['1001061103.xhtml',
'1001061300.xhtml']`). That is Genesis's outline page, the same family as spine 80 (Exodus's
outline). It still maps to `None`, because it precedes `bookTargetSpine[0] = 29`. The expected
result is right, but the label hides an asymmetry: Genesis's outline falls back through A5, while
every other book's outline falls back through A4 at the cost of a wasted load.

**Fix.** Relabel it "Genesis's outline (28)", and note the A5/A4 asymmetry in A4.

VERDICT: CLEAR
