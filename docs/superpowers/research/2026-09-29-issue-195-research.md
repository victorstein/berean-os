# Issue #195 research — retire Bible chapter-completion recording

**Date:** 2026-09-29
**Branch:** `refactor/195-retire-chapter-completion`, based on `3591e3fe` (#197, which closed #194)
**Surface:** data (`src/study`, `lib/StudyStore`), and it also reaches the reader activity, i18n and
the host-test tree.

Every claim below comes from a command run in this worktree, or from a line read at the cited
`file:line`.

## 1. Who reads the record today: nobody

#194/#197 removed the only two readers. Its commit body (`git show 3591e3fe`) says "The
chapter-completion store and its writers are untouched; retiring them is the follow-up issue". It
also reports that `grep -rnw -e loadCompletion … src lib` returns nothing.

I re-checked on this tree with
`grep -rn -i -E "completion|recordDocumentRead" src lib test docs/file-formats.md test/CMakeLists.txt`.
Excluding unrelated uses of the word (`FilesPage.html:1489`, `BibleSearchActivity.cpp:979` and
`OtaUpdateActivity.cpp:195`, all about "download/OTA completion"), every hit is in the write path
or its tests, listed in §2.

Nothing outside the record's own code calls `isRead`, `readCount` or `readCountInBook`
(`grep -rln "readCount\|isRead(" src lib | grep -v ChapterCompletion` → empty).

**The issue's "stop if a live feature still reads it" check:**

- `docs/superpowers/specs/2026-09-13-bible-chapter-status-and-hint-design.md` covers the chapter
  number in the status bar and the verse-list hint. It does not read the completion record: it
  predates the record, which #78 added on 2026-09-23 (`git log --diff-filter=A` →
  `1269fced feat: record Bible chapters read … (#78)`).
- `2026-09-23-bible-chapter-completion-design.md` is the design of the record itself.
- The Meetings "workbook progress" that #197 kept does not come from this record. No Meetings or
  launcher code references `ChapterCompletion` (grep above).

**No live reader exists, so no decision is needed on that point.**

## 2. What owns the behaviour: the full write path

The writer is `EpubReaderActivity::pageTurn`:

| Where | What |
|---|---|
| `src/activities/reader/EpubReaderActivity.cpp:903` | `if (!study::forwardTurnLeavesDocument(...))` is the forward-turn branch condition |
| `EpubReaderActivity.cpp:907-909` | next-spine branch: `RenderLock lock; recordDocumentRead();` |
| `EpubReaderActivity.cpp:915-917` | end-of-book branch: `RenderLock lock; recordDocumentRead();`. #78 added this lock *for* completion (its commit body: "the end-of-book branch now takes it too") |
| `EpubReaderActivity.cpp:940-945` | `recordDocumentRead()` calls `STUDY.markDocumentRead` and shows `STR_CHAPTERS_READ_SAVE_FAILED` on `SaveFailed` |
| `EpubReaderActivity.h:83-86` | declaration and its comment |
| `EpubReaderActivity.cpp:219-221` | `takeCompletionLoadFailureNotice()` → `STR_CHAPTERS_READ_LOAD_FAILED`, a load-time popup the issue does not list |

The store is `StudyStore`:

| Where | What |
|---|---|
| `src/study/StudyStore.cpp:8` | `#include "ChapterCompletionFile.h"` |
| `StudyStore.cpp:19-21` | `saveBibleCompletion()` in the anonymous namespace |
| `StudyStore.cpp:71-76` | `openPublication` loads `/.berean/completion/bible.json`. This is an SD **read** on every Bible open, and it sets `completionSaveDisabled_` on failure |
| `StudyStore.cpp:89` | `closePublication` resets `completion_` |
| `StudyStore.cpp:110-121` | `markDocumentRead` |
| `StudyStore.cpp:123-127` | `takeCompletionLoadFailureNotice` |
| `src/study/StudyStore.h:10` | `#include "StudyStore/ChapterCompletion.h"` |
| `StudyStore.h:130-144` | the `markDocumentRead` and `takeCompletionLoadFailureNotice` declarations and their comments |
| `StudyStore.h:193-200` | `completion_` (149 bytes, static storage in the singleton), `completionSaveDisabled_`, `completionLoadFailureAnnounced_` |

