Tier: heavy

# berean-o-7 (R22) PR review, code quality, pass 0

- PR: #263, `perf: skip the home verse scan on a same-day wake`. Base `main`, head `759e26ec`
  (matches the local branch).
- Scope: the PR's own commits after the `origin/main` merge `a07f1a63` (`fd2868eb..759e26ec`),
  reviewed as `git diff a07f1a63..HEAD`. That covers:
  - `lib/Serialization/{PersistableStore.h,SdPaths.h}`;
  - `src/activities/launcher/HomeVerse{,Doc,File}.{h,cpp}`;
  - `test/home_layout/*` and `test/sd_paths/SdPathsTest.cpp`;
  - the new `docs/file-formats.md` section.
  Main's merged commits were not reviewed.

## How this was checked

- **Siblings read in full, to compare against:**
  - `src/network/MeetingWeekCache.{h,cpp}`, the shell's declared model;
  - `src/util/PlacesDoc.{h,cpp}`, the Doc's model;
  - `src/PlacesStore.{h,cpp}` and `src/RecentBooksStore.cpp`, the other derived-budget stores;
  - `src/study/PassageFile.cpp` and `src/study/BibleSearchFingerprintFile.h`, the other `*File`
    shells;
  - `lib/Serialization/{PersistableStore.cpp,SaveBudget.h,TempAdoption.h,DocReadStatus.h}`.
- **Tests, built and run in a scratch build** (`cmake -S test -B <scratchpad>/build`):
  - `HomeVerseDocTest`: 14/14 passed.
  - `HomeVerseFileTest`: 8/8 passed.
  - `HomeLayoutTest`: 42/42 passed.
  - `SdPathsTest`: 8/8 passed.
  - I did not run a firmware build. CI on `759e26ec` is green: `Build x4pro`, `clang-format`,
    `cppcheck`, `unit-tests`.
- **A scratch probe, not committed** (`<scratchpad>/probe/OomProbe.cpp`). It links against the
  suite's own `HomeVerseFile`/`HomeVerseDoc`/`PersistableStore` objects and uses the
  `CountdownJsonAllocator` from `test/storage_io/PassageFileTest.cpp:132-148`. It confirmed three
  things:
  - `HomeVerseFile::save` allocates 6 times from a given allocator.
  - A failure at any of those 6 allocations returns 0 and leaves no file.
  - `load` on an allocator that fails at once returns `Unreadable`.
  So the code is right. Finding 1 is about the missing tests, not about behaviour.
- **ArduinoJson 7.4.2 source**, read-only:
  - `Strings/Adapters/RamString.hpp:90-95` adapts a `const char (&)[N]` as a static string of
    length `N - 1`. That confirms the cast comment at `HomeVerseDoc.cpp:123`.
  - `Json/TextFormatter.hpp:55-65` emits two bytes for each escape, and `\u0000` for NUL only.
- **Budget arithmetic**, recomputed by script. The wrapper literal at `HomeVerseDoc.h:21` is 106
  bytes. The largest entry `TheLargestEntryFitsTheBudget` builds serialises to 1,421 bytes,
  against the budget of 3,943.

## Findings

### MINOR 1: the injectable allocator and the `overflowed()` refusal have no tests

**Evidence.**

- The shell has two paths that depend on the allocator:
  - `HomeVerseFile::load` and `save` take an `ArduinoJson::Allocator*` (`HomeVerseFile.h:20`,
    `:25`), and `makeDocument` passes it on (`HomeVerseFile.cpp:16-18`).
  - `save` refuses an `overflowed()` document (`HomeVerseFile.cpp:53-56`). That is this store's
    "refuse, never truncate" guard for out-of-memory.
- Every call in `HomeVerseFileTest.cpp` passes `nullptr` (all 16 calls, `:48-114`). So the
  non-null branch of `makeDocument`, the PSRAM routing the PR claims (spec A13), and the
  `overflowed()` refusal are never exercised.
- The sibling with the same shape tests all three, with the allocator above:
  - `PassageFileTest.cpp:183-200`, `TheAllocatorOverloadParsesOnTheGivenAllocator`;
  - `:202-213`, `ALoadThatRunsOutOfMemoryFailsAndLeavesTheFile`;
  - `:215-227`, `ASaveThatRunsOutOfMemoryIsRefusedAndWritesNothing`.
