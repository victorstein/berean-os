# Issue #111 plan review, pass 0

Plan: `docs/superpowers/plans/2026-09-26-issue-111-plan.md`
Spec: `docs/superpowers/specs/2026-09-26-issue-111-design.md`
Tree: `refactor/111-sd-path-constants` at `7c40cced` (source identical to baseline `2f303f6f`).

## What was checked, and held

- **Every site is covered.** `grep -rn '"/\.crosspoint\|"/\.berean' src lib --include='*.cpp' --include='*.h'`
  prints 38 lines today. Each maps to a task: PersistableStore ×2 and the four `getFilePath()`
  headers (Task 3), nine `Epub(…, "/.crosspoint")` (Task 4), ClearCache `:100,:120` (Task 5),
  HighlightFile/BookmarkUtil/MigrationRunner `:30,31` (Task 6), four root duplicates (Task 7),
  three subdirectories (Task 8), eight owner aliases in five headers (Task 9). The two left over are
  `src/main.cpp:227` (A8) and the comment at `src/util/RecentBooksDoc.h:41`. That matches Task 10's
  expected output (plan `:617-618`) byte for byte. A second grep for `.crosspoint`/`.berean` in any
  other form (no leading `"/`, `.hpp`, `.c`, `.ino`), excluding comments, prints nothing.
- **Every use of a deleted constant is rewritten.** A grep for `BEREAN_DIR|STUDY_DIR|LEGACY_DIR|PASSAGES_DIR|UNITS_DIR|COMPLETION_DIR`
  finds uses at `MigrationRunner.cpp:48,116,171`, `PubKeyRegistry.cpp:45`, `TagPaletteFile.cpp:74`,
  `MeetingWeekCache.cpp:55`, `CatalogIndexStore.cpp:51,204,205`, `PassageFile.cpp:54,110`,
  `UnitIndexCache.cpp:48,92` and `ChapterCompletionFile.cpp:18,72`. Every one of them is in Tasks 6–8, and
  the quoted "today" text matches the file, indentation included. `BibleSearchIndexer.cpp:278-279`
  reads `BibleSearchStore::SEARCH_DIR`, which A5 keeps as an alias, so it rightly does not change.
- **Include anchors exist.** Every "add after `#include <X>`" line in Tasks 3–9 names a line that is
  present in its file. The CatalogIndexStore instruction keeps the `// clang-format off` block
  (`CatalogIndexStore.cpp:3-14`) intact.
- **No host test breaks on the new include.** No host test compiles or includes any of the 28 source
  files this change touches. The only test references are to the pure `*Action.h` and
  `WifiCredentialEdit.h` helpers (`test/highlight_file/CMakeLists.txt`,
  `test/bookmark_save_action/CMakeLists.txt`, `test/credential_integrity/CMakeLists.txt`), so a
  test target cannot fail for lack of `lib/Serialization` on its include path.
- **Types and names are consistent.** The header in Task 2 (plan `:152-210`) is identical to the
  spec header (spec `:57-99`), with the elided `static_assert`s written out: one per constant, and
  the two search files checked against `SEARCH_DIR`. Every `sdpaths::` name used in Tasks 3–9 is
  declared there. The aliases `inline constexpr const char* X = sdpaths::Y;` are constant
  expressions (the address of an inline array with static storage). `sdpaths::SEARCH_DIR` is
  qualified inside `BibleSearchStore`, so it cannot resolve to the member it initialises. The test
  has 8 cases and Task 2 expects 8.
- **Spec requirements map to steps.** Testing step 0 is Task 1c and Task 11's revert. Red/green is
  Tasks 1–2. The sweep is Task 10. The build, the existing suites and the formatting are Task 10.
  The A8 shared-file lines, the CI gap and the human-tester check are in the Task 11 PR body. The
  "13 bare uses, one more than the issue" count agrees with research `:28,169`.
- **FILES lines.** They are at column 0 and outside fences (plan `:15-25`), with 32 repo-relative
  paths. Every file any task edits or `git add`s appears on them. `test/CMakeLists.txt` is
  deliberately absent: the plan states that at `:27-29`, it is never staged (the guard at
  `:40-41,224,655`) and Task 11 reverts it. This carries out the cleared spec's step 0 and does not
  leave a file off the lock by mistake.
- **Test-first.** Only Tasks 1–2 add behaviour, and they are red then green in one commit. Tasks 3–9
  are spelling swaps whose correctness is the pinned literals plus the Task 10 sweep and build, as
  spec step 3 prescribes. There is nothing new for a per-task failing test to catch.

## MINOR 1 — Task 11 pushes and opens a PR with no approval gate

**Claim.** Task 11 runs `git push` (plan `:666`) and `gh pr create` (plan `:673-675`) as ordinary steps.

**Problem.** `CLAUDE.md`, Git workflow → Rules 2: "Never push to any remote, or open or close a PR,
without explicit user approval." The same finding was raised in
`docs/superpowers/reviews/issue-34-plan-review-0.md:143-148` and `issue-40-plan-review-0.md:121-138`.
Plans in this pipeline routinely include the step, so this is convention rather than an oversight,
but the plan should not present the step as unconditional.

**Fix.** Preface Task 11 with "Only on explicit approval from the user or orchestrator. Otherwise
stop after Task 10 with the branch committed locally."

## MINOR 2 — a failed Task 10 sweep or build has nowhere labelled to land

**Claim.** Tasks 3–9 each commit without compiling. The single `pio run` comes in Task 10 (plan
`:633`), which follows `CLAUDE.md`'s "build once after the last code edit". Task 10 says to fix a
missed site "with the same pattern as its task" (plan `:621`). Its only commit is
`style: format SdPaths.h call sites` via `git add -u -- lib src test/sd_paths` (plan `:654-656`).

**Problem.** A missed site from the sweep, or a compile fix from the build, would be swept into a
commit labelled `style:`. PRs are squash-merged, so this does not affect the release. It does make
the branch history misleading, and the plan gives no instruction for a failed build.

**Fix.** Add to Task 10: "If the sweep or `pio run` needs a code fix, commit it separately as
`refactor: <what>` before the format commit, then re-run `pio run` once."

## Summary

This is a mechanical refactor, and the plan is precise: every edit is shown as exact before/after
text that matches the tree, and every site the spec lists is accounted for, along with every use of
each deleted constant. There are 0 BLOCKERs and 0 MAJORs. The two MINORs are one-line wording fixes
to Tasks 10 and 11. An implementer with no other context can execute the plan literally and arrive
at the spec.

VERDICT: CLEAR
