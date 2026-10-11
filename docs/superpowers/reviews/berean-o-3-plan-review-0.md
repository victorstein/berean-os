Tier: heavy

# berean-o-3 plan review 0

Reviewed: `docs/superpowers/plans/2026-10-11-berean-o-3-plan.md` at `f2cc4daa`, against
`docs/superpowers/specs/2026-10-11-berean-o-3-design.md` (after spec review 1), the code on this branch,
the in-progress t4 worktree (`perf-r21-menu-close-restore`), `origin/main`, and the pipeline's own
`prompts/plan.md`, `prompts/implement.md` and `src/lib/gating.ts` in `herdr-plugin-pipeline`.

## How I checked

I executed the plan literally, not just read it. I exported `HEAD` into a scratch copy
(`/private/tmp/claude-501/o3-plan-review/repo`) and pasted every code block in through a script. The
script refuses any "Replace" or "insert before/after" anchor that does not occur exactly once. Then:

- **Task 0.** The wrapper configured, and the baseline build and `ctest` passed: 133 tests.
- **Every red step went red as the plan says.** Step 1.2 failed with
  `no member named 'isAlreadyFront' in namespace 'RecentBooksDoc'`. Step 3.7 failed exactly two tests:
  `TheSpineIsReadInBufferWindowsNotPerEntry` and `AShortSpineBlockFailsTheLoad`. Step 5.2 failed with
  `no member named 'reopen'`. Steps 7.4 and 8.3 failed with "file not found".
- **Every green step went green with the plan's counts.** Task 1: 29 `RecentBooksDoc*` tests,
  including 5 new. Task 4: 6. Task 5: 12. Task 7: 9. Task 8: 6. None of the host-compiled files the
  plan touches gives a warning under the suite's `-Wall -Wextra -pedantic`.
- **Tasks 2, 6, 9, 10 and 11 apply cleanly.** Every anchor in `RecentBooksStore.cpp`, `Epub.{h,cpp}`,
  `StudyStore.{h,cpp}`, `EpubReaderActivity.{h,cpp}`, `BibleNavCache.h` and `BibleBookIndex.h` is
  unique on this branch.
- **The whole host tree passes with all 13 tasks applied.** All 1,585 tests pass, which is what CI's
  `cmake --build build/test` + `ctest` does (`.github/workflows/ci.yml:145-151`).
- **The plan's code is already formatted.** `clang-format` 21.1.8 with the repo's `.clang-format`
  changes none of the 24 files the plan creates or edits.

I did not run `pio run`, because nine PlatformIO processes from other sessions were live. I checked the
firmware-only code against the real APIs it calls instead (list below).

## Verified and sound (not findings)

- **Spec coverage.** Every component in spec §5.1 (1–9) and every assumption with a code consequence
  maps to a step:
  - A3/A4 → Task 4. A6 → Task 5. A7 → Task 6. A5/A8 → Tasks 7, 10 and 11.6.
  - A9 → Tasks 8, 9 and 11.8: all four conditions, with the registry re-read only for a `local-` key.
  - A10 → `resumePublication`. A11 → Steps 11.5 and 11.10. A14 → Tasks 1–2. A16 → Step 9.5.
  - Spec review 1's MINORs are honoured:
    - the href skip loops over a 64-byte scratch, and is tested at 200 bytes and across 4,096-byte
      refills;
    - the reuse branch sets `highlightsLoaded = true`;
    - `park` with nothing to park clears the slot;
    - Delete cache sets `cacheDeleted`, and the reader then does not park.
  - The two recorded deviations are sound. `ParkedBookOf.h` is split out so the host suite never
    includes `Epub.h`. `BibleBookIndex.h`'s size-routing `static_assert` is deleted because it goes
    stale. Both are declared.