- The spec's test list (spec :420-428) says why the over-budget refusal goes untested: no valid
  `Entry` can reach it. It says nothing about the out-of-memory refusal, which a failing allocator
  reaches directly.

**Why it matters.** The allocator parameter exists so the host suite can inject one, and the suite
never injects one. The PR's two resource claims would both break silently:

- if `makeDocument` dropped the allocator, the documents would no longer be in PSRAM;
- if the `overflowed()` check were deleted, a cut document would be written.

No test would fail in either case. The cost is low, because this file is a cache: a cut write
reads back `Malformed` and the next scan replaces it. That is why this is MINOR.

**Fix inline.** In `HomeVerseFileTest.cpp`, add a `CountdownJsonAllocator` like
`PassageFileTest.cpp:132-148`, and two cases:

1. `save` with a counting allocator writes the file, and `allocations > 0`. Then `remaining = 0`
   makes `save` return 0, with no file at `PATH` and no `PATH + ".tmp"`.
2. `load` on an allocator with `remaining = 0` returns `Load::Unreadable`, and `out.valid` is
   false.

The scratch probe above is this test, already passing against the branch.

### MINOR 2: the derived budget has no "under the shared default" guard, unlike its siblings

**Evidence.**

- Both other stores whose budget comes from `worstCaseBytes()` pin it below the shared ceiling at
  compile time:
  - `src/PlacesStore.cpp:58-59` and `src/RecentBooksStore.cpp:89-90` both have
    `static_assert(<Doc>::SAVE_BUDGET < persist::DEFAULT_SAVE_BUDGET, "a store whose fields are
    bounded must claim less than the shared default")`;
  - `PlacesDocTest.cpp:75-78` repeats the check as a test.
- `HomeVerseFile.cpp` and `HomeVerseDocTest.cpp` have neither. The budget is pinned only to its
  number (`HomeVerseDocTest.cpp:232`).
- This file needs the guard more than those two stores do. It is read through
  `readDocFromFileAdopting`, then `readDocFromFileChecked`, then `Storage.readFile`
  (`PersistableStore.cpp:124-128`), whose truncation cap is `SaveBudget.h:19`. The PR's
  resources section also relies on the file being "under 4 KB". Today that holds only because
  someone re-pins `3943u` by hand whenever the caps change.

**Fix inline.** In `HomeVerseFile.cpp`, as `PlacesStore.cpp:58-59` does, add:

```cpp
static_assert(HomeVerseDoc::SAVE_BUDGET < persist::DEFAULT_SAVE_BUDGET,
              "a store whose fields are bounded must claim less than the shared default");
```

Optionally, also add `static_assert(HomeVerseDoc::SAVE_BUDGET <= 4096, ...)`. That would make the
PR's "under 4 KB" claim hold at compile time.

## Checked, and not findings

- **The shape mirrors the repo.**
  - The Doc/File split, `FORMAT_VERSION`, the wrapper-literal comment,
    `worstCaseBytes()`/`SAVE_BUDGET` and "recompute it, do not tidy it" all follow `PlacesDoc.h`.
  - The read is `readDocFromFileAdopting` and the write is `writeDocToFileAtomic`, as in
    `MeetingWeekCache.cpp`.
  - The `restoreAttempted` gate follows `PlacesStore`'s `loadAttempted`/`ensureLoaded()`
    (`PlacesStore.h:16`, `PlacesStore.cpp:27-29`). So a query that loads lazily on first use has
    a precedent.
  - The nullptr-means-default allocator idiom is `PassageDoc::newJsonDocument`
    (`PassageDoc.h:64`) and `PersistableStoreBase::loadAdopting` (`PersistableStore.cpp:241-246`).
- **`ESCAPE_FACTOR = 6`, not the siblings' 2** (`HomeVerseDoc.h:32` against `PlacesDoc.h:35` and
  `RecentBooksDoc.h:57`).
  - The budget is 2,522 bytes above any entry the firmware can write, so
    `TheLargestEntryFitsTheBudget` has to be `EXPECT_LE`. PlacesDocTest's exact fit is not
    possible here.
  - This is the decision spec A11 made (spec :168-170), and spec review 1 cleared it. It is a safe
    overestimate: a C string cannot hold NUL, the only six-byte escape.
  - Changing it would reverse a decision for no functional gain, so it is not a finding.
