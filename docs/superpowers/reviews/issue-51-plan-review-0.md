# Adversarial review — issue #51 implementation plan, pass 0

**Target:** `docs/superpowers/plans/2026-09-17-issue-51-plan.md`
**Against:** `docs/superpowers/specs/2026-09-17-issue-51-design.md` (cleared spec pass 0, revised in
pass 1)
**Branch:** `fix/51-adopt-orphaned-tmp` @ `48ece824` (docs only; `git status --short` clean)
**Date:** 2026-09-16
**Counts:** 0 BLOCKERs, 2 MAJORs, 7 MINORs

## What was checked, and how

Every identifier the plan writes was traced to the file it has to compile against:
`lib/Serialization/{PersistableStore.h,PersistableStore.cpp,DocReadStatus.h,SaveBudget.h}`,
`lib/hal/HalStorage.h`, `src/util/{HighlightFileAction.h,HighlightFile.h,HighlightFile.cpp,BookmarkFile.cpp,BookmarkSaveAction.h}`,
`src/study/{TagPaletteFile.cpp,PassageFile.cpp,PubKeyRegistry.cpp,MigrationRunner.cpp}`,
`src/network/MeetingWeekCache.cpp`, `test/CMakeLists.txt`, `test/highlight_file/*`,
`test/save_budget/CMakeLists.txt`, `.github/workflows/ci.yml`, `platformio.ini` and
`.claude/agents/data-dev.md`.

Four claims were executed rather than reasoned about:

- **The plan's own code was built and run.** `lib/Serialization/TempAdoption.h` (plan:166-208 +
  plan:315-332), `test/temp_adoption/CMakeLists.txt` (plan:76-91) and
  `test/temp_adoption/TempAdoptionTest.cpp` (plan:95-138 + plan:244-291) were written into the
  worktree verbatim, `add_subdirectory(temp_adoption)` appended, and built. Result, byte for byte
  what plan:339 predicts:

  ```
  [==========] 12 tests from 2 test suites ran. (0 ms total)
  [  PASSED  ] 12 tests.
  ```

  All three scratch files were removed and `test/CMakeLists.txt` reverted; the tree is as found.
- **Step 5's gate claim was falsified by building it** — see MAJOR 1.
- `add_subdirectory(save_budget)` really is `test/CMakeLists.txt:96`, the two CI anchors the plan
  cites are right (`ci.yml:53-56` runs the unsuffixed `clang-format-fix`; `ci.yml:187-194`
  configures, builds and `ctest`s the host suite), `.venv/bin/clang-format` reports
  `clang-format version 21.1.8` and `ci.yml:45-51` installs clang-format-21.
- `PATH="$PWD/.venv/bin:$PATH" .venv/bin/clang-format --dry-run -Werror` over the plan's two new
  files reports exactly two violations, both in the test snippet at plan:262-269 (the two-line
  `EXPECT_EQ`s fit in the 120-column limit and get joined). Step 11 absorbs them; not a finding.

Anchors verified byte for byte in the working tree: `HighlightFileAction.h:26-47` (the load half),
`:52-56` (the save half), `:3` (`#include <DocReadStatus.h>`); the four `switch` lines
`HighlightFile.cpp:40`, `BookmarkFile.cpp:55`, `TagPaletteFile.cpp:39`, `PassageFile.cpp:72`; the
four includes `HighlightFile.cpp:9`, `BookmarkFile.cpp:12`, `TagPaletteFile.cpp:11`,
`PassageFile.cpp:8`; `PersistableStore.h:10,63,66,164` and `.cpp:38,39,46-61,63-65`;
`HighlightFileActionTest.cpp:1-12` (header comment), `:18-51` (six load tests), `:52-66` (three save
tests). `Storage.exists/remove/rename` exist with the signatures step 8 uses
(`lib/hal/HalStorage.h:35-37`). Step 10's grep expectation is exactly right: after the change
`grep -rn readDocFromFileChecked src` returns precisely `HighlightFile.cpp:28,36`,
`BookmarkFile.cpp:41,49`, `TagPaletteFile.cpp:27,35`. A-7's premise holds — `readDocFromFile`'s only
caller is `PersistableStore.h:164`, and `unusedFunction` is suppressed in `platformio.ini:24`, so
keeping it cannot trip cppcheck.

