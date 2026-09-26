# Plan review, pass 0: issue #99

Plan: `docs/superpowers/plans/2026-09-26-issue-99-plan.md`
Spec: `docs/superpowers/specs/2026-09-26-issue-99-design.md`

## How this was checked

The plan says every code block was compiled and run. I didn't take that on trust.
I extracted every `Write ... with exactly this content` block and every bash block,
in plan order, and ran them in a scratch copy of the tree at `45ee4c01`. The copy
was outside the worktree, and GoogleTest and ArduinoJson came from the worktree's
`build/test/_deps` through `FETCHCONTENT_SOURCE_DIR_*`. I skipped only the fetch,
the push and `gh`. Results:

| Step | Plan expects | Observed |
|---|---|---|
| 0.1 baseline | 851 | 851 |
| 1.2 red | link failure on `HalStorage`/`HalFile`/`storage_fake` symbols | link failure, those symbols |
| 1.3 green | 15 passed | 15 passed |
| 1.4 pagination | 8 passed; warnings only at `TextBlock.cpp:393`, `ParsedText.cpp:968,970` | exactly those three warnings; 8 passed |
| after commit 1, full tree | (implicit: tree works) | 866/866 passed |
| 2.1 red | undefined `obfuscation::deobfuscateFromBase64` | exactly that |
| 2.2 / 2.3 | 20 passed; the two rename tests fail under sabotage | same |
| 3.1 / 3.2 | 30 passed; the four named tests fail under sabotage | same four |
| 4.1 / 4.2 | 36 passed, one `MODULE` warning; round-trip test fails under sabotage | same |
| 5.1 / 5.2 | nine `edited` lines; `comments only` | same |
| 6.1 | N + 36 | 887/887 |
| 6.2 ASan/UBSan | 36 and 8 passed, clean | same, no `runtime error` |
| 6.3 full-tree format | `rc=0`, only ` M test/CMakeLists.txt` | same |

**Spec coverage.** Every row of the spec's Files touched table (spec `:174-193`) is
on a `FILES:` line (plan `:30-37`).

- Every test bullet in spec §Testing strategy (`:415-474`) maps to a named test:
  10 fake, 5 atomic write, 9 adopting read plus the 45,000-byte control, and 6
  TagPaletteFile.
- A-1 through A-20 are each honoured:
  - The stub header matches `lib/hal/HalStorage.h:13-98` signature for signature,
    minus the three omissions A-2 names.
  - The defined subset matches spec `:233-248`.
  - The failure hooks match the table at spec `:377-381`.
  - The shared-file line and the #98 note are in the PR body.
- The plan's names are consistent from step to step: `storage_fake::*`,
  `PATH`/`TMP_PATH`, `DocReadStatus`, and `TagPaletteFile::{SaveResult,LoadResult}`.

**`FILES:` lines.** They're at column 0, outside any code block, and use repo-relative
paths or `dir/` prefixes. No step commits a file outside them.

**Considered and not ranked: `test/CMakeLists.txt`.** Step 0.2 edits
`test/CMakeLists.txt` (plan `:85-100`), and that file isn't on a `FILES:` line. The
plan explains why at `:39-42`, and spec A-14 plus `.claude/agents/data-dev.md:22-27`
require it: the file is a shared append point that the orchestrator applies. The
edit is never staged, so it can't reach a commit or collide with a sibling. Locking
it would serialise this task behind every sibling that needs the same append
point, which is the opposite of what the lock is for. I don't count it as an
undeclared touched file.

**Considered and not ranked: Task 7 pushes and opens the PR.** The pipeline's
`implement` phase requires this: its signal is `pr`, and `prompts/implement.md`
says "open the PR ... Push before your turn ends". It's sanctioned here, unlike
the ungated PR step the issue-63 plan review flagged.

---

## MINOR

### 1. The per-commit `clang-format-fix -g` never formats anything this plan creates

**Claim.** Ground rules (plan `:60-61`) and each commit step (`:879-880`, `:1114-1115`,
`:1312-1313`, `:1521-1522`, `:1649`) say to `git add` new files and then run
`./bin/clang-format-fix -g`, which then formats them.

**Problem.** `-g` becomes `git ls-files --modified` (`bin/clang-format-fix:22-24,48`).
That lists only files whose working tree differs from the index. Once a new file,
or a staged edit, has been `git add`ed, it matches the index and is skipped. So the
per-commit format step is a no-op for every file the plan writes. Only Step 6.3's
full-tree run formats them. If that run reformats anything, there's no step to
commit the result, because Step 6.3 just expects a clean tree.

It is harmless as written, because every block is already formatted: Step 6.3 was
clean in my run. It stops being harmless the moment the implementer adapts code,
for example when Task 5's asserts fire because a sibling changed a comment.

**Evidence.** In the scratch tree:

1. I staged `test/stubs/zz.cpp` containing `int   x  =1;`.
2. `git ls-files --modified` printed only `test/CMakeLists.txt`.
3. After `./bin/clang-format-fix -g`, the file was unchanged.

**Fix.**

- In each commit step, replace `./bin/clang-format-fix -g && git diff --stat` with
  `./bin/clang-format-fix && git diff --stat -- . ':!test/CMakeLists.txt'`, and
  `git add` whatever it prints before committing.
