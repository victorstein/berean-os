# Issue #132 — plan review 0

Plan: `docs/superpowers/plans/2026-09-26-issue-132-plan.md`
Spec: `docs/superpowers/specs/2026-09-26-issue-132-design.md`

## What was checked

- **Every anchor the plan quotes** was compared with the current tree and matches:
  - `HighlightDoc.cpp:1-3, 121-122`
  - `TagPalette.cpp:1-3, 78-79`
  - `ChapterCompletion.cpp:1-3, 111-112`
  - `PassageDoc.cpp:1-3, 221-222`
  - `BookmarkDoc.cpp:1-3, 28-32`
  - `PubKeyRegistry.cpp:3-4, 28-30, 57, 83`
  - `MigrationRunner.cpp:3-5, 68-69, 95-96`
  - `MeetingWeekCache.cpp:3-4, 24-25`
  - `FormatVersion.h:10-12`
  - `FormatVersionTest.cpp:16`
  - the test anchors in `HighlightDocTest.cpp:59-71`, `TagPaletteTest.cpp:88-94`, `ChapterCompletionTest.cpp:271-274`, `PassageDocTest.cpp:46-51` and `BookmarkDocTest.cpp:96-102`
  - the four per-suite `target_include_directories` blocks the plan replaces
- **Spec coverage.** All 11 sites in the spec's Architecture table (`spec:209-219`) map to Tasks 1–8. The helper comment and the test message (A8) map to Task 9. The PR-body obligations map to Task 10.5:
  - the load-bearing write-path defaults (A2)
  - the untested `src/` stores
  - the fact that only HighlightDoc's cases fail first
  - "none" under the shared-file heading (A7)
- **Absent-`"v"` defaults per site.** Each site's default matches spec A1/A2:
  - `| 0` is kept at TagPalette, ChapterCompletion and PassageDoc.
  - `| FORMAT_VERSION` / `| LEDGER_FORMAT_VERSION` is used everywhere else.

  No `LOG_ERR` text, return value or constant changes.
- **Local names.** The new `const int version` locals do not collide with any name in `findBySymbol`, `lookup`, `readLedger`, `appendLedger` or `load`.
- **The grep in Task 10.1**, run today, matches exactly the 11 hand-rolled sites and nothing else. After the change it will return nothing.
- **The count in Task 10.2**, 13 new tests (3+3+2+4+1), matches the test blocks the plan writes.
- **`FILES:` lines** (`plan:10-16`) are at column 0, outside any fence, and repo-relative. Every file any task edits appears on one of them, including `test/passage_doc/CMakeLists.txt`.
- **TDD shape.** Task 1 is the only red-first task, which is correct: only HighlightDoc accepts `0`/`-1` today (`HighlightDoc.cpp:121-122`). The other tasks write their tests first and pin existing behaviour, as spec `:291-295` requires.

## Findings

### MAJOR 1 — Task 2 breaks `PassageDocTest`, and Task 4b's "expect green" then fails to compile

- **Claim.** Task 2c says "two suites compile `TagPalette.cpp`" (`plan:204`) and adds `lib/Serialization` to `tag_palette` and `migration_planner` only. The plan's rules say "Every commit leaves the host suite green" (`plan:35-36`).
- **Problem.** A third suite compiles `TagPalette.cpp`:
  - `test/passage_doc/CMakeLists.txt:6` lists `${REPO_ROOT}/lib/StudyStore/StudyStore/TagPalette.cpp`.
  - Its include directories (`:10-13`) are only `lib/StudyStore` and `lib/Utf8`.
  - The shared `crosspoint_test_common` adds only `${REPO_ROOT}` and `${REPO_ROOT}/lib` (`test/CMakeLists.txt:37-40`), so `<FormatVersion.h>` does not resolve.

  This has three effects:
  - After Task 2d, `PassageDocTest` no longer compiles.
  - The Task 2 commit leaves the host suite red. Task 2e builds only `TagPaletteTest MigrationPlannerTest StorageIoTest` (`plan:285`), so nothing in Task 2 notices.
  - A literal executor first sees the failure at Task 4b (`plan:451-455`), which says "expect green" but gets a compile error. The include-path fix only arrives one sub-step later, in 4c.

  The end state is correct, because 4c does add the path. The ordering is what is wrong. The spec has the same blind spot: A4 (`spec:159-162`) names `migration_planner` as the extra `TagPalette.cpp` consumer and does not mention `passage_doc`.
- **Evidence.** `grep -rn TagPalette.cpp test --include=CMakeLists.txt` returns three suites, not two:
  - `tag_palette/CMakeLists.txt:3`
  - `migration_planner/CMakeLists.txt:7`
  - `passage_doc/CMakeLists.txt:6`

  (`storage_io/CMakeLists.txt:23` also compiles it but already has the path at `:33`.)
- **Fix.**
  - Move the `test/passage_doc/CMakeLists.txt` edit (the current 4c block) into Task 2c.
  - Reword `plan:204` to "three suites compile `TagPalette.cpp`".
  - Add `PassageDocTest` to the Task 2e build and run lines.
  - Add `test/passage_doc/CMakeLists.txt` to the Task 2f `git add`.
  - Replace Task 4c with "no CMake change: Task 2 already added `lib/Serialization`".
  - Keep the `FILES:` lines as they are; the file is already listed.

## Verdict

One MAJOR. It is a mechanical re-ordering within files already on the lock, reverses no decision and changes no scope, so it can be fixed inline. Everything else is sound: the anchors are exact, the per-site defaults match the spec, the refusal paths are preserved, and the file lock is complete.

VERDICT: CLEAR
