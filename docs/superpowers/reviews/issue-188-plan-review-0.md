Tier: heavy

# Issue #188 plan review, pass 0

Plan: `docs/superpowers/plans/2026-09-29-issue-188-plan.md`
Spec: `docs/superpowers/specs/2026-09-29-issue-188-design.md`
Base: `9750706b` (branch `fix/188-full-verse-passages`).

## How this was checked

I didn't only read the plan. I copied the worktree into a scratch directory and ran it:

- **Host-testable tasks.** I applied every code block and command from Tasks 1–7, 10 (test deletions),
  11 and 12 as written, then built and ran the suites each task names. After the last of them I ran the
  full `cmake --build build/test && ctest`.
- **Firmware tasks.** I applied Tasks 8, 9, 10 and 13 on top and ran `pio run -e x4pro`. The result is
  under "Firmware build" below.

The plan code is otherwise sound. `PassageText`, the v4 `PassageDoc`, the capture filter, `snapSpan`,
`planTextRepair`/`RepairSchedule`, the rung ladder and the sampler all behave as specified. Five literal
defects stop the plan from being executed as written, though, and all five are fixable inline.

**FILES coverage.** Every file any step creates, modifies or deletes appears on a `FILES:` line at
column 0 outside a code block (`plan:17-31`). The directory prefixes `test/passage_doc/`,
`test/unit_text/`, `test/study_sleep_pick/` and `test/passage_label/` cover the new test files and
their `CMakeLists.txt` edits. I found no missing entry.

**Spec mapping.** A1–A24 each map to a step:

| Spec | Tasks |
|---|---|
| A1, A21, A22 | 8, 9, 10 |
| A2 | 6 |
| A4–A7 | 5 |
| A8–A11 | 3 |
| A12 | 14 |
| A13–A16 | 11, 12, 13 |
| A17–A20 | 7, 9 |
| A24 | 1, 2, 4, 8, 9, 13 |

The plan moves the spec's proposed `test/passage_span/` and `test/text_repair/` suites into
`test/unit_text/`, so `test/CMakeLists.txt` needs no new lines. The plan explains this (`plan:55-59`),
and it is reasonable. The two gaps are MINOR 1 and MINOR 2.

## MAJOR

### M1: Task 2's `sed` rewrites a test the same task just added, and the suite stops compiling

- **Claim.** Task 2 Step 6 converts `x.displayText = …;` assignments to `.assign(…)` with
  `sed -i '' -E 's/\.displayText = ([^;]+);/.displayText.assign(\1);/' test/passage_doc/PassageDocTest.cpp`
  (`plan:636`). The plan says this makes the existing tests compile.
- **Problem.** Step 1 has already appended `CopyPassageReportsAFailedTextCopy` to that same file. It
  contains `target.displayText = study::PassageText(DOC_FAILING);` (`plan:451`), and the `sed` turns it
  into `target.displayText.assign(study::PassageText(DOC_FAILING));`. That does not compile.
- **Evidence.** Reproduced in scratch: `PassageDocTest.cpp:876:29: error: no viable conversion from
  'study::PassageText' to 'std::string_view'`. With that one line restored, all 77 PassageDoc tests,
  69 StorageIo tests and 14 MigrationPlanner tests pass.
- **Fix.** Either:
  - run the Step 6 `sed` before Step 1 appends the new tests, or
  - exclude the line from the `sed`:
    `sed -i '' -E '/PassageText\(/!s/\.displayText = ([^;]+);/.displayText.assign(\1);/'`.

### M2: Task 2's `MigrationRunner` edit still copies a move-only passage, and the firmware doesn't compile

- **Claim.** Replacing `passages.add(*planned.passage)` with
  `passages.add(std::move(*planned.passage))` makes the migration path work with a move-only
  `TaggedPassage` (`plan:720-730`).
- **Problem.** `planned` is declared `const auto planned = study::planMigration(in, flat);`
  (`src/study/MigrationRunner.cpp:326`). `std::move` of a const lvalue is a const rvalue, so this
  still selects the deleted copy constructor. The plan never builds the firmware until Task 15, so
  nothing catches this before then.
- **Evidence.** Reproduced with `pio run -e x4pro`: `src/study/MigrationRunner.cpp:357:24: error: use
  of deleted function 'study::TaggedPassage::TaggedPassage(const study::TaggedPassage&)'`.
- **Fix.** In the same Task 2 Step 7, change `MigrationRunner.cpp:326` to
  `auto planned = study::planMigration(in, flat);`. Nothing reads `planned.passage` after the `add`
  (`MigrationRunner.cpp:343-362`). Add `src/study/MigrationRunner.cpp:326` to the step's text. The
  file is already on a `FILES:` line.

### M3: `SetWholeTextOverTheBudgetLeavesThePassageExactlyAsItWas` fails, and "no per-text cap" is not true