- Correct the ground rule at `:60-61`: `-g` reaches only unstaged edits to tracked
  files.
- Give Step 6.3 an action for a non-clean result: commit it as `style:`.
- The project memory note "Format and check gates fail open" makes the same
  `git add first` claim. It's worth correcting after this lands.

### 2. "`git diff --stat` must print nothing" can never be true here

**Claim.** Plan `:892-894`: "After formatting, `git diff --stat` must print nothing.
If it prints anything, `git add` the reformatted files before committing." The same
pattern appears in Tasks 2-5.

**Problem.** Step 0.2 deliberately leaves `test/CMakeLists.txt` modified and unstaged,
so `git diff --stat` always prints `test/CMakeLists.txt | 1 +`. An implementer
following the text literally is told to `git add` the file the plan forbids staging
(`:55-57`). The ` M test/CMakeLists.txt` status check right after catches it, but
the two instructions contradict each other.

**Evidence.** After every commit step in the scratch run, `git diff --stat` printed
`test/CMakeLists.txt | 1 +`.

**Fix.** Use `git diff --stat -- . ':!test/CMakeLists.txt'`, as in finding 1, and say
"must print nothing" of that command.

### 3. Step 2.3's sabotage doesn't show that any Task 2 test can fail

**Claim.** Plan `:900-902`: "The red step here is the link, and Step 2.3 proves the
tests can fail."

**Problem.** Step 2.3 makes `rename` overwrite its destination. The only tests that
catch that are the two Task 1 fake tests (plan `:1103-1104`). Every `AtomicWrite`
test still passes under it, because `writeDocToFileAtomic` removes the primary
before renaming. So no `AtomicWrite` assertion is ever shown going red. Task 3.1 has
no red step at all, although Step 3.2 does cover four of its tests.

**Evidence.** In the scratch run of Step 2.3, the only failures were
`HalStorageFake.RenameIntoAMissingDirectoryFails` and
`HalStorageFake.RenameOntoAnExistingFileFailsAndKeepsBoth`. The other 18 passed,
all five `AtomicWrite.*` tests among them.

**Fix.** Change the sabotage to disable the rename hook:
`s/if (card().failRename.count(oldPath) != 0) return false;//`. Expect
`AtomicWrite.AFailedRenameLeavesOnlyTheTempWhichTheNextReadAdopts`,
`AdoptingRead.AFailedPromotionStillReturnsTheDocumentAndKeepsTheTemp` (from Task 3
onward) and `HalStorageFake.AFailedRenameChangesNothing` to fail. Alternatively,
keep the current sabotage but reword `:900-902` so it doesn't claim Task 2's tests
are proven.

### 4. Step 5.2's `ctest | tail -1` shows the timing line, not the result

**Claim.** Plan `:1646` runs `ctest ... | tail -1` and `:1660` expects "every test
passes".

**Problem.** The last line of ctest output is `Total Test time (real) = ...`, so the
pass count is never shown. The implementer can't read the expected result from the
command's own output.

**Evidence.** The scratch run of Step 5.2 printed `Total Test time (real) =   0.39 sec`.

**Fix.** Use `| tail -3`, as Task 0.1 and Step 6.1 already do, and expect
`100% tests passed out of N + 36`.

### 5. Step 6.4 diffs against `origin/main` with two dots

**Claim.** Plan `:1702`: `git diff origin/main --stat -- lib src`, expected to show
"exactly the three comment-only headers".

**Problem.** Step 0.1 fetches `origin`, and the ground rules (`:48-53`) expect siblings,
#98 in particular, to land on `main` while this task waits. A two-dot diff against a
`main` that has moved past the merge base also shows every sibling's `lib/`/`src/`
change, reversed. Then the check fails, or someone misreads it as this branch
touching production code.

**Evidence.** Branch base: `git merge-base HEAD origin/main` = `2f303f6f`.

**Fix.** `git diff origin/main...HEAD --stat -- lib src`.

### 6. The PR body miscounts the comment-only test files

**Claim.** Plan `:1751-1752`: "three `lib/`/`src/` headers and five test files".

**Problem.** Task 5 edits six test files (plan `:1546-1628`):

- `TempAdoptionTest.cpp`
- two in `highlight_file`
- two in `bookmark_save_action`
- one in `credential_integrity`

Task 1 also changes comments in `test/pagination/CMakeLists.txt`.

**Evidence.** The scratch run's Task 5 commit reported `9 files changed`: 3 headers
plus 6 test files.

**Fix.** Say "six test files".

### 7. The stub `String` member is named `s_`

**Claim.** Plan `:143-166` names the private member `s_`.

**Problem.** `CLAUDE.md`, Naming: "Private members: `memberVariable`, no prefix". The
trailing underscore is the same convention in suffix form, and the name says
nothing about the content. The spec (`:279-291`) carries the same name, so this
isn't a plan-only divergence, but it is cheap to fix before it lands.

**Evidence.** `grep -rn '[a-z]_;' test/stubs/*.h` returns no hits on the current tree,
so no existing stub uses the style.

**Fix.** Rename `s_` to `text` throughout the class in Step 1.1.

---

No BLOCKER and no MAJOR. The plan executes literally to the spec: every expected
output I could reproduce matched, and every commit leaves a tree that builds and
passes. The findings above are mechanical and can be fixed inline.

VERDICT: CLEAR