Spec-review-0 follow-through was checked finding by finding. MAJORs 1-3 and MINORs 1, 2, 5, 7, 8 are
fully applied to the spec and correctly reflected in the plan. MINOR 3 and MINOR 6 are applied to the
argument but not everywhere in the spec text — see MINORs 1 and 2 below; in both cases the *plan*
takes the correct branch.

---

## MAJOR 1 — step 5 leaves the host suite unbuildable, and its gate is written so it cannot notice

**Claim.** plan:8: *"Each is 2–5 minutes, leaves the tree building, and ends in a commit."* Step 5's
gate, plan:408-413: *"**Gate.** The host suite still passes (nothing it covers moved), and firmware
builds: `ctest --test-dir build/test --output-on-failure -j` / `pio run`."*

**Problem.** Step 5 deletes `HighlightLoadAction` and `highlightLoadAction` from
`src/util/HighlightFileAction.h` (plan:359-360). `test/highlight_file/HighlightFileActionTest.cpp`
still contains six tests that name both, plus `DocReadStatus`, which step 5 also drops from that
header's includes — and the step that deletes those tests is **step 6**, one commit later
(plan:430). So the step-5 commit does not compile. It is not committable in CI terms either: CI's
host job runs `cmake --build build/test` (`ci.yml:190-191`) over the default target, which includes
`HighlightFileActionTest`.

The gate cannot catch it, because `ctest --test-dir build/test` does not build — it re-runs whatever
binaries are already in `build/test`, which at that point are the stale ones from step 4. The plan
would report green on a tree that does not compile. The same shape appears at step 8's gate
(plan:580-583), where `ctest` again follows no `cmake --build`; there it is harmless only because
step 8 changes nothing the host suite sees.

**Evidence.** Step 5's header edit was applied to the working tree exactly as plan:359-382 describes
and the suite built:

```
$ cmake --build build/test --target HighlightFileActionTest
.../test/highlight_file/HighlightFileActionTest.cpp:28:94: error: no member named 'ReportFailed' in 'HighlightSaveAction'
.../test/highlight_file/HighlightFileActionTest.cpp:33:33: error: use of undeclared identifier 'DocReadStatus'
fatal error: too many errors emitted, stopping now
20 errors generated.
make: *** [HighlightFileActionTest] Error 2
```

(The scratch edit was reverted; `git status --short` is clean.) The donor tests are
`HighlightFileActionTest.cpp:18-49`, and `test/highlight_file/CMakeLists.txt:9-11` puts
`${REPO_ROOT}/src` on that suite's include path, so it compiles the very header step 5 empties.

**Concrete fix.** Two edits, both mechanical:

1. **Run step 6 before step 5.** After step 4 the renamed tests already exist and pass in
   `test/temp_adoption/`, so deleting the six donor tests from `test/highlight_file/` is safe at that
   point and leaves that suite at three green tests. Step 5 then removes the load half from a header
   nothing references any more. Nothing else in either step depends on the other order — step 6's
   reworded comments point at `test/temp_adoption/`, which exists from step 1, and step 5's grep
   sanity-check (plan:404) still returns nothing once both are done.
2. **Make every gate build before it tests.** Replace the bare `ctest` line in steps 5 and 8 with
   `cmake --build build/test -j8 && ctest --test-dir build/test --output-on-failure -j`, as step 6
   (plan:464) and step 12 (plan:718-719) already do. Then correct step 5's gate prose: with the
   reorder, "the host suite still passes" is true, but only because step 6 went first.

---

## MAJOR 2 — the spec's single-task invariant, which the spec assigns to *this* change, appears in no step

**Claim.** Spec `:398-412`, §Concurrency: *"**The invariant, stated so it can be re-checked rather
than re-derived:** Every reader and every writer of the eight files in scope runs on the Arduino loop
task … The three direct callers — `/.berean/pubkeys.json`, `/.berean/migration-ledger.json`,
`/.berean/meeting-weeks.json` — have **no lock at all** and rely on the single-task property alone."*
And `:410-412`: *"**This change is what makes a read path mutate, so this invariant is its
responsibility.** Any PR that introduces a background task touching `/.berean/` must either give
these three files a mutex or revert them to `readDocFromFileChecked`."*

