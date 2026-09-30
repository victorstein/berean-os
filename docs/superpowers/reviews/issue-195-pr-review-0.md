Tier: standard

# PR #207 review 0: retire Bible chapter-completion recording (#195)

Reviewed: `gh pr diff 207` at `f2409729` (merge base `3591e3fe`, which is also current `main`), issue #195, spec
`docs/superpowers/specs/2026-09-29-issue-195-design.md` (v3), and plan `docs/superpowers/plans/2026-09-29-issue-195-plan.md` (v2).
The orchestrator's decision t5-d1 covers the `test/CMakeLists.txt` line and the two `english.yaml` keys. I treated both as settled.

Checks I ran myself:

- `grep -rni completion src lib test` finds only unrelated hits ("build to completion", OTA, FilesPage), plus
  `COMPLETION_DIR` at `lib/Serialization/SdPaths.h:33,57` and `test/sd_paths/SdPathsTest.cpp:35`. This matches the PR's removal proof.
- No `STR_CHAPTERS_READ*` key is left under `lib/I18n/translations`.
- Outside historical specs, plans, research and reviews, `ChapterCompletion` and `chapters read` appear only in `CHANGELOG.md`
  (history) and in the retired section of `docs/file-formats.md`.
- A reconfigure of `build/test`, then a build of `AutoPageTurnTest` and `StorageIoTest`, passed: 49/49 in the affected suites.
  `ctest -N` gives `Total Tests: 1272`, which matches the spec's arithmetic (1302 − 32 + 2).
- I did not re-run `pio run`, `pio check` or `clang-format-fix`. The PR reports them clean.

## Intent

**Acceptance criteria.**

- **No SD write for completion on a chapter change.** Both `recordDocumentRead()` calls are gone from `pageTurn`
  (`src/activities/reader/EpubReaderActivity.cpp:905-917`). So are the method, `StudyStore::markDocumentRead` and the
  `ChapterCompletionFile` storage shell. No code builds the completion path any more. The issue asks for a log check. Spec A-14
  explains why that cannot show an absence: a successful atomic write logs nothing. It replaces the log check with the grep and
  device check 1. That is a reasoned substitute, not a silent reduction.
- **Build, host suites and format are clean.** The PR reports all three, and my partial re-run of the host tests agrees.
- **A card with an old file boots and is untouched.** The load in `openPublication` is gone (`src/study/StudyStore.cpp:59-72`), so
  nothing opens the file. Clear Cache walks only `/.crosspoint` (spec non-goal, `ClearCacheActivity.cpp:101,120-121`), and nothing
  deletes the file. Device check 1 covers the byte-identity part on hardware.

**Issue scope, item by item.**

- **Write path.** `EpubReaderActivity::recordDocumentRead` (`.h` and `.cpp`) and `StudyStore`'s completion members, include, load,
  reset, save helper and methods are all removed.
- **Files and suite removed once unreferenced.** `ChapterCompletionFile.{h,cpp}`, `ChapterCompletion.{h,cpp}` and
  `test/chapter_completion/` are deleted, with the `test/CMakeLists.txt` line (t5-d1). The grep proof is in the PR body.
  `test/storage_io/ChapterCompletionFileTest.cpp` goes too. The issue does not name it, but it compiled the deleted sources
  (spec A-7), so this is a forced consequence, not scope expansion.
- **Existing files stay, and are documented as unused.** `docs/file-formats.md` marks the section retired, says the file is no
  longer written or read, and moves the behaviour claims into the past tense. `USER_GUIDE.md` updates the directory row.
- **Find any other reader first.** Spec A-1 and research R §1 establish that no live reader remained after #197. My grep agrees.
- **Beyond the issue's list.** The load-failure notice in `loadBook` and its string are removed. They fall with
  `takeCompletionLoadFailureNotice` (A-5), so this is the necessary other half of the removal, not new scope.

**The one relocation.** `forwardTurnLeavesDocument` moves to `src/activities/reader/PageTurn.h`. Its body is unchanged
(`return !(currentPage < pageCount - 1 || stillBuilding);`), and `pageTurn` calls it with the same arguments. Its two tests moved
with their bodies intact to `test/auto_page_turn/ForwardTurnTest.cpp`. They exercise the behaviour: the boundary pages, a
one-page section and the still-building case. They do not restate the implementation. Both `RenderLock`s are kept, as A-4 requires.

**Divergence from the plan.** None of substance. The commit sequence matches plan Steps 2–10 one for one. `ForwardTurnTest.cpp:14`
has the second test collapsed onto one line, which is clang-format's output rather than a deviation. `SdPaths.h` line numbers moved
by one (32→33, 56→57) because of the new comment, and the PR quotes the post-change numbers. No missing-include fallback was needed.

## Quality

- **Pattern mirroring.** `PageTurn.h` follows `BibleReference.h`: a pure header, a global free function, and a one-line reason why
  it is free of Arduino and Epub. It is tested from an existing suite that already puts `${REPO_ROOT}/src` on the include path, so
  it adds no second way of doing this. The deletion-plus-grep-proof shape mirrors #197.
- **Naming.** The suite is renamed to `ForwardTurn`, so no `ChapterCompletion` name survives in `test/`. The function name is
  unchanged, and it has left `study::` because it is not study-data code (A-2).
- **Dead code.** There is no dead or commented-out code.
  - `highlightsLoaded` still has live readers (`EpubReaderActivity.cpp:1382`).
  - `UnitAnchors.h` is still included directly (`StudyStore.cpp:11`).
  - `COMPLETION_DIR` is kept on purpose, with a one-line reason: it reserves the path.
- **Comments.**
  - The new comments state a reason, not the next line's action: `SdPaths.h:32` says the path is reserved, and `PageTurn.h:3-5`
    says why the header is free of Arduino and Epub.
  - Two edited comments were left with ragged short lines; see MINOR 1.
- **Error handling.** Two failure modes were removed along with their popups. The passage store's `saveDisabled_` latch and
  `STR_HIGHLIGHTS_LOAD_FAILED` are untouched and independent.
- **Duplication.** No duplication was introduced.

## Findings

1. **MINOR.** Two comments edited in this PR were left with ragged wrapping. Nothing is wrong in them, but they now read oddly.
   - `lib/Serialization/PersistableStore.h:112` ends at `// single-task property alone. If a background` (about 45 columns), and
     the sentence continues on `:113`.
   - `test/storage_io/CMakeLists.txt:3` ends at `# it -- against the in-memory Storage fake in`, and the sentence continues on
     `:4`.

   clang-format does not reflow comments, so the plan's "accept whatever clang-format produces" never tidied them. The fix is to
   rewrap each paragraph to fill its lines. This is cosmetic only.

VERDICT: CLEAR