- **`Load` as its own enum, and `save` returning a byte count** instead of
  `LoadResult = AdoptedLoad` / `SaveResult` (`PassageFile.h:23-29`, `HighlightFile.h:23-34`).
  - Both were declared in the plan's deviations (plan :86-90).
  - `Load` needs the `OtherBuild` and `NoBuildId` outcomes, which `AdoptedLoad` cannot carry.
  - `BibleSearchFingerprintFile.h:48` has the same `enum class Load` precedent.
- **`MAX_UNIT_BYTES = 28` is defined again** (`HomeVerseDoc.h:30`, beside `PlacesDoc.h:32`).
  - Each Doc keeps its own caps locally; PlacesDoc does the same.
  - The shared home would be `lib/StudyStore/StudyStore/Unit.h`, which is outside the file lock.
  - Two copies do not justify the extraction yet.
- **Comments.** Every new comment gives a "why" or a contract. None restates the next line:
  - the on-card codes kept apart from the enum (`HomeVerseDoc.cpp:16-17`);
  - `is<T>()` before `| default` (`:47-48`);
  - the char-array cast (`:123`, confirmed in ArduinoJson as noted above);
  - the cross-field rule (`:142`);
  - why the path is not under `PASSAGES_DIR` (`SdPaths.h:26`);
  - why `save()` stays silent (`HomeVerse.cpp:106`);
  - the IDF retrieve length (`HomeVerse.cpp:38-39`, which matches
    `sdkconfig.x4pro:507`, `CONFIG_APP_RETRIEVE_LEN_ELF_SHA=9`).
  - The test headers' `(spec berean-o-7, A…)` tags follow `HomeVerseCacheTest.cpp:1` and
    `HomeTargetsTest.cpp:1`.
- **No dead code.**
  - The trailing returns after the exhaustive switches (`HomeVerseDoc.cpp:31`,
    `HomeVerseFile.cpp:45`, `:83`) are the `-Wreturn-type` idiom.
  - `describe(Load::NoBuildId)` is never reached by firmware (`HomeVerse.cpp:95-98` returns
    first), but the switch must be exhaustive, and a test pins the word.
- **`[[maybe_unused]] const unsigned long started = millis();`** (`HomeVerse.cpp:93`, `:109`)
  follows `BibleSearchStore.cpp:149` and others.
  - The release-build `unused variable 'started'` in `scan()` predates this PR
    (`origin/main:HomeVerse.cpp:96`). The PR moved that body but did not add the line.
- **Error handling has the established shape.**
  - The shell logs the specific cause: unknown format and malformed with `LOG_ERR`, another build
    with `LOG_DBG`, as `MeetingWeekCache.cpp:24-26` does.
  - `HomeVerse` adds one summary line.
  - Writes are refused, never truncated.
- **Fixtures are duplicated per test file** (`HomeVerseDocTest.cpp:33`, `HomeVerseFileTest.cpp:21`).
  The places suites do the same (`PlacesDocTest.cpp:19`, `PlacesStoreTest.cpp:22`), and the repo
  has almost no shared test-fixture headers.
- **The tests are well designed** apart from Finding 1:
  - golden documents pin the on-card bytes;
  - round trips go through `validFor`;
  - one edit at a time covers every refusal class;
  - `.tmp` adoption and refuse-then-replace go through the real `PersistableStore.cpp`.
  - `TheBudgetIsDerivedFromTheCaps`'s `SAVE_BUDGET == worstCaseBytes()` is not a tautology. It
    catches a budget tidied to a round number, as `PlacesStore.cpp:54-56` does with a
    static_assert.
- **`docs/file-formats.md`** follows the places/recent sections: the owners, then `### Version 1`,
  then `v` first, then an example, then the fields, then the derived budget. Its values agree with
  the code: `r` ≤ 48 bytes against `REFERENCE_BYTES = 49`, and `rung` 0–2 against
  `RUNG_COUNT = 3`.

## Assessment

The change follows the repo's existing patterns throughout: the Doc/File split, adopting reads,
atomic writes, a derived budget, a version that is refused rather than reinterpreted, and a lazy
first-use gate. It adds no second way to do something the repo already does. The names match the
siblings, and the comments give reasons rather than narration. Both findings are small additions
that bring the PR level with its own siblings, one a test and one a compile-time guard. Neither
reverses a decision or needs the owner's judgement.

Findings: 0 BLOCKER, 0 MAJOR, 2 MINOR.

VERDICT: CLEAR