- **Names and signatures stay consistent across steps.**
  - `ParkedBookOf::park(std::string, uint64_t, Held)` and `take(const std::string&, uint64_t, bool,
    Miss&)` match their callers in `ParkedBook.cpp`.
  - `ParkedBook::park(const std::string&, Held)` and `take(const std::string&, bool)` match
    `EpubReaderActivity`.
  - `study_reuse::mayReuse(const Inputs&, bool (*)(void*), void*)` matches `StudyStore::isOpenFor`.
  - `Epub::reopenCache(bool)` satisfies the template's `book->reopenCache(skipLoadingCss)`.
  - `loadFreshEpub(bool)`, `cacheDeleted`, `makePsramNavCache()` and `loadedFileSize` are each
    declared before use.
- **Firmware-only code against the real headers.**
  - `HalFile::fileSize64()`, `size()` and `seek()` exist (`lib/hal/HalStorage.h:77-80`).
  - `CssParser::hasCache() const`, `loadFromCache()` and `clear()` exist
    (`lib/Epub/Epub/css/CssParser.h:83,88,106`).
  - `Epub::getBibleBookNavSpineIndex()` and `getPath()` are `const` (`lib/Epub/Epub.h:56,91`), so
    `keyInputsFor(const Epub&)` compiles.
  - `bookPath` is a `protected` member of `ReaderActivity` (`ReaderActivity.h:13-14`).
  - `<esp_heap_caps.h>` with `heap_caps_*` and `MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT` is an existing
    idiom (`src/network/CatalogIndexStore.cpp:14,46`; `src/study/BibleSearchStore.cpp:5,25`).
  - `parseCssFiles()` always calls `saveToCache()` (`lib/Epub/Epub.cpp:351`). A book with no CSS
    therefore still has a rules cache, and `reopenCache` will not miss on it forever.
  - The unit index lives under `UNITS_DIR` and is keyed on the pubkey (`UnitIndexCache.cpp:48`).
    Clear cache only removes `epub_*` directories (`ClearCacheActivity.cpp:120-126`), so a reused
    `units_` never points at deleted data.
  - `BibleNavCache` holds only EPUB-derived text (`BibleNavigationActivity.cpp:144-176`), not UI
    strings, so a language change at Home cannot make a parked cache stale.
- **Collisions.**
  - t4's live diff to `EpubReaderActivity.{h,cpp}` (a `PageFrameCache` member, render-path captures,
    `pageCache.release()` in `onExit`) touches none of the plan's anchors: the destructor, `loadBook`,
    `DELETE_CACHE`, `openChapterPicker`, the includes, the `navCache` comment and
    `pendingReadFolderMove`.
  - `origin/main` has gained `#255`/`#256` since the base. They touch none of the plan's files.
- **`FILES:` lines.** Lines 15–21 are at column 0 and outside any fence. They are comma-separated
  repo-relative paths or `/`-terminated prefixes with no globs, which is exactly what `planDeclaredFiles`
  parses (`herdr-plugin-pipeline/src/lib/gating.ts:35-60`). Every tracked file a step creates or edits
  is on them (MINOR 4 covers the one untracked file). Step 12.2's `git add -u` cannot widen the set,
  because the base tree is already formatted.
- **TDD shape.** Each host-testable unit is test-first. Tasks 2, 6, 9, 10 and 11 have no failing test,
  because their files cannot be compiled on the host (spec §9; `RecentBooksDocTest.cpp:1-4`). In each
  case the logic they call was pulled out and tested first: Tasks 1, 5, 7 and 8. That is the split
  spec §9 prescribes.

## Findings

### MINOR 1: the regression `ctest` regex silently skips StorageIoTest and PassageText

**Claim.** Steps 0.3 and 12.3 build `StorageIoTest` and run
`ctest -R '...|StorageIo|...|PassageDoc|...'`, and the plan expects "every test to pass" (plan
`:81-85`, `:1966-1970`). Spec §9 lists `test/storage_io/` and `test/passage_doc/` among the suites to
run (spec `:343`).

**Problem.** `gtest_discover_tests` names tests after the gtest suite, not the executable. No suite in
`StorageIoTest` contains "StorageIo": its suites are `AdoptingRead`, `AtomicWrite`, `HalStorageFake`,
`LoadAdopting`, `PassageFileIo` and `TagPaletteFileIo` (`test/storage_io/*.cpp`).
`PassageDocTest`'s `PassageText.*` (`test/passage_doc/PassageTextTest.cpp:24`) does not match
"PassageDoc" either. So 68 + 8 tests are built and never run, and the step still reads as green.
Nothing is lost in practice: neither executable compiles a file this plan changes, and CI runs
everything. But the step does not do what it says.

