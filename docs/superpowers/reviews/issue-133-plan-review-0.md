# Issue #133 plan review 0

Reviewed: `docs/superpowers/plans/2026-09-26-issue-133-plan.md` against
`docs/superpowers/specs/2026-09-26-issue-133-design.md`.

## How this was checked

I did not stop at reading the plan. I applied its edits literally, with the exact text from
steps 2a, 2b, 6a and 6b, to `test/persistable_store/PersistableStoreTest.cpp`. I then ran the
plan's own build command and each of its four mutation one-liners (M1, M4, M2, M3), rebuilding
from deleted objects each time. After that I reverted everything, rebuilt, and confirmed that
`git status --short` was empty. Results:

- **Unmodified production tree:** `[  PASSED  ] 7 tests.`
- **M1** (`FormatVersion.h`): `git diff --stat` reports 1 line changed. Exactly three tests fail:
  - `ARefusedLoadBlocks…` at `:71-73`
  - `AnUnparseableFileAfterARefusal…` at `:104-105`
  - test 1 at its `saveToFileAtomic`, `saveToFile` and `bytesOn(PATH)` checks

  This matches plan step 3.
- **M4** (rename skipped): only test 1 fails, at the two post-load checks and the two final checks.
  This matches step 4. `ARefusedLoadBlocks…` does not fail.
- **M2** (`.tmp` removed on `KeepTempReportEmpty`): only test 2 fails, at the single
  `bytesOn(TMP_PATH)` check after the load. This matches step 7.
- **M3** (`KeepTempReportEmpty` reports `ParseError`): only test 2 fails, at `saveToFileAtomic`
  and the two final `bytesOn` checks. This matches step 8 and shows that A-6 does real work.
- **Formatting:** `./bin/clang-format-fix` leaves the edited test file unchanged. All lines are
  under `ColumnLimit: 120` (`.clang-format:133`).
- **Perl regexes:** each one matches exactly one site. The M4 pattern cannot hit
  `PersistableStore.cpp:40`, which is `finalPath.c_str()`. The M3 pattern cannot hit the
  `adoptedLoad` arm at `TempAdoption.h:86-88`, which returns `AdoptedLoad::Empty`.
- **ctest:** `gtest_discover_tests` (`test/persistable_store/CMakeLists.txt:21`) registers the
  tests as `PersistableStoreGuard.*`, so `-R PersistableStore` selects exactly 7. Step 9's
  "out of 7" is therefore correct.

## Spec-to-plan mapping

| Spec item | Plan step |
|---|---|
| A-1: one test file, existing fixture and helpers | 2b, 6b (`PersistableStoreGuard`, `PATH`, `TMP_PATH`, `NEWER`, `bytesOn`) |
| A-2: header comment | 2a |
| A-3: two card checkpoints in test 1 | 2b (post-load and post-save checks) |
| A-4: both saves refused | 2b |
| A-5: `value = 42` and `value == 0` | 2b |
| A-6: test 2 starts refused | 6b (priming `putFile` + `ASSERT_FALSE` + `ASSERT_TRUE(remove)`) |
| A-7: `TORN` constant beside `NEWER` | 6a |
| A-8: two card checkpoints in test 2 | 6b |
| A-9: test names | 2b, 6b |
| A-10: mutation red, object deletion, stale-excerpt rule | Ground rules; steps 3, 4, 7, 8 |
| TDD order 1-4 | Steps 2 → 3/4 → 6 → 7/8 → 9 |
| Gates: unsuffixed format after `git add`; no `pio run`; `Closes #133` and excerpts in the PR body | Steps 5, 9 and 10 |
| Error handling (`hpipe decide`; fix the test, not the mutation) | Ground rules |

Every spec requirement has a step. Names are consistent throughout: `TORN`, both test names,
`store()`, `bytesOn`, and the log paths `build/test/mutation-M{1,2,3,4}.log`.

`FILES:` lines (`:13-14`) sit at column 0 outside any fence and use repo-relative paths. The
comma-separated form matches earlier plans (`2026-09-26-issue-111-plan.md:15-25`). They cover
the test file and all three files the mutation steps edit. Everything else the steps write goes
under `build/`, which is gitignored build output, not a source file.

Steps 2 and 6 cannot start red, because the behaviour already exists. The spec settles this in
A-10, and the plan shows red through steps 3/4 and 7/8 before each commit. Every step leaves the
tree building and green, and each mutation step ends with a revert and a `git status` check.

## Findings

### MINOR 1: `head -30` truncates the M1 excerpt in the PR body

- **Claim:** step 10 builds each mutation excerpt with
  `grep -E "Failure|FAILED  \]|Actual|Expected|Which is|^  " … | head -30`
  (plan `:296`).
- **Problem:** that filter yields 39 lines on the M1 log. Test 1 runs last, so the cut removes
  the detail of test 1's `bytesOn(PATH)` failure and the `[  FAILED  ] 3 tests` summary. That
  failure is the step-5 evidence the spec's M1 row asks for (spec `:220`). The other logs are
  unaffected: M4 gives 25 lines, M3 17 and M2 9.
- **Evidence:** I measured this on a real M1 log. The `head -30` cut falls between the
  `PersistableStoreTest.cpp:128: Failure` header and its values.
- **Fix:** drop `| head -30`, or raise it to `head -60`.

### MINOR 2: PR excerpts carry the local absolute worktree path

- **Claim:** gtest prints failure locations as absolute paths, for example
  `/Volumes/stein/.herdr/worktrees/berean-os/…/PersistableStoreTest.cpp:126: Failure`.
- **Problem:** the grep copies these lines verbatim into the body of a PR on a public repo. That
  is noise, and it exposes a local path.
- **Fix:** add `| sed "s|$PWD/||"` to the step-10 pipeline, before `head`.

Neither finding reverses a decision, changes scope or needs a human judgment. Both are fixable
inline.

VERDICT: CLEAR