- **Claim.** A 200,000-byte whole text makes `setWholeText` return `TextResult::OverBudget`
  (`plan:948-957`). Task 14's docs say a whole text "may be any length, and is bounded only by the
  file's 200,000-byte save budget" (`plan:4696-4697`), which repeats spec A9/A11 (`spec:129-131`).
- **Problem.** ArduinoJson 7.4.2 stores a string's length in 2 bytes on 32/64-bit targets: host and
  ESP32 both have `ARDUINOJSON_STRING_LENGTH_SIZE 2`, "up to 65535 characters"
  (`.pio/libdeps/x4pro/ArduinoJson/src/ArduinoJson/Configuration.hpp:140-146`), and nothing in
  `platformio.ini` or `test/CMakeLists.txt` overrides it. Any `"w"` over 65,535 bytes therefore
  overflows the measuring document. `measureBytes()` returns `SIZE_MAX` and `setWholeText` returns
  `OutOfMemory`.
- **Evidence.** Reproduced: `Expected equality … setWholeText(…) Which is: 1-byte object <02>` against
  `OverBudget <03>`. It is the only failure in the full host `ctest` run after M1, M2 and M4 are fixed.
  The limit fails closed: the text is refused, never cut. No real selection comes near 64 KB, since a
  selection never crosses one spine document. So there is no data risk. The test is wrong, and the
  docs and header comments state a guarantee the library does not give.
- **Fix.**
  - Build the over-budget case from rows each under 64 KB. For example, add three whole rows of
    60,000 bytes, then `setWholeText` a 60,000-byte text on a fourth row.
  - Add a test that a 70,000-byte text is refused and nothing is cut.
  - State the ArduinoJson ~64 KB string limit in the `V3_MAX_DISPLAY_TEXT_BYTES` / `SAVE_BYTE_BUDGET`
    comment in `PassageDoc.h` and in the Task 14 `w` paragraph. Note that `setWholeText` reports that
    case as `OutOfMemory`, so the repair log will say "out of memory" for it.
  - This does not reverse spec A9's decision (still no cut, still refused). It corrects a factual bound.

### M4: Task 4's new tests don't compile, because `fileBytes` returns an optional

- **Claim.** Tasks `ALoadThatRunsOutOfMemoryFailsAndLeavesTheFile` and
  `ASaveThatRunsOutOfMemoryIsRefusedAndWritesNothing` declare
  `const std::string before = storage_fake::fileBytes(PATH);` (`plan:1639`, `plan:1653`).
- **Problem.** `storage_fake::fileBytes` is `std::optional<std::string> fileBytes(const std::string&)`
  (`test/stubs/HalStorageFake.h:25`).
- **Evidence.** Reproduced: `PassageFileTest.cpp:206:21: error: no viable conversion from
  'std::optional<std::string>' to 'const std::string'`, and the same error at line 220. With `auto`,
  all 73 StorageIo tests pass, including the four new ones.
- **Fix.** Change both lines to `const auto before = storage_fake::fileBytes(PATH);`. The later
  `EXPECT_EQ(storage_fake::fileBytes(PATH), before)` then compares optionals and needs no other change.

### M5: Three commits leave the firmware uncompilable, which the plan's own convention forbids

- **Claim.** "Firmware call sites are edited in the same task that changes a type they use, so every
  commit compiles" (`plan:84-85`).
- **Problem.** The plan contradicts that claim three times:
  - Task 9 changes `addPassage`'s signature, and its note admits `PassageSelectActivity.cpp` "still
    calls the old 6-argument `addPassage` until Task 10" (`plan:3483-3484`).
  - Task 11 removes `FitSize` and `sizeIndex`, and admits "`StudySleepScreen.cpp` does not compile
    against this header until Task 13" (`plan:3871-3872`).
  - Task 12 removes `TEXT_CAPACITY`, `wholeTextFits` and the five-argument `offer`, which
    `StudySleepScreen.cpp:68`, `:112` and `:131` still use until Task 13.
- **Evidence.** Commits 9, 11 and 12 each leave `pio run` broken. The brief asks that every step leave
  the tree working and committable. Nothing is lost, because the PR is squash-merged, but no one can
  bisect across those commits and the plan's stated invariant is false.
- **Fix.** Fold Task 10 into Task 9 so there is one commit for the signature change and its only
  caller. Land Task 13's `StudySleepScreen.cpp` replacement in the same commit as Tasks 11 and 12: do
  11 and 12 test-first, then 13, then commit once. Or keep Task 13's edits in a working tree that is
  not committed until the step after 12. Then change `plan:84-85` to state what is actually true.

## MINOR

### m1: The two-phase repair pass (spec A18) is flattened into one ordered loop

- **Claim.** The spec requires "Two phases per pass: 1. rows whose units resolve at their stored spine
  hint; 2. rows that need the Verse book search … capped at 2" (`spec:219-226`).
