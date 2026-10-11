Tier: heavy

# berean-o-7 (R22) plan review 0

- Plan: `docs/superpowers/plans/2026-10-11-berean-o-7-plan.md` (at `e281c0c6`)
- Spec: `docs/superpowers/specs/2026-10-11-berean-o-7-design.md`
- Spec review: `docs/superpowers/reviews/berean-o-7-spec-review-1.md` (CLEAR)
- Brief: `hpipe brief --task t7`

This is the first plan review, so there are no earlier findings to re-check.

## How this was checked

I did not stop at reading the plan. I ran it literally against a scratch copy of the tree
(`git archive e281c0c6`) and reused the main checkout's googletest 1.17.0 and ArduinoJson 7.4.2
sources. A script copied every code block out of the plan by line range, in task order, and placed
each one where the plan says. That covers Tasks 1–10, both the red run and the green run of each
task. The tracked files in this worktree were not touched.

- **Firmware TUs.** No firmware build was run. Instead, the plan's final `HomeVerse.cpp`,
  `HomeVerseDoc.cpp` and `HomeVerseFile.cpp` were each compiled syntax-only with the real ESP32-S3
  headers. That used the xtensa GCC 14.2 toolchain, `-std=gnu++2a -Wall`, the framework's own
  `flags/defines`, `flags/includes` and `flags/cpp_flags`, and the `x4pro` and release logging
  defines.
- **Baseline.** Today's `HomeVerse.cpp` went through the same check first, as the control.

## What was verified and holds

