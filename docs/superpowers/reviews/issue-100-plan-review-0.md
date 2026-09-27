Tier: heavy

# Plan review 0 — streaming store writer (issue #100)

Plan: `docs/superpowers/plans/2026-09-27-issue-100-plan.md`
Spec: `docs/superpowers/specs/2026-09-27-issue-100-design.md`
Base: `7124f139`

## How this was checked

I did more than read the plan. I executed it literally, step by step, in a throwaway
`git worktree` of `7124f139`. I made each edit by exact-string replacement of the anchors
the plan quotes, so every anchor had to match exactly once. After each step I ran the
plan's own commands. The worktree has since been removed. Observed results:

| Step | Plan expects | Observed |
|---|---|---|
| 0 baseline | 59 / 7 | 59 / 7 |
| 1a | compile error, `failWritesAfter` missing | as stated |
| 1c | 61 passed | 61 passed |
| 2a | both `*TargetsOwnParent*` tests fail | both failed |
| 2c | 63 / 7 | 63 / 7 |
| 3b | both `AShort*` tests fail | both failed |
| 3d | 65 / 7 | 65 / 7 |
| 3d mutation (`buffered.flush(); return true;`) | both `AShort*` fail | both failed |
| 4a | compile error, `readDocFromFileStreamed` missing | as stated (`AtomicWriteTest.cpp:93`) |
| 4b/4d | 66 / 7 | 66 / 7 |
| 5 | 67 passed | 67 passed |
| 7 host | ctest 100% | `100% tests passed out of 980` |
| 7 format | no changes | one change, see MINOR-1 |

I did not rebuild the device firmware (`pio run`). The plan records a prototype build, and the
new code uses only APIs already present on the device side:

- `HalFile::close()` returns `bool` (`lib/hal/HalStorage.h:94`).
- `openFileForWrite` opens with `O_TRUNC` (`freeink-sdk/libs/hardware/SDCardManager/src/SDCardManager.cpp:337`).
- `BufferedFileWriter::flush()` latches short writes (`lib/Serialization/BufferedFile.h:41,54-57,62`).

## Spec → step mapping

| Spec requirement | Step |
|---|---|
| A-1/A-2/A-3/A-7: the `JsonFileWriter` adaptor over a 512 B `BufferedFileWriter`, kept in the `.cpp`'s anonymous namespace, with `flush()` checked | 3c.2, 3c.3 |
| A-4/A-6: open, `flush()` and `close()` all checked, with an explicit close before the rename | 3c.3 (`writeDocStreamed`) |
| A-4b: no `measureJson` added | 3c (none added) |
| A-5: the failed `.tmp` is left in place | 3c.4, asserted in 3b's first test |
| A-8: lock taken per chunk | inherent in `HalFile::write`; nothing to do |
| A-9/A-10: parent directory derived from the path, in both writers; `writeDocToFile` keeps its `String` body | 2b |
| A-11: `readDocFromFileStreamed` logs under `"PERSIST"`; `PassageFile` uses it | 4b, 4c |
| A-12: the `failWritesAfter` hook, its own test, and clearing by `clearFailures`/`reset` | 1 (`reset()` replaces `FakeCard` whole, `HalStorageFake.cpp:61`) |
| A-13: sizes reported, not asserted | 7 |
| "Files touched" comment corrections (`PersistableStore.h`, `TempAdoption.h`, `BookmarkFile.cpp:64`, `HighlightFile.cpp:40-41`) | 6 |
| Both CMake include-path lines (MAJOR-1 of spec review 0) | 3a |
| TDD steps 1–5 | plan steps 1, 2, 3, 5, and the regression runs in every step |
| Device heap check using the scoped low-water mark | PR body |

Every spec requirement maps to a step.

Names and signatures stay consistent from step to step:

- `ensureParentDirectory`, `WRITE_BUFFER_BYTES`, `JsonFileWriter`, `serializeInto`,
  `writeDocStreamed` and `HalFileReader` are introduced once and referenced consistently.
- `readDocFromFileStreamed` matches `DocReader` (`PersistableStore.h:88`).

`FILES:` lines (plan lines 18-22):

- They are at column 0 and outside any code fence.
- Each entry is a repo-relative path.
- They cover every file any step edits: all 13 files the literal run touched.

## Findings

### MINOR-1 — Step 4c's deletion leaves a double blank line, so step 4 commits a file clang-format then changes

**Claim.** Plan lines 520-529 say the top of `PassageFile.cpp` "then reads" with a single blank
line between `MODULE` and `}  // namespace`. The preamble adds that `./bin/clang-format-fix`
changed nothing (plan line 11).

**Problem.** The instruction deletes "everything from the line `// ArduinoJson reader over
HalFile...` through the closing `}` of `readInto`" (plan lines 516-518). Taken literally, that
keeps both the blank line before the comment (`src/study/PassageFile.cpp:12`) and the one after
`readInto` (`:51`). The literal run produced two consecutive blank lines, and step 7's
`clang-format-fix` removed one.

The effects are small:

- The step-4 commit is not format-clean.
- Step 7's "expect no changes" is wrong.

Step 7 already covers this case ("If `clang-format-fix` changes anything, commit it as
`style: format (#100)`"), so nothing breaks.

**Evidence.** In the scratch run, `git diff` after `clang-format-fix` showed exactly one hunk,
in `src/study/PassageFile.cpp`, which removed a blank line after
`constexpr const char* MODULE = "PASSAGE";`.

**Fix.** Make step 4c.1 delete "through the closing `}` of `readInto` *and the blank line that
follows it*". Alternatively, add `src/study/PassageFile.cpp` to a `./bin/clang-format-fix -g`
run before the step-4 commit. The expected listing already shown is then correct.

## Not raised

- **Step 7 `git push`.** This matches every earlier pipeline plan (for example
  `2026-09-26-issue-112-plan.md` and `2026-09-26-issue-108-plan.md`). The branch already tracks
  `origin/fix/100-streaming-store-writer`, and `push.autoSetupRemote=true`. It is the
  pipeline's sanctioned implement-phase push, not a departure.
- **`PIO` path.** It points into another session's scratchpad. The file exists and is the
  batch's shared lock script, as the spec's gate names it.
- **`JsonFileWriter`'s `out_` member.** It matches `HalFileReader`'s existing `file_`, which
  moves verbatim from `PassageFile.cpp:27`. It is a suffix, not the prefix `CLAUDE.md` forbids.
- **Steps 5 and 6 do not start with a failing test.** The spec sanctions step 5 as a
  characterisation test (spec TDD step 4). Step 6 changes only comments.

VERDICT: CLEAR
