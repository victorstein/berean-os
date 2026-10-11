Tier: standard

# berean-o-6 (R11) plan review, pass 0

Plan: `docs/superpowers/plans/2026-10-11-berean-o-6-plan.md` (at `8fc56e2a`).
Spec: `docs/superpowers/specs/2026-10-11-berean-o-6-design.md`.

## Summary

The plan is sound. I didn't just read it; I executed it literally in a throwaway copy of `HEAD`
(`git archive` into the scratchpad). A script took every code block by line range and applied
every "Replace X with Y" as an exact-string replacement that fails unless X occurs exactly once.

- **Every Replace block in Tasks 3 to 7 matches exactly once, in order.** That still holds after
  Task 4's `./bin/clang-format-fix -g` pass, run through the repo wrapper in a throwaway git repo,
  and after Task 0's merge of `origin/main` (R20, `106dfc4b`, #255), which is now on `main` and
  touches `BibleSearchActivity.cpp`.
- **The host suites pass as written.**
  - After Task 1, `BibleSearchIndexTest` has 61 tests, the 7 `BibleSearchResidency.*` tests
    included.
  - After Task 2, `BibleSearchFingerprintTest` has 13.
  - After Task 3, it has 26.
  - No warnings come from the new sources under the suite's `-Wall -Wextra -pedantic`.
  - Each count matches the plan's "Expected" line (`:329-330`, `:811`, `:1132`).
- **The firmware-only store compiles.** The merged `BibleSearchStore.cpp` after Tasks 4 and 5,
  and `BibleSearchFingerprintFile.cpp`, pass `clang++ -std=gnu++2a -fsyntax-only -Wall -Wextra`.
  They were compiled against `test/stubs` plus four small `esp_*`/`Print.h` shims. Two log
  configurations were checked, with no warnings in either:
  - no-op log macros, which is what `x4pro-gh_release` produces (`lib/Logging/Logging.h:63-65`);
  - printf-backed log macros under `-Wformat=2`, which is the dev env's `LOG_LEVEL=2`.
- **The activity was checked by reading, not by compiling.** Its dependencies (FreeRTOS, the
  generated i18n headers) are too heavy to shim. Its diff is small, and it uses the store API as
  declared.
- **The numbers are right.**
  - The golden record in `FingerprintFileTest` (`:470-482`) is correct: I recomputed the check as
    `0x0A2773772E5D52D7` → `D7 52 5D 2E 77 73 27 0A`.
  - The FNV-1a 64 vectors (`""`, `"a"`) and the little-endian helper bytes are correct too.
- **The `FILES:` lines** (`:31-35`) are at column 0, outside any fence, comma-separated and
  repo-relative. The pipeline's own parser (`herdr-plugin-pipeline/src/lib/gating.ts:42-62`)
  accepts that form. They cover every file a step creates or edits:
  - `src/study/BibleSearchResidency.h`
  - `src/study/BibleSearchFingerprintFile.{h,cpp}`
  - `src/study/BibleSearchStore.{h,cpp}`
  - `src/activities/reader/BibleSearchActivity.{h,cpp}`
  - everything under `test/bible_search_index/`
  - `docs/file-formats.md`

  The `build/` logs and `build/r11-discover-names.md` are git-ignored scratch files.

There are no BLOCKERs and no MAJORs. The three MINORs below are process wrinkles in Tasks 7 and 8,
fixable inline.

## Findings

### MINOR 1 — Declaring `docs/file-formats.md` holds all of t6 behind t3, so Task 7's fallback can never run

- **Claim.** If t3 still holds `docs/file-formats.md` when Task 7 comes up, Task 7 skips the edit
  and pastes the section into the PR body (plan `:56-58`, `:2244-2246`).
- **Problem.** The same file is on t6's lock (`FILES: docs/file-formats.md`, plan `:35`). The
  pipeline holds the **whole task** back while a sibling in flight shares any declared path:
  - `prompts/plan.md`: "holds this task back while a sibling is working on any of them";
  - `gating.ts:155` `filesClearFor` and `:166` `releasableFromFiles`.

  So t6 cannot be implementing while t3 holds the file, and the Task 7 branch is unreachable. The
  real effect is different from what the text implies. If t3 (R12, heavy) is in a files-holding
  phase when t6 clears plan review, none of R11's code starts until t3 is out of flight.
  - The spec only expected the docs file to be "added to this task's files once t3's lock allows"
    (spec `:472-475`), not the store and activity work to wait.
  - Correctness is unaffected, and declaring the file is the right call. Leaving it off while
    Task 7 edits it would be an undeclared file.
- **Fix.** Pick one; neither needs a code change.
  - **(a) Keep the declaration.** Replace plan `:56-58` with one sentence: "Declaring
    `docs/file-formats.md` holds this task in `blocked-on-files` while t3 implements; that is
    accepted." Then drop the conditional at `:2244-2246`.
  - **(b) Run R11 in parallel.** Remove `docs/file-formats.md` from `:35`. Turn Task 7 into
    "always put the section in the PR body under *For the orchestrator*", and never edit the file
    in this branch.

### MINOR 2 — Task 8 licenses format edits to files outside the lock