- **Spec coverage.**
  - Every assumption A1–A15 maps to a task.
  - Every test that the spec's Testing strategy lists for `HomeVerseDocTest` (1–11),
    `HomeVerseFileTest` (1–6), `HomeLayoutTest` and `SdPathsTest` maps to a test in the plan.
  - The error table, the Logging lines (with MINOR 3's exception), the PR-body requirements and
    the two sibling seams all have a home.
- **Deviations.** The four declared deviations (plan:78-92) are sound. None of them changes
  behaviour.
- **Types and signatures stay consistent from task to task.**
  - `HomeVerseDoc::Restore`, `HomeVerseFile::Load`, `describe`, `load(buildId, rungCount,
    allocator, Entry&)` and `save(entry, buildId, allocator)` agree everywhere they appear, and all
    of it compiled.
  - `RUNGS`/`RUNG_COUNT` = 3, `MAX_BUILD_ID_BYTES` and `SAVE_BUDGET` agree from task to task.
- **The TDD claims are true as written, apart from MAJOR 1's commands.**
  - **Task 1:** red gives `no member named 'HOME_VERSE_FILE' in namespace 'sdpaths'`; green passes
    8 tests.
  - **Task 3:** red is the `Undefined symbols … toJson` link error. Green passes 4 tests, and all
    three golden strings match exactly.
  - **Task 4:** red is the `Undefined symbols … fromJson` link error; green passes 9 tests.
  - **Task 5:** red fails exactly the three new tests. Failure counts are 2 (`e` 1, `f` false),
    4 (rung, reference, spine, date) and 6, as plan:841-847 predicts. Green passes 12 tests.
  - **Task 6:** red gives the four `no member named 'worstCaseBytes'/'SAVE_BUDGET'` errors. Green
    passes 14 tests. The wrapper string counts to 106 characters, and
    `worstCaseBytes() == 3943` follows from the caps.
  - **Task 7:** green passes 8 tests, including the `.tmp` adoption: main file restored with
    identical bytes, `.tmp` gone.
  - **Task 8:** the mutation fails 446 vs 448, and the reverted tree passes 42 tests.
  - **Whole suite:** with all ten tasks applied, `cmake --build` and `ctest` give
    `100% tests passed out of 1576`.
- **Task 9 compiles clean on the target.**
  - `x4pro` config: zero warnings and zero errors in all three TUs.
  - Release config: the only warnings are `unused variable 'started'` (see MINOR 2).
  - The `scan` body is byte-identical to `HomeVerse.cpp:68-133` today.
  - `esp_app_desc.h` is on the framework include path
    (`framework-arduinoespressif32-libs/esp32s3/pioarduino-build.py:317`). It declares
    `int esp_app_get_elf_sha256(char*, size_t)` (`esp_app_desc.h:64`) under `extern "C"`.
  - With `size` 9 it copies 8 hex digits and a NUL (`esp_app_desc.c:107-116`).
- **ArduinoJson details the code relies on.**
  - **The cast at plan:509/976 is needed and its comment is accurate.** A `const char (&)[N]` is
    adapted as a static `RamString(p, N - 1, true)` (`Strings/Adapters/RamString.hpp:89-95`). An
    uncast `reference` would therefore be stored by pointer at 48 bytes, NULs included.
  - **`makeDocument`'s nullptr branch is needed.** `JsonDocument`'s allocator constructor has no
    null guard (`JsonDocument.hpp:24`), and the branch mirrors `PersistableStore.cpp:242`.
- **Storage discipline (CLAUDE.md, five rules).**
  - The write is `writeDocToFileAtomic`, after an `overflowed()` check and a `fitsBudget` check.
  - The read is `readDocFromFileAdopting`, and `.tmp` promotion goes through
    `PersistableStore.cpp:170-213`.
  - `doc["v"] | 0` refuses an absent version (`FormatVersion.h:10-16`).
  - The owning task is the loop task, and the single-task comment is updated.
- **The `FILES:` lines.**
  - They sit at column 0, outside fences (plan:57-62), and are comma-separated. That is the form
    `planDeclaredFiles` parses (stein.pipeline `src/lib/gating.ts:34-59`).
  - Their 14 entries are exactly the 14 files the ten tasks change. A `git diff --name-only` of
    the scratch run, set against them, leaves nothing missing and nothing declared but unused.
  - The only undeclared touch is the transient one in MINOR 1.
- **Sibling drift.**
  - `origin/main` has moved two commits past `478cf1ed` (`106dfc4b`, `b2f19ef7`). Neither touches
    any file this plan edits.
  - t2's lock includes `test/stubs/HalStorageFake.cpp`, but its plan only adds
    `HalFile::seekCur` (`perf-r9-cover-band-cache` plan:58), which `HomeVerseFileTest` does not
    depend on.
  - The discovery the PR body cites, `t7/x1`, exists (`hpipe discoveries`).

## Findings

### MAJOR 1 — The new test executables cannot be built with the commands the plan gives (Tasks 2 and 7)

- **Claim.**
  - Task 2 Step 2: "CMake reconfigures and fails with `Cannot find source file: …/HomeVerseDoc.cpp`"
    (plan:264-269).
  - Task 2 Step 4: `cmake --build build/test --target HomeVerseDocTest -j8 && ./build/test/home_layout/HomeVerseDocTest`
    passes 1 test (plan:332-336).
  - Task 7 Steps 2 and 4 make the same two claims for `HomeVerseFileTest` (plan:1314-1319,
    :1448-1452).
- **Problem.**
  - Task 0 configures the build directory with the default generator, Unix Makefiles, because
    there is no ninja (plan:125).
  - Under Makefiles, `make` resolves the target name before `cmake_check_build_system` can
    regenerate, so a target added to a `CMakeLists.txt` since the last configure does not exist yet.
  - **Both steps therefore print `make: *** No rule to make target 'HomeVerseDocTest'.  Stop.`**,
    and nothing reconfigures.
  - Step 2 looks red for the wrong reason. Step 4 then fails the same way even with correct code,
    so an implementer who follows the plan literally is stuck on two tasks' green runs.
- **Evidence.**
  - Reproduced in the scratch copy with cmake 4.4.2 (`CMAKE_GENERATOR:INTERNAL=Unix Makefiles`),
    for both targets.
  - An explicit `cmake -S test -B build/test` prints the message the plan expects:
    `CMake Error at home_layout/CMakeLists.txt:26 (add_executable): Cannot find source file:`, and
    `:48` for the second target.
  - After the sources exist, reconfigure plus build passes. CI uses Ninja (`.github/workflows/ci.yml:145`),
    which re-runs CMake by itself, which is why this has not shown up there.
- **Fix.**
  - In Task 2 Step 2 and Task 7 Step 2, run `cmake -S test -B build/test` first. The expected
    output is the "Cannot find source file" error.
  - In Step 4 of both tasks, start the command with `cmake -S test -B build/test && `.
  - Tasks 3–6 and Task 11 are unaffected: their targets already exist, or they build `all`.

### MINOR 1 — Task 8 Step 2 edits `LyraTheme.h`, which no `FILES:` line covers

- **Claim.** Task 8 Step 2 changes `.topPadding = 5,` in `src/components/themes/lyra/LyraTheme.h:11`
  to 6, runs the test, then `git checkout --` the file (plan:1505-1514).
- **Problem.**
  - Literally, this is a touched file missing from `FILES:` (plan:57-62).
  - I rank it MINOR, not BLOCKER:
    - The edit is never staged or committed, and it is reverted in the same step. Task 8 Step 3 then
      checks `git status --short`.
    - The lock is dispatch-time intent, not an edit or diff guard
      (`gating.ts:24-26`, "Declared-intent heuristic, not enforcement"; `supervisor/tasks.ts:272-274`
      only widens it). An edit that never leaves this worktree cannot collide with a sibling.
  - Two smaller problems with the same step:
    - **Stale rebuild.** In the scratch run the revert landed in the same second as the object
      file. `make`'s one-second mtimes then left `HomeLayoutTest` stale, and Step 3 reported
      6 failures until I touched the header.
    - **Wrong expected output.** Step 2 says "along with other existing layout tests", but under
      `--gtest_filter='*VerseTextBox*'` only the one test runs.
- **Fix.** Prove the failure without leaving the locked file.
  - In the new test, temporarily build the Lyra box from a copy of the metrics:
    `ThemeMetrics shifted = LyraMetrics::values; shifted.topPadding = 6;`
    then `layoutFor(shifted).verseText`.
  - Expect 446 vs 448, then restore the line.
  - Drop the `LyraTheme.h` edit and the `git checkout`.

### MINOR 2 — The plan forbids the repo's own idiom for log-only timing variables, and adds two release-build warnings

- **Claim.** Task 11 Step 3 (plan:1972-1977): "an `unused variable 'started'` warning in
  `restoreOnce` or `save` is that same pattern: leave it, and do not add casts."
- **Problem.**
  - CLAUDE.md, "When to commit": do not commit "when the build fails or warns".
  - The repo already marks log-only timing variables `[[maybe_unused]]`:
    `src/study/BibleSearchIndexer.cpp:152`, `:228`, `:256`; `src/network/MeetingWeekPrefetch.cpp:72`;
    `src/main.cpp:801`.
  - The plan mirrors the one instance that warns (`HomeVerse.cpp:96`) rather than the idiom.
  - Its citation is also off.
    - **Plan's citation.** It cites `lib/Logging/Logging.h:51-60`.
    - **Release's actual branch.** The release env defines no `ENABLE_SERIAL_LOG`
      (`platformio.ini:191`), so the branch that applies is the `#else` at `Logging.h:62-66`.
    - **What expands to nothing.** In that branch `LOG_ERR` is empty too, not only
      `LOG_DBG` and `LOG_INF`.
- **Evidence.** The syntax-only check of the plan's `HomeVerse.cpp` under the release defines
  reports:
  - `HomeVerse.cpp:93:23` (`restoreOnce`, new);
  - `:109:23` (`save`, new);
  - `:148:23` (`scan`, pre-existing; it is `:96` today and appears in the baseline check too).

  The `x4pro` config is clean.
- **Fix.**
  - In Task 9's `restoreOnce` and `save`, write `[[maybe_unused]] const unsigned long started = millis();`.
  - In Task 11 Step 3, accept only the pre-existing warning in `scan`, whose body moves unchanged,
    and cite `Logging.h:62-66`.

### MINOR 3 — The `other build` identities are not on the restore line, and the deviation is undeclared

- **Claim.** The spec's Logging section (spec:348-351) says: "On `other build` the line also names
  both identities (`file 1a2b3c4d, running 5e6f7a8b`)".
- **Problem.**
  - The plan puts the identities on a separate `LOG_DBG` inside `HomeVerseFile::load`:
    `Home verse file is from build %s; running %s` (plan:1397-1399).
  - The restore line stays `Verse restore: %s, current %d, %lu ms` (plan:1727-1728).
  - Deviations 1–4 (plan:78-92) do not mention this.
  - The device run still sees both identities, one line earlier, so the purpose is met. Only the
    contract changed silently.
- **Fix.** Add a Deviation 5 that says so. Keeping it in `HomeVerseFile` avoids handing `fromJson`'s
  document back to `HomeVerse`. Name the extra line in the PR body's device step 2 (plan:2043).

### MINOR 4 — Task 11 Step 4 rebuilds `x4pro` after changes that are only comments and docs

- **Claim.** Task 11 Step 4 runs `pio run -e x4pro` again to read the RAM and Flash lines
  (plan:1979-1985).
- **Problem.**
  - CLAUDE.md, "Testing checklist", item 1: do not "repeat a target that already passed, or
    rebuild after formatting, comment-only or documentation-only changes".
  - Task 9 Step 4 already builds `x4pro` and already greps `^(RAM|Flash):` (plan:1826).
  - After it come only Task 10 (a comment in `PersistableStore.h`, a doc) and possibly a
    `style: format` commit.
  - The header comment forces a long recompile, and the image is identical.
- **Fix.** Record the two lines from Task 9 Step 4 into the scratch file and compare them against
  `r22-size-before.txt`. Delete Step 4's build and keep its 8 KB check.

### MINOR 5 — The PR body's resource section misses `fromJson`'s transient internal allocations

- **Claim.** PR body, Resources (plan:2058-2063) lists:
  - the PSRAM documents;
  - `readFile`'s `String`;
  - "No allocation is kept and none happens in a loop."
- **Problem.**
  - `readPick` copies each stored line into a `std::vector<std::string>`, to reuse
    `home_verse::packLines` (plan:934-940). That is up to 4 heap strings, about 516 bytes of text at
    most, allocated in a loop.
  - Every block is under the 4,096-byte routing threshold, so it lands in internal SRAM (project
    memory, ESP32-S3 PSRAM auto-routing threshold). All of it is freed on return.
  - Reusing `packLines` is the right call. The PR body just has to say what it costs, because the
    brief requires every allocation to be justified there.
- **Fix.**
  - Add a bullet: "the restore copies the up-to-four fitted lines into `std::string`s for
    `home_verse::packLines`: transient, internal SRAM, about 530 B at most, once per boot".
  - Reword "none happens in a loop" accordingly.

### MINOR 6 — "For the orchestrator" names the wrong sibling for `docs/file-formats.md`

- **Claim.** PR body: "t3 (R12) has `docs/file-formats.md` in its lock. This PR adds one section to
  it." (plan:2084).
- **Problem.**
  - t3's spec makes no change to that file (spec review 1, MINOR 9).
  - **t6 (R11, now in `implement`) is the sibling that actually adds a section to it.** Its plan
    declares `FILES: docs/file-formats.md` and adds `search-fingerprint.bin`
    (`perf-r11-search-index-resident/docs/superpowers/plans/2026-10-11-berean-o-6-plan.md:35`, `:48`).
  - `hpipe show --task t6` lists the file in t6's lock.
  - The gate sequences t7 behind t6 anyway. The orchestrator's merge note should still name the
    sibling whose text the rebase will meet.
- **Fix.** Name t6 alongside t3 in that bullet.

## Assessment

The plan is sound and executes. Its code compiles on the host and, syntax-only, on the target, and
every test it adds goes red, then green, as it claims. The whole host suite stays green, and the
file lock matches the change exactly.

The one MAJOR is a mechanical command fix: reconfigure before building a newly added target. It
reverses no decision, changes no scope and needs no owner judgment. The MINORs are a cleaner way to
prove a test can fail, a repo idiom the plan should follow, one undeclared logging deviation, one
redundant firmware build, and two PR-body corrections. All of them can be fixed in the plan inline.

VERDICT: CLEAR
