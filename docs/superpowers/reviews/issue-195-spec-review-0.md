Tier: standard

# Review of `docs/superpowers/specs/2026-09-29-issue-195-design.md` (v1)

Reviewed against issue #195 (`gh issue view 195 --repo victorstein/berean-os`), the research note
`docs/superpowers/research/2026-09-29-issue-195-research.md` (R), and the tree at `d4488da6`.

## What checks out

These were verified line by line, and they hold:

- **Every line reference in A-5 and A-6 is current.** `EpubReaderActivity.h:83-86`, `.cpp:219-221`,
  `:903`, `:909`, `:917` and `:940-945`. `StudyStore.cpp:8`, `:19-21`, `:71-76`, `:89`, `:110-121`
  and `:123-127`. `StudyStore.h:10`, `:130-144` and `:193-200`.
- **A-1 holds: nothing reads the record.** `git grep -nw -e isRead -e readCount -e readCountInBook`
  finds uses only inside `ChapterCompletion.{h,cpp}` and `test/chapter_completion/`.
- **A-3 holds.** `forwardTurnLeavesDocument` (`ChapterCompletion.h:66-68`) is
  `!(currentPage < pageCount - 1 || stillBuilding)`. That is exactly the pre-#78 condition that
  `git show 1269fced` replaced (`-    if (section->currentPage < section->pageCount - 1 || section->isBuilding())`).
- **A-4 holds.** `renderBook` reads `currentSpineIndex` (`EpubReaderActivity.cpp:995`), and so
  does `isAtEndOfBook` (`:970`), so the lock at `:916` still guards a real mutation (`:918`).
- **The include analysis holds.** Dropping `StudyStore.h:10` does not strand `UnitAnchors.h`,
  because `UnitIndexCache.h:10` pulls it in through `UnitIndexFormat.h:8`. Nothing outside the
  deleted files uses `BIBLE_BOOK_COUNT`, `markDocumentChapters` or `CompletionSaveFn` (git grep).
- **D1's test home works.** `test/auto_page_turn/CMakeLists.txt` already puts `${REPO_ROOT}/src`
  on the include path and uses `gtest_discover_tests`, so adding a source file needs no
  `test/CMakeLists.txt` line.
- **A-14's premise is true.** `PersistableStore.cpp` logs only failures (`:77, 85, 98, 118`) and
  temp recovery (`:188`). `grep -c LOG_ lib/hal/HalStorage.cpp` returns 0.
- **The documentation targets are complete.** `git grep -i` for "chapters read", "completion/" and
  "chapters you have read" outside `docs/superpowers` finds only `USER_GUIDE.md:431`,
  `docs/file-formats.md:350-376`, the two YAML keys, the code being deleted, and `CHANGELOG.md:262`.
  The changelog is history and correctly left alone.

## Findings

### MAJOR 1: the expected test count is wrong. It is 1272, not 1274.

**Claim.** Testing §3 says the suite "passes **1274** tests: the 1302 baseline (R §5), minus the 30
`ChapterCompletion*` tests, plus the 2 moved tests". The Problem section says "about 700 lines of
code and 30 host tests".

**Problem.** The deleted files hold 32 tests, not 30. R §5 counted them with
`ctest -N | grep ChapterCompletion`, which misses the `CanonicalChapters` suite. That suite lives
in `test/chapter_completion/ChapterCompletionTest.cpp:30` and `:37` and is deleted along with the
file. After the change the run reports 1272. An implementer then either goes looking for two
phantom tests or quotes a false count in the PR.

**Evidence.**

    $ grep -n "^TEST" test/chapter_completion/ChapterCompletionTest.cpp test/storage_io/ChapterCompletionFileTest.cpp \
        | sed 's/,.*//' | awk -F'(' '{print $2}' | sort | uniq -c
       2 CanonicalChapters
       5 ChapterCompletionBitmap
       5 ChapterCompletionFileIo
      11 ChapterCompletionJson
       4 ChapterCompletionSave
       5 ChapterCompletionTrigger
    $ ctest --test-dir build/test -N | tail -1
    Total Tests: 1302
    $ ctest --test-dir build/test -N | grep -c -E "ChapterCompletion|CanonicalChapters"
    32

1302 − 32 + 2 = 1272.

**Fix.** Change Testing §3 to **1272**, as "1302 − 32 (the 30 `ChapterCompletion*` tests plus the
2 `CanonicalChapters` tests) + 2 moved". Change "30 host tests" in the Problem section to 32. Note
in the revision log that R §5's count was short by the `CanonicalChapters` suite.

### MAJOR 2: A-9 and A-10 stretch #194's decision d1 beyond what it covered. This needs an orchestrator decision.