- **Claim.** "This task may fix formatting anywhere the whole-tree format pass reports" (plan
  `:2325`). Step 1 then commits whatever `./bin/clang-format-fix` rewrote (`:2329-2331`).
- **Problem.** Any rewrite outside the `FILES:` lines is an undeclared edit that a sibling could
  be making at the same time. CI keeps `main` formatted, so this is unlikely in practice.
  - I ran the whole-tree wrapper over `HEAD` plus every plan edit. It changed only
    `src/study/BibleSearchStore.cpp` (three cosmetic reflows), which is declared.
  - But Task 0 merges `origin/main` first, and the plan's wording authorises committing whatever
    comes up.
- **Fix.** Replace `:2325`'s second sentence with: "If the pass rewrites any file not on the
  `FILES:` lines, restore it (`git checkout -- <file>`) and report it in the PR body; do not
  commit it."

### MINOR 3 — Task 8 tells the implementer to push and open the PR, which its brief forbids

- **Claim.** Task 8 Step 5 says "Run: `git push`" and open the PR (plan `:2373-2375`).
- **Problem.** The pipeline hands this plan to an implementer subagent along with a verbatim
  brief. That brief says: "Commit, but do not push: pushing is the worker's, once it has verified
  your work" (`herdr-plugin-pipeline/prompts/implement.md:45`). The worker pushes and opens the PR
  only after it has re-run the tests itself (`implement.md:20-23`). An implementer following the
  plan literally would push unverified work, and race the worker's PR.
- **Fix.** Retitle Step 5 "Worker: push and open the PR (after verifying Steps 1-3 yourself)".
  Add to Task 8's header: "Steps 1-4 are the implementer's; Step 5 is the worker's." Step 4
  (`hpipe discover`) can stay with either; `hpipe` resolves on PATH (`~/.local/bin/hpipe`).

## Checked and found sound (no finding)

- **Every spec requirement maps to a step.** I checked Goals 1-4, A1-A13, D1, every row of the
  error table, the docs contents, the testing list and the follow-up.
  - **Decode order:** `decode` follows the fixed order exactly (plan `:767-786`), and the tests
    pin each step of it.
  - **`coversEveryBook`:** gates the one writer, `persistFingerprint` (plan `:1556-1582`), which
    is reached through `resolveDocuments` from both `fingerprintFor` and `documents()`. That is
    equivalent to the spec's "called from both".
  - **`mayOverwrite`:** gates the writer as A10/A11 require. The plan adds a skip when the record
    is already `Ok` with the same fingerprint, which satisfies CLAUDE.md's "guard redundant
    writes".
  - **`identifiedAs`:** compares path and spine count without re-sizing the EPUB (A3, m4).
  - **The trivially-copyable `static_assert`:** `BibleBookNameTable` is char arrays plus an `int`
    (`BibleBookNameTable.h:57-59`), and it compiled.
- **The I/O tests behave as intended against the fake.**
  - `failReadsOf` makes `openFileForRead` fail while `exists` stays true
    (`test/stubs/HalStorageFake.h:28-30`, `HalStorageFake.cpp:158-159`), so the `Unreadable` test
    exercises the path it names.
  - `failWritesAfter` produces the short write.
  - The fake's `rename` refuses an existing target, which proves the remove-then-rename order.
- **A leftover `.tmp` cannot wedge `save()`.** The real `openFileForWrite` opens with `O_TRUNC`
  (`freeink-sdk/libs/hardware/SDCardManager/src/SDCardManager.cpp:337`), so a longer leftover
  never fails the 40-byte size check. That backs the spec's "a leftover `.tmp` is overwritten by
  the next save".
- **Resume safety.** `IndexReader::open` stores `source_` by value with `ctx = &indexFile_`, and
  `cacheTermIndex()` is a no-op once cached (`lib/BibleSearch/BibleSearch/IndexReader.cpp:18-20`,
  `:50-52`). So re-opening into the same member is enough, as A2 says.
- **The activity log delta is deliberate and harmless.** The plan logs `index ready|unusable`
  where the spec says `index resumed|opened|unusable`. It flags this itself (`:2218-2221`). The
  store's own `Index resumed|opened in N ms` line still carries the distinction the device check
  needs.
- **Tasks 4-6 have no failing test, and that is sanctioned.** The spec's testing strategy puts the
  store and activity wiring outside host tests, because the store needs `Epub` and `STUDY` (spec
  `:611-612`). Every task still leaves the tree building: until Task 6 the activity still calls
  `close()`, so the device behaves as before.
- **Formatting between tasks has no practical effect.** `clang-format-fix -g` formats only
  tracked, modified files (`git ls-files --modified`, `bin/clang-format-fix`), so it never touches
  the new files. They are already format-clean, though, and Task 8's whole-tree pass is the
  backstop.
- **One stale sentence.** Plan `:59` says t1 "edits only `onEnter`". #255 on `main` also changed
  comments in `showFailure`, `finishBuild`, `handleDialogInput` and `render`. None of them
  intersects a Task 6 block, as I verified by applying Task 6 to `origin/main`'s files.

VERDICT: CLEAR
