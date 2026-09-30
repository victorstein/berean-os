Tier: standard

# PR #248 review 0: perf: make go to open the chapter grid fast (#238)

Reviewed at head `2ffc7813` against issue #238, the spec
(`docs/superpowers/specs/2026-09-30-issue-238-design.md`) and the plan
(`docs/superpowers/plans/2026-09-30-issue-238-plan.md`). The three new suites were rebuilt and run
here: `BookLabelsTest` 5/5, `SpineSearchTest` 13/13, `BibleNavCacheTest` 8/8.

## Intent

**Acceptance criteria.**

- *Measured breakdown, before and after, in the PR.* **Not met yet.** The instrumentation is in
  (`EpubReaderActivity.cpp:934-935`, `BibleNavigationActivity.cpp:91-98,105-110`,
  `ReaderActivity.cpp:55,60`, `LauncherActivity.cpp:411`), and the "before" build is its own commit
  (`927b2955`), as the plan's Task 1 requires. But every cell of the PR's table says "pending".
  Spec A12 and plan Task 10 foresaw this: no device was attached, so the owner runs it. That is an
  acceptable way to *open* the PR. It is not a reason to close the issue, and the PR body ends with
  `Closes #238` (finding 1).
- *A second Go to is visibly faster; the caching logic is host-tested.* Met in code, but only a
  device can confirm "visibly faster". On a warm menu Go to, `loadBooks` returns from
  `adoptBooks` with no SD read (`BibleNavigationActivity.cpp:116-121`), and `loadChapters` does the
  same for the stored book (`:209-217`). `BibleNavCacheOf` is host-tested
  (`test/number_grid/BibleNavCacheTest.cpp`): the empty state, a zero-book commit refused (A8),
  withdrawal on refill, the chapters kept only for their own book, and the copy semantics.
- *No regression in free internal heap, logged at Go to.* The existing `LOG_INF` at
  `BibleNavigationActivity.cpp:99-102` is the check. The only new internal allocation is the
  `shared_ptr` control block. The 6 KB cache sits above the 4,096 B always-internal threshold, and
  a `static_assert` pins that (`BibleBookIndex.h:26`). The number itself is pending with the device
  run.

**Spec requirements.**

- Goal 2, the long walks removed. Book spines now come from the TOC entries `joinToc` already
  reads (`BibleBookNameTable.cpp:54-60`). `TocEntry::spineIndex` is resolved at `book.bin` build by
  an exact href match against the spine (`BookMetadataCache.cpp:412-443`), so it names the same
  item the old walk found. The fallback walk runs only for targets still unset
  (`BibleNavigationActivity.cpp:143-147`). Chapters are walked from the book's own nav page,
  wrapping to the start (`:239-241`). Both functions match every unset slot, so duplicate targets
  both resolve (spec MINOR 2).
- A1, A4, A5, A7, A8: implemented as written. The cache is lazily allocated only for a Bible, and
  an OOM logs `LOG_ERR` and leaves Go to uncached (`EpubReaderActivity.cpp:949-959`). Only the last
  book's chapters are kept, the marks are recomputed on a hit, and a failed load stores nothing.
- A9, the Recent chips: implemented, and done correctly. `setAbbreviations` is now shared by
  `load()` and the navigator, so the cached table's `abbreviationFor` gives exactly what `load()`
  gave: the page label, or `""` (`BibleBookNameTable.cpp:39-43,64-66`; `BookLabels.h:21-33` keeps
  the old "rows past the last label stay empty" behaviour). The chips never fill the cache
  (`EpubReaderActivity.cpp:1002-1007`).
- A10, the masthead band cache: correctly left unbuilt. Its gate is a device number, and the PR
  says so.
- A11: the milestones are `LOG_DBG`, compiled out of release.

**Scope.** No expansion: every changed file is in the plan's `FILES:` lines, and the Task 9 files
are untouched. No silent reduction beyond the device numbers already covered above.

**Plan divergence.** The code follows the plan task for task. The plan's three stated deviations
from the spec are the only divergences, and the plan explains each one:

- the label test targets `BookLabels.h` because `Epub.h` has no host stub;
- the cache is filled in place through `booksToFill`/`commitBooks`, because the table is too big
  for a stack;
- the band mechanism (unbuilt here).

**Tests.** They test behaviour. `SpineSearchTest` checks, with a counting getter, where the walk
reads, how far it reads and when it stops, not just what it returns. `BibleNavCacheTest` is written
against a template seam with a fake book type, so it pins the cache contract without Epub. The
field-by-field copy in `copyBooksTo`/`adoptBooks` has no host test. The PR says so, and names the
device check that would catch a missed field.

## Quality

- **Patterns.** It follows existing patterns:
  - The header-only, Epub-free helpers under `test/number_grid` follow `BibleEntryPosition.h`.
  - The href getter is a function pointer plus a context, not a `std::function`.
  - The unique-to-shared hand-over copies what `loadBook()` does with the `Epub`.
  - `makeUniqueNoThrow` is null-checked with `LOG_ERR`.
  - Both `SpineSearch` functions match through the existing `BibleNav::filenameTail`, as
    `findTargetByHref` and `resolveFilenamesToSpineIndices` do.
- **Duplication.** `copyUtf8Truncated` was moved into `BookLabels.h`, not copied. The navigator's
  size constants now alias `BibleNavLimits`, and `static_assert`s tie those to
  `BibleBookNameTable` (`BibleBookIndex.h:21-23`). There is one real parallel structure: see
  finding 2.
- **Comments.** They are sparse and explain a *why*: the PSRAM threshold, "filled in place because
  … too large for a task stack", and why the chapter walk starts at the nav page. None restates the
  next line. There is no dead or commented-out code.
- **Error handling.** Every new failure path either keeps the existing `return false` shape or logs
  and degrades to the uncached path.

## Findings

1. **MAJOR.** The PR closes #238 before its first acceptance criterion can be met. The PR body's
   last line is `Closes #238`, while its "Measured breakdown" table is entirely "pending". The
   issue's acceptance criteria require "The measured breakdown, before and after, is in the PR"
   and "no regression in free internal heap, logged at Go to". The spec's A10 gate (the band
   cache, plan Task 9) and its A10b report both depend on the same device run. With hpipe merging
   a ready PR, the issue would auto-close with the measurements, the heap check and the A10 gate
   all unresolved, and nothing left open to prompt them. **Fix inline:** change the trailer to
   `Refs #238` (or `Part of #238`), keeping the issue open for the owner's device run, which
   either fills the table or opens Task 9. This changes no code and no decision: A12 already
   assigns the numbers to the owner.

2. **MINOR.** The book table now exists twice, kept in sync by hand. Spec A6 wanted "one
   `BibleBookIndex books` member" in the navigator, so that a cache hit is one assignment. Plan
   deviation 2 instead kept the navigator's separate members (`bookNames`, `bookTargetSpine`,
   `bookIsDirect`, `bookAbbrev`, `sectionTitle`, `sectionStart`, `sectionCount`, `bookCount`). It
   mirrors them field by field in `BibleBookIndex` (`BibleBookIndex.h:10-19`) and copies them both
   ways in `copyBooksTo`/`adoptBooks` (`BibleNavigationActivity.cpp:181-201`). The deviation is
   explained, and its stated reason, avoiding renames, is legitimate. But a field added to
   `loadBooks` later and missed in both helpers will make the warm Go to differ silently from the
   cold one, and no host test can catch it. Acceptable as is. Folding the members into one
   `BibleBookIndex` member would remove the class of bug if this code is touched again.

VERDICT: CLEAR