**Evidence.** `ctest -N -R '<the plan's regex>'` in the scratch build lists 133 tests, none from
`StorageIoTest` and none named `PassageText.*`.

**Fix (inline).** Extend both regexes with
`|AdoptingRead|AtomicWrite|HalStorageFake|LoadAdopting|PassageFileIo|TagPaletteFileIo|PassageText`.
Or drop `-R` and run each listed executable directly, e.g. `build/o3-tests/out/repo_tests/storage_io/StorageIoTest`.

### MINOR 2: Steps 12.4 and 13 have the implementer push and open the PR, which the pipeline gives to the worker

**Claim.** Step 12.4 is `git push` (plan `:1972-1976`), and Task 13 runs `gh pr create` (`:1980-1989`).

**Problem.** The plan is handed to the implementer subagent, whose brief says: "Commit, but do not
push: pushing is the worker's, once it has verified your work"
(`herdr-plugin-pipeline/prompts/implement.md:45`). The worker then pushes and opens the PR only after
it has run the tests itself (`implement.md:22-23,29`). Followed literally, the plan pushes unverified
work, and the worker's own `gh pr create` then fails because a PR already exists. The PR body template
in Task 13 is still useful, as the text the worker posts.

**Fix (inline).** Retitle Step 12.4 and Task 13 as "worker only, after verifying Task 12's output", and
tell the implementer to stop after Step 12.3.

### MINOR 3: a fix found by Step 12.1 has no commit step of its own

**Claim.** Step 12.1 says "Fix any error or warning in the file it names, and re-run" (`:1939-1940`).
Step 12.2 then runs `git add -u` and commits as `style: format berean-o-3 changes` (`:1956-1959`).

**Problem.** Tasks 2, 6, 9, 10 and 11 are committed before any firmware build. A compile fix made in
12.1 is therefore left uncommitted, and 12.2's `git add -u` folds it into a commit labelled `style:`.
That commit then mixes a code change into what is described as formatting. The squash merge hides it on
`main`, but the branch history and the PR's per-commit review are misleading.

**Fix (inline).** Add one line to 12.1: commit each fix with explicit paths as `fix: <what>` before
Step 12.2.

### MINOR 4: the local test wrapper is created and edited by steps but is not on a `FILES:` line

**Claim.** Steps 0.2, 3.6 and 7.3 create and edit `build/o3-tests/CMakeLists.txt`.

**Problem.** The plan prompt asks for every file a step creates or edits (`prompts/plan.md:17`). This
one is gitignored (`.gitignore:13`, `build`) and lives in this task's own worktree. A sibling cannot
touch it and it never appears in a diff, so the prefix lock (`gating.ts:27-29`) gives it no protection,
and leaving it off is not a real lock gap. I rank it MINOR rather than BLOCKER for that reason. It is
listed only so that a strict reading of the rule has nothing to object to.

**Fix (inline).** Add `FILES: build/o3-tests/`. It costs nothing, and no sibling declares anything
under `build/`.

### MINOR 5: only `skipLoadingCss = true` is shown to reach `reopenCache`

**Claim.** Spec §9 requires that "the `skipLoadingCss` flag reaches `reopenCache`" (spec `:340`).

**Problem.** `TheSameUnchangedBookIsHandedBackWithItsNavCache` asserts `lastSkipLoadingCss` only after
`take(..., true, ...)` (plan `:989-994`). A `take` that ignored its argument and always passed `true`
would pass the whole suite. On device, the result would be a warm open that never re-checks the CSS
cache under embedded style, which is exactly A7's case.

**Fix (inline).** In `ACacheThatNoLongerMatchesMissesAndReleases`, which already takes with `false`,
add `EXPECT_FALSE(book->lastSkipLoadingCss);`.

VERDICT: CLEAR