The format and the storage shell:

- `lib/StudyStore/StudyStore/ChapterCompletion.{h,cpp}`: the bitmap, the JSON format, and
  `markDocumentChapters` / `recordDocumentRead` / `CompletionMarkResult`.
- `src/study/ChapterCompletionFile.{h,cpp}`: `load`, `save` and `path`, over
  `sdpaths::COMPLETION_DIR`.

The issue's line numbers are stale by a few lines (it cites `EpubReaderActivity.cpp:914, 922, 945`
and `StudyStore.cpp:18-19, 57, 72, 98`). #197 edited both files after the issue was written. The
numbers above are from this tree.

### Current control flow

A forward turn whose `forwardTurnLeavesDocument` is true takes `RenderLock`, then calls
`recordDocumentRead()`, then `STUDY.markDocumentRead(spine)`. That call does work only for the
`bible` pubkey with a ready unit index (`StudyStore.cpp:111`). It then calls
`study::recordDocumentRead(completion_, units_->unitsFor(spine), completionSaveDisabled_, saveBibleCompletion)`,
which marks bits. **Only if a bit flipped** does it call `ChapterCompletionFile::save`, which does
`writeDocToFileAtomic` to `/.berean/completion/bible.json` (the `.tmp` write and rename, under
`storageMutex` via HalStorage).

So the SD write the issue wants gone happens at most once per newly read chapter, not on every
page turn. The load happens once per Bible open.

## 3. Dependencies the issue does not list, which the spec must handle

1. **`study::forwardTurnLeavesDocument` is live, non-completion logic.** It is declared in
   `ChapterCompletion.h:63-68` (constexpr, header-only), and `pageTurn` uses it as its own branch
   condition (`EpubReaderActivity.cpp:903`). Deleting the header deletes the page-turn predicate.
   Its tests are `ChapterCompletionTrigger.OnlyAForwardTurn…` and `.DoesNotFireWhileTheSectionIsStillBuilding`
   (`test/chapter_completion/ChapterCompletionTest.cpp:102-113`), so the suite the issue deletes
   holds its only coverage. The spec must choose between moving the predicate (and its two tests)
   and inlining the original condition. `git show 1269fced -- src/activities/reader/EpubReaderActivity.cpp`
   shows the condition as it was before #78.
2. **`test/storage_io` also compiles the completion code.** `test/storage_io/CMakeLists.txt:15`
   (`ChapterCompletionFileTest.cpp`), `:21` (`src/study/ChapterCompletionFile.cpp`) and `:24`
   (`lib/StudyStore/StudyStore/ChapterCompletion.cpp`), plus the header comment at `:2`. Deleting
   the sources without editing this file breaks the host build. The issue names only
   `test/chapter_completion/` and its `test/CMakeLists.txt:117` line.
3. **Two i18n keys become dead:** `STR_CHAPTERS_READ_LOAD_FAILED` and `STR_CHAPTERS_READ_SAVE_FAILED`
   (`lib/I18n/translations/english.yaml:373-374`, and in no other YAML:
   `grep -rln STR_CHAPTERS_READ lib/I18n/translations` → 1 file). `gen_i18n.py` scans comments
   for `STR_` names (project memory), so no comment may keep naming them once they are deleted.
4. **`sdpaths::COMPLETION_DIR`** (`lib/Serialization/SdPaths.h:32`, `static_assert` at `:56`) is
   asserted by `test/sd_paths/SdPathsTest.cpp:35`. The file stays on the card and is documented, so
   keeping the constant as a reserved-path record is defensible. Whether to remove it is a spec
   choice either way.