**Claim.** A-9 says: "Decision d1 on #194 accepted in-branch deletions to shared files on the
grounds that deletions do not collide the way appends do (`…-issue-194-design.md`, A-17)." On that
basis it deletes `test/CMakeLists.txt:117` in-branch. A-10 relies on the same d1 for
`english.yaml:373-374`.

**Problem.**

- **d1 was a YAML-only exception, granted for one task under one condition.** The 194 spec
  records d1 as "The YAML shared-file exception went to the orchestrator as decision d1"
  (`2026-09-29-issue-194-design.md:20-22`). A-17 (`:150`) scopes it to named keys in
  `lib/I18n/translations/*.yaml`, on the stated condition that "t4 holds
  `lib/I18n/translations/` in its file set, so no sibling can collide".
- **#194 itself handed off its `test/CMakeLists.txt` line.** A-19 (`:152`) says: "handed to the
  orchestrator in the PR body rather than edited here", citing "Shared files — report, do not
  edit". The #197 commit body repeats it: "`test/CMakeLists.txt` is a shared file, so it is not
  edited here" (`git show 3591e3fe`).
- **The agent rule is still in force.** `.claude/agents/data-dev.md:22-27` names both
  `test/CMakeLists.txt` and `lib/I18n/translations/*.yaml` as "report, do not edit … let the
  orchestrator apply it".
- **So the spec carries an orchestrator decision over to a different file and a different issue.**
  Nothing establishes that no sibling task in this batch holds `test/CMakeLists.txt` or
  `english.yaml`.

A-9's practical argument is sound, and it is the right thing to put in front of the orchestrator:
deleting `test/chapter_completion/` without removing `add_subdirectory(chapter_completion)` breaks
`cmake -S test`, so a hand-off leaves the PR's own CI red. But choosing to override the
shared-file rule is the orchestrator's call, exactly as it was for d1. The spec must not record it
as already decided.

**Evidence.** `docs/superpowers/specs/2026-09-29-issue-194-design.md:20-22`, `:150` (A-17) and
`:152` (A-19). `.claude/agents/data-dev.md:22-27`. `git show 3591e3fe` ("`test/CMakeLists.txt` is a
shared file, so it is not edited here").

**Fix.** Raise one decision to the orchestrator covering both edits. The question: "Delete
`test/CMakeLists.txt:117` and `english.yaml:373-374` in-branch, as deletions only, because a
hand-off leaves `cmake -S test` broken on the PR?" Record the answer in A-9 and A-10 as a new
decision for #195, not as d1. If the answer is hand-off, A-9 must say how the PR's host-test CI
stays green. The #194 pattern is to edit locally and leave the file unstaged, which does not help
CI, so the PR would need to state that CI is expected red until the line is applied.

### MINOR 1: the removal-proof grep will hit a file the spec never edits

**Claim.** Testing §2's `grep -rnw … -e ChapterCompletionFile … src lib test` "must return
nothing", and the Architecture list names every file to edit.

**Problem.** `lib/Serialization/PersistableStore.h:111` has a comment that names
`ChapterCompletionFile` among the stores that rely on the single-task property. That file is not
in the Architecture list, so the gate fails on a file the spec never mentions. The comment also
becomes false once the store is gone.

**Evidence.** `git grep -nw ChapterCompletionFile -- lib` →
`lib/Serialization/PersistableStore.h:111:  // ChapterCompletionFile, PassageFile, TagPaletteFile, BookmarkFile and`.

**Fix.** Add `lib/Serialization/PersistableStore.h` to the Architecture list as "EDIT: drop
`ChapterCompletionFile` from the comment at `:108-113`". Rewrap the comment so it still reads
"PassageFile, TagPaletteFile, BookmarkFile and HighlightFile".

### MINOR 2: the gate's symbol list leaves out some of the members A-6 deletes

**Claim.** Testing §2 lists the symbols whose absence proves the removal.

**Problem.** The list includes `completionSaveDisabled_` but leaves out its sibling latch
`completionLoadFailureAnnounced_`. It also leaves out `saveBibleCompletion`, `CompletionSaveFn`,
`markDocumentChapters`, `BIBLE_BOOK_COUNT` and the `completion_` member. A half-applied A-6 that
leaves the member at `StudyStore.h:200` behind would compile, pass, and pass the gate.

**Evidence.** `git grep -nw -e completionLoadFailureAnnounced_ -e saveBibleCompletion -- src` →
`StudyStore.cpp:19, 116, 124, 125` and `StudyStore.h:200`.

**Fix.** Add these to the `grep -rnw` list: `-e completionLoadFailureAnnounced_`,
`-e saveBibleCompletion`, `-e CompletionSaveFn`, `-e markDocumentChapters`,
`-e BIBLE_BOOK_COUNT` and `-e completion_`. All of them are word-anchored and have no other
occurrences today.

VERDICT: BLOCKER
BLOCKERS: 0
MAJORS: 2