**Problem.** The plan never carries it anywhere a future reader will look. Step 8's declaration
comment (plan:519-529) and its two in-switch comments (plan:550-551, 558-566) cover A-12, A-10 and
the promote-first rule; none mentions tasks or locking. Step 10 (plan:633-675) switches
`PubKeyRegistry`, `MigrationRunner` and `MeetingWeekCache` — the three unlocked files the invariant
is *about* — and says only "argument lists unchanged". The PR body's follow-up list (plan:769-779)
files the budget/status defect and two stale comments, and is silent on this.

The result of executing the plan literally is a merged tree in which a read path now renames files
with no lock, and the one sentence explaining why that is safe lives only in a spec document. That is
the failure mode the spec explicitly wrote the invariant to prevent ("stated so it can be re-checked
rather than re-derived"), and it is the same class of durable constraint that CLAUDE.md's comment
policy says *does* earn a comment: a non-obvious why that would otherwise look arbitrary. The plan is
otherwise scrupulous about migrating spec rationale into code comments — A-12 at plan:563-566, A-10
at plan:559-561, A-3 at plan:316-319 — which makes this omission the odd one out rather than a
stylistic choice.

**Evidence.**
- Spec `:398-412` — the invariant and its assignment to this change.
- plan:510-590 (step 8), plan:633-675 (step 10) — no mention of task affinity or `storageMutex` in
  any added comment.
- `src/study/PubKeyRegistry.cpp:23,52,78`, `src/study/MigrationRunner.cpp:55,74`,
  `src/network/MeetingWeekCache.cpp:23` — none of the three files takes a lock of its own; confirmed
  by reading them.
- `.claude/agents/data-dev.md:49` — *"Name the owning task and hold `storageMutex` on write"* — is
  the surface rule the invariant discharges.

**Concrete fix.** Add to step 8, at the end of the `readDocFromFileAdopting` declaration comment in
`PersistableStore.h`:

```cpp
  // Safe without a lock of its own only because every reader and writer of the
  // adopting files runs on the Arduino loop task (the CRTP stores additionally
  // hold storeMutex). This is the first read path that renames: if a background
  // task ever touches /.berean/, give those files a mutex or move them back to
  // readDocFromFileChecked.
```

and one line to the PR body's follow-up section recording the same constraint, so the reviewer of the
next background-task PR meets it.

---

## MINOR 1 — the spec still contradicts the plan on the compatibility shim

Spec `:197` (§A-2) says *"`src/util/HighlightFileAction.h` keeps only the save half (`:52-56`).
**No compatibility shim.**"*, which is spec-review MINOR 3 correctly applied. But the Files-touched
table two screens later, spec `:217`, was not updated with it: *"`src/util/HighlightFileAction.h` |
load half removed; **includes `<TempAdoption.h>`**; save half unchanged"*. That clause is the pass-0
shim MINOR 3 removed. The plan takes the right branch and says so emphatically (plan:383-385, *"**No
shim.** … A header that silently re-exports the new home would leave `BookmarkFile`, `TagPaletteFile`
and `PassageFile` still including a highlights header"*), so nothing in the plan needs changing —
but an implementer who diffs plan against spec meets a direct contradiction on a line the plan
flagged in bold. **Fix:** delete `includes <TempAdoption.h>;` from spec `:217`.

## MINOR 2 — A-12 is not applied to the spec's hardware-test procedure

Spec `:545`, the human-tester steps: *"Repeat with the `.tmp` hand-truncated: expected `Missing`,
defaults, `.tmp` deleted."* A-12 (spec `:312-335`) is precisely that the new function **never**
deletes; the correct expectation is that the `.tmp` is still on the card. Spec `:544` ("the `.tmp` is
gone") is fine — that one is consumed by the rename. The plan's PR body has it right
(plan:793-794, *"Repeat with the `.tmp` hand-truncated: expected defaults, and the `.tmp` still on
the card"*), so again the plan is correct and the spec line is the leftover. **Fix:** change spec
`:545` to "expected `Missing`, defaults, and the `.tmp` still present".

## MINOR 3 — step 7 repoints two signposts whose referent does not move

plan:483 justifies the three edits with *"these three point at `util/HighlightFileAction.h` as the
home of a rule that is no longer there."* True of `src/util/HighlightFile.h:12` ("the `.tmp`
promotion decision … live in util/HighlightFileAction.h"). Not true of the other two:

- `src/util/BookmarkSaveAction.h:9-10` — *"`util/HighlightFileAction.h` documents why there is no
  stub"*. That is a pointer to the **Arduino / no-host-stub** reasoning, not to the load rule.
- `test/bookmark_save_action/BookmarkSaveActionTest.cpp:5` — *"See the comment atop
  util/HighlightFileAction.h for the full reasoning"*, same subject.

Step 5's own replacement comment (plan:368-375) deliberately **keeps** that reasoning in
`HighlightFileAction.h` (*"HighlightFile.cpp itself cannot be built on the host — it includes
`<PersistableStore.h>`, which includes `<Arduino.h>` unconditionally"*), while `TempAdoption.h`'s
comment (plan:173-180) carries only the compressed form and never mentions the missing stub. So
repointing those two at `Serialization/TempAdoption.h` sends the reader to a header that does not
answer their question. **Fix:** in step 7, edit only `src/util/HighlightFile.h:12`, and leave
`BookmarkSaveAction.h:9` and `BookmarkSaveActionTest.cpp:5` pointing where they do — or, if the
"one canonical explanation" is wanted, move the no-stub paragraph into `TempAdoption.h` in step 2 and
say so.

## MINOR 4 — step 7 names an edit at `HighlightFile.h:34` that has no content

plan:485-486: *"`src/util/HighlightFile.h:12` and `:34` — references to the load decision and to
`readDocFromFile`. Repoint the load-decision reference to `Serialization/TempAdoption.h`."* `:34` is
`LoadResult load(const std::string& bookPath, HighlightDoc& doc);` — no reference at all. The
`readDocFromFile` mention is `:33`, inside the `LoadResult` doc comment: *"collapsing these four
states into one is precisely the bug the foundations plan fixed in readDocFromFile."* That sentence
is **not** stale after this change: A-7 keeps `readDocFromFile` (plan:612-614), and the sentence is
about `readDocFromFileChecked`'s introduction, not about where the `.tmp` rule lives. So the step
lists a second edit, gives no instruction for it, and none is needed. (Inherited from spec `:225`.)
**Fix:** drop `:34` from step 7's list and from the spec's files-touched row, leaving
`HighlightFile.h:12` as the only edit in that file.

## MINOR 5 — step 7's grep expectation cannot match

plan:495-499:

```sh
grep -rn "HighlightFileAction" src test lib
# expect: only src/util/HighlightFile.cpp:9, src/util/HighlightFileAction.h,
#         test/highlight_file/CMakeLists.txt and HighlightFileActionTest.cpp
```

`grep -rn` matches file *contents*; `src/util/HighlightFileAction.h` contains no line with its own
name, before or after step 5 (verified — the current file's only self-reference is the filename).
The real output after steps 5-7 is `src/util/HighlightFile.cpp:9`,
`test/highlight_file/HighlightFileActionTest.cpp:16` plus its reworded header comment, and
`test/highlight_file/CMakeLists.txt:1,4,5,6,9,14,19` (the target name `HighlightFileActionTest`
matches the pattern seven times). A literal implementer gets an output that does not resemble the
expectation and has to decide whether that is a problem. **Fix:** restate the expectation as the
four *files* rather than the hits, or narrow the pattern to
`grep -rn "util/HighlightFileAction.h" src test lib`.

## MINOR 6 — `pio check` is never run, and CI fails on `low` defects

Step 12 (plan:712-729) presents itself as *"Full verification, in this order"* and runs the host
suite, `pio run`, `clang-format-fix` and `git status`. It omits `pio check`. CI runs
`pio check --fail-on-defect low --fail-on-defect medium --fail-on-defect high`
(`.github/workflows/ci.yml:95`) with no `-e`, so it resolves `default_envs`, and it is **not**
exercised by `pio run` — CLAUDE.md's testing checklist lists it as a separate item, and this repo has
already been bitten by exactly that (a deleted `case` body leaving two consecutive `break`s is
`[low:style] duplicateBreak`, which compiles and passes the host suite). Steps 5 and 8 do both of the
things cppcheck comments on here — remove enumerator handling from four `switch`es, and add a new
`switch` with a `default:` arm over a fully covered enum. **Fix:** add
`~/.platformio/penv/bin/pio check --fail-on-defect low --fail-on-defect medium --fail-on-defect high`
as step 12's item 2b, after `pio run`.

## MINOR 7 — one assertion is silently dropped from the "moved, unchanged" tests

plan:163 says the header is *"moved and renamed, body unchanged"*, and spec `:494` bills the six
donor tests as *"moved and renamed | unchanged behaviour after A-2's move"*. The donor
`PrimaryOkAlwaysUsesTheLoadedDoc` has four assertions
(`HighlightFileActionTest.cpp:19-23`); the plan's replacement (plan:109-113) has three — the
`(Ok, false, true)` case at `:21` has no counterpart. The input is still covered, indirectly, by step
3's exhaustive test (plan:286, `if (primary == DocReadStatus::Ok) EXPECT_EQ(reported, DocReadStatus::Ok)`
over all 4 × 2 × 2), so no coverage is actually lost — but the move is not the verbatim one both
documents claim. **Fix:** add
`EXPECT_EQ(tempAdoptionAction(DocReadStatus::Ok, false, true), TempAdoptionAction::UseLoaded);` back
at plan:110.

---

## Checked and sound — not findings

- **Every spec requirement maps to a step.** The spec's files-touched table (`:212-225`) is covered
  item for item: `TempAdoption.h` → steps 2 and 4; `HighlightFileAction.h` → step 5;
  `PersistableStore.{h,cpp}` → steps 8 and 9; the four rename-only adopters → step 5; the three
  direct callers → step 10; `test/temp_adoption/` → steps 1 and 3;
  `HighlightFileActionTest.cpp` and `test/highlight_file/CMakeLists.txt` → step 6; the signposts →
  step 7 (with MINORs 3-4); the A-11 shared-file report and the two follow-up issues → step 12.
- **Names and signatures are stable across steps.** `tempAdoptionAction`, `adoptedReadStatus`,
  `TempAdoptionAction`, `readDocFromFileAdopting(const char*, JsonDocument&) -> DocReadStatus`, the
  `TempAdoptionTest` target and the `build/test/temp_adoption/TempAdoptionTest` binary path all match
  between the step that introduces them and every later reference. The binary path was confirmed by
  building it.
- **Step 8's function is correct against the headers it calls.** `<string>` (`PersistableStore.cpp:9`),
  `<HalStorage.h>` (`:3`) and `<Logging.h>` (`:4`) are all already included; `Storage.exists`,
  `Storage.rename` take `const char*` (`HalStorage.h:35,37`); and the reuse of the caller's `doc` for
  the `.tmp` read is safe for the stated reason — `readDocFromFileChecked` returns at
  `PersistableStore.cpp:47-48` without touching `doc` when the file is missing.
- **Step 9 is behaviour-preserving at the boundary.** `readDocFromFile` is
  `readDocFromFileChecked(...) == Ok` (`PersistableStore.cpp:63-65`), so
  `readDocFromFileAdopting(...) != Ok` is the same predicate with the adoption branch added, and
  `loadFromFile` already holds `storeMutex` across a `storageMutex`-taking read
  (`PersistableStore.h:161,164`) — the rename introduces no new lock ordering.
- **Step 10's split is right.** `PassageFile.cpp` never calls `readDocFromFileChecked` at all (it
  goes through its own `readInto`, `PassageFile.cpp:62,69`), which is why it is absent from the
  `.tmp`-reader list at plan:653-655 while still appearing in step 5's rename table — that asymmetry
  is correct, not an omission.
- **Steps 5-10 having no failing test first is not a finding.** The spec states the reason
  (`:466-478`) and the plan restates it at plan:8-11 and plan:98-102 rather than pretending
  otherwise. The A-11 procedure — add `add_subdirectory(temp_adoption)` locally in step 1, revert it
  in step 12, report it in the PR body as blocking — is internally consistent and honest about what
  CI will and will not have run.
- **The hand-back is disciplined.** plan:786-794 refuses to claim the device behaviour, gives the
  human tester a reproducible procedure, and plan:799-800 asks before pushing, as CLAUDE.md requires.

---

## Verdict reasoning

Both MAJORs are inline-fixable and neither touches a decision. MAJOR 1 is a step reorder plus a
two-word change to two gate commands; MAJOR 2 is a five-line comment and one PR-body line carrying
across an invariant the spec already wrote. Neither reverses a spec decision, changes scope, nor
needs a judgment only the human can make. The plan's substance is sound: its code compiles and passes
as written, every anchor it quotes matches the tree byte for byte, and the one place it silently
departs from the spec (the shim) is the place where the spec contradicts itself and the plan takes
the correct branch.

Fix MAJORs 1-2 and MINORs 1-7 inline, then proceed.

VERDICT: CLEAR