5. **User-facing docs:** `USER_GUIDE.md:431` lists `completion/` as "which Bible chapters you have
   read". `docs/file-formats.md:350-376` documents version 1 as live. The issue asks for the latter
   to say "no longer written or read".
6. **`PostedMessage`** (`src/activities/PostedMessage.{h,cpp}`) came in with #78, but it is general.
   `ReaderUtils::showMessage` routes every reader refusal through it. It stays. Its two-slot queue
   was sized partly for "highlights load notice plus chapter load notice" (#78 body). Shrinking it
   is out of scope.
7. `canonicalChapterCount`, `CANONICAL_CHAPTER_TOTAL` and `BIBLE_BOOK_COUNT` are defined only in
   `ChapterCompletion.{h,cpp}`, and nothing outside it uses them (grep in §1). They go with the file.

## 4. Shared files: report, do not edit

`.claude/agents/data-dev.md` lists `test/CMakeLists.txt` and `lib/I18n/translations/*.yaml` as
shared append points, whose lines go in the PR description for the orchestrator. That applies to
`test/CMakeLists.txt:117` and `english.yaml:373-374`. `test/storage_io/CMakeLists.txt` is
suite-local and is not on the shared list. #197 set the precedent: it deleted YAML keys in its own
diff (under decision d1) and handed off only its `test/CMakeLists.txt` line.

## 5. Installed tool versions

| Tool | Version | Evidence |
|---|---|---|
| PlatformIO Core | 6.1.19 | `~/.platformio/penv/bin/pio --version` (not on `PATH`) |
| Platform | pioarduino `platform-espressif32` 55.03.37 | `platformio.ini:15` |
| ArduinoJson | 7.4.2 | `platformio.ini:155` |
| clang-format | 21.1.8 | `.venv/bin/clang-format --version`, after `./bin/bootstrap` |
| CMake | 4.4.2 | `cmake --version` |

CI configures host tests with `-G Ninja` (`.github/workflows/ci.yml:145`). Ninja is not installed
here, so the local baseline used the default generator:

    cmake -S test -B build/test -DCMAKE_BUILD_TYPE=Release && cmake --build build/test -j8
    ctest --test-dir build/test -j8   →   100% tests passed out of 1302

**30** of those tests are completion tests (`ctest -N | grep ChapterCompletion`): Bitmap 5, Trigger 5,
Save 4, Json 11, FileIo 5. The expected count after the change is 1302 − 30, plus whatever
forwardTurn tests are moved (see §3.1).

## 6. The nearest existing example

- **The inverse change, #78 (`1269fced`),** is the checklist. Its `--stat` touches exactly: the two
  `ChapterCompletion` files, the two `ChapterCompletionFile` files, `StudyStore.{h,cpp}`,
  `EpubReaderActivity.{h,cpp}`, `english.yaml` (+3), `docs/file-formats.md` (+27),
  `test/chapter_completion/*` and `test/CMakeLists.txt` (+1). Its `LauncherActivity`, `ReaderUtils`,
  `PostedMessage` and `UiListActivity` hunks are either already reverted by #197 or general-purpose
  (§3.6). `test/storage_io/ChapterCompletionFileTest.cpp` came later, in `c46f87a2` (#127).
- **The retirement pattern, #197 (`3591e3fe`),** removed the reader side. It deleted the code and
  the i18n keys outright, proved "no references left" with a `grep -rnw` over `src lib` quoted in
  the PR, and handed off its `test/CMakeLists.txt` line. That is the same proof the issue's
  acceptance criteria ask for.

## 7. Tier

It stays at **standard**. There is no on-disk contract change: the file is left in place, never
read and never written, so no format version moves and no migration is needed. The edits outside
`data` (`EpubReaderActivity`, one YAML, the test trees) are ones the issue itself scopes, and they
are pure deletions apart from the `forwardTurnLeavesDocument` relocation.