- **Problem.** `repairTexts` walks `repairSchedule_.order(...)` once (`plan:3343-3361`), mixing hint
  rows and search rows in cursor order. Progress is still bounded, because of the search cap and the
  rotating cursor. But a search row stored early can spend most of the 3 s budget before cheaper hint
  rows after it run. The plan does not mention the deviation.
- **Fix.** Walk `order` twice: hint rows first, then searchable rows under `searchesLeft`. Or record
  the single-loop choice as a deliberate deviation.

### m2: Device check 6's prefilter-capacity log is missing

- **Claim.** The spec says to "Log the prefilter's measured capacity at rung 7 once (`LOG_DBG`) to
  confirm A14's estimate" (`spec:478-479`, `spec:177-180`).
- **Problem.** Task 13 adds no such log. `rowsUnfit` counts prefilter rejections and measured misfits
  together (`plan:4329-4330`, `plan:4305-4307`). The plan's check 6 is reworded to "confirm the
  prefilter never rejects a text that visibly fits" (`plan:4785-4786`), and nothing on the device lets
  a tester do that.
- **Fix.**
  - Count `rowsOverPrefilter` separately in `ScanTotals`.
  - After building `gate`, add one `LOG_DBG` with the floor rung's line budget
    (`gate.floor.maxHeight / gate.floor.lineHeight`) and a chars-per-line estimate, for example
    `gate.width` divided by the measured width of a 10-character sample.

### m3: The `repairTexts` orchestration has no host test

- **Claim.** Spec M4's test and its error-table row "Repair: save fails → all rows restored"
  (`spec:423`, `spec:462-463`) describe pass-level behaviour.
- **Problem.** The plan tests only the pure pieces: `RepairSchedule` (`plan:2713-2742`) and
  `PassageDoc::replace` (`plan:973-984`). The loop that combines them has no host test (`plan:3319-3424`):
  - the search cap and deferral without `markAttempted`,
  - the backup and abort on OOM,
  - the undo on save failure,
  - the spine-hint repair.
- **Fix.** Optional: extract the per-pass driver into `lib/StudyStore` with injected locate, text and
  save callbacks and test it there. At minimum, list these as device-verified in the hand-off.

### m4: Verification commands that don't do what they say

- **Target name.** `CrosspointStateTest` / `build/test/crosspoint_state/CrosspointStateTest`
  (`plan:1772-1774`) should be `CrossPointStateTest` (`test/crosspoint_state/CMakeLists.txt:4`). The
  plan hedges this in a parenthesis, but the command as written fails.
- **Task 10 Step 2 grep.** `grep -n "label\.\|appendWords\|passage_label" …` is expected to print
  nothing (`plan:3565-3566`). It matches the kept comment `// passage-only label.`
  (`PassageSelectActivity.h:97`, reproduced in scratch).
- **Task 10 Step 3 grep.** `--include=*.h --include=*.cpp` is unquoted (`plan:3572`). This host's
  shell is zsh, where that aborts with `no matches found` (reproduced). Quote the globs.

### m5: v1–v3 validation is stricter than the spec says

- **Claim.** The spec says "`fromJson` for a v1–v3 file: unchanged validation" (`spec:138`).
- **Problem.** `displayTextFromJson` refuses the whole load when a v1–v3 file carries `"h"`
  (`plan:1247-1248`), and the plan tests that it does (`LoadRefusesAWholeFlagInAnOlderVersion`,
  `plan:906-915`). No build writes such a file, so the practical risk is nil. It is still an
  unrecorded divergence that would latch saving off.
- **Fix.** Keep it and note it as intended (it matches the "refuse what this build would not have
  written" rule), or drop the check.

## Firmware build

I applied Tasks 2 (Step 7), 8, 9, 10 and 13 on top of the host tasks and ran
`~/.platformio/penv/bin/pio run -e x4pro`.

- As written, the build fails with the M2 error. That is the only one.
- With M2's one-word fix, it succeeds: `[SUCCESS]`, RAM 19.9%, Flash 84.0% (5,507,814 of
  6,553,600 B). The files this plan touches produce no warnings.

Everything else the plan writes for the firmware compiles as given, including:

- `PsramJsonAllocator` and `rangeText`,
- the new `addPassage` and `repairTexts`,
- the `PassageSelectActivity` edits,
- the full `StudySleepScreen.cpp` replacement.

The render-lock claim (`plan:3468-3471`) holds: `loadBook` holds no `RenderLock`
(`EpubReaderActivity.cpp:143-244`). The first `RenderLock` in that file is at `:247`.

## Verdict

Every finding is fixable inline. None reverses a spec decision, changes scope or needs a judgment only
the human can make. M3 corrects a documented bound but keeps the spec's refuse-never-cut rule. With
M1–M5 applied, the plan can be executed literally.

VERDICT: CLEAR
