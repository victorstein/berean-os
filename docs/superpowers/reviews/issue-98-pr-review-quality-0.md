# PR #127 review: code quality (issue #98)

Branch `fix/98-single-tmp-adoption-policy`, diffed against `0e0e7641` (release 1.16.4, the merge base).
Scope reviewed: `lib/Serialization/{PersistableStore.cpp,PersistableStore.h,TempAdoption.h}`, the five
store loaders under `src/study/` and `src/util/`, and the new and changed host tests.

Host build: `cmake --build build/test` produced no warnings or errors. The 50 related tests passed
(`LoadAdopting`, `TempAdoption*`, `AdoptedLoad`, `AdoptingRead`, `TagPaletteFileIo`,
`ChapterCompletionFileIo`, `PassageFileIo`).

## Summary

The change removes duplication. There were five nearly identical copies of the read-primary /
read-`.tmp` / switch-on-`tempAdoptionAction` block, and one of them
(`TagPaletteFile.cpp`, old lines 47-52) had already drifted by dropping its rejected-after-recovery
log. All five are replaced by one `PersistableStoreBase::loadAdopting`, and `readDocFromFileAdopting`
is rebuilt on the same private `readAdopting` core (`PersistableStore.cpp:64-106`). Neither path
adds a second way to do the job.

- **Pattern fidelity.** The callback shape `DocAcceptor = bool (*)(void*, JsonVariantConst)`
  (`PersistableStore.h:151-155`) is a function pointer plus context, which is the pattern CLAUDE.md
  asks for (no `std::function`, no template). Each call site passes a captureless lambda that decays
  to that pointer (`ChapterCompletionFile.cpp:21-26`, `PassageFile.cpp:60-63`,
  `TagPaletteFile.cpp:21-24`, `BookmarkFile.cpp:37-42`, `HighlightFile.cpp:26-28`).
- **Naming.** `AdoptedLoad` / `adoptedLoad` (`TempAdoption.h:57-81`) follows the existing pair
  `TempAdoptionAction` / `tempAdoptionAction` and `adoptedReadStatus`, and it is a `constexpr` pure
  mapping kept free of Arduino, like its siblings. Each per-store `LoadResult` becomes
  `using LoadResult = AdoptedLoad;`, so the enumerator names are unchanged and all consumers
  (`StudyStore.cpp:44-57`, `MigrationRunner.cpp:209-379`, `LauncherActivity.cpp:84-85`,
  `EpubReaderBookmarksActivity.cpp:35,50`) compile untouched.
- **Renaming `DeleteTempReportEmpty` to `KeepTempReportEmpty`.** The name now says what the code
  does. The rationale comment moves from the old call site onto the enumerator
  (`TempAdoption.h:23-27`), where both consumers can see it.
- **Error handling.** It matches the established shape. `LOG_ERR` plus a `Failed` result is used
  for a rejected primary and for a rejected recovered `.tmp`
  (`PersistableStore.cpp:98-104`). The rename-failure `LOG_ERR` / recovery `LOG_INF` split is kept
  (`PersistableStore.cpp:63-70`). `PassageFile::readInto` gains a `LOG_ERR` on a zero-byte file
  (`PassageFile.cpp:38-40`), which brings it in line with `readDocFromFileChecked`'s empty-file log
  (`PersistableStore.cpp:52-54`). Per-store module tags give way to `"PERSIST"` on these lines. The
  spec records this as A-10, and every line still names the path.
- **Dead code.** None. `#include <TempAdoption.h>` is dropped from each `.cpp` that stopped using it
  and added to the headers that now alias `AdoptedLoad`. Each remaining `MODULE` constant is still
  used by its `save()` (for example `ChapterCompletionFile.cpp:32`, `TagPaletteFile.cpp:32`).
- **Tests.** They are designed, not just present.
  - `LoadAdoptingTest.cpp` asserts card state (bytes on disk, `.tmp` presence), not only the return
    value. It covers each action arm: the rejected-but-still-promoted `.tmp` (line 100), the
    failed rename (108), and the transient-read case that motivates keeping the `.tmp`
    (127). It also proves that the caller's reader is the one used for both reads (136).
  - `TempAdoptionTest.cpp:116-141` adds an exhaustive property sweep: `Empty` is never reported
    over a present primary, and success is never reported for a rejected document.
  - Each per-store suite adds one wiring test for the issue's case. `PassageFileTest.cpp:75-86`
    checks that the streaming reader is threaded through to the `.tmp` read. It uses a fixture
    that asserts it exceeds the 50,000-byte cap (`ASSERT_GT`, line 78), so it cannot pass
    vacuously.

BookmarkFile and HighlightFile have no storage-fake suite. `HighlightFile.h:12-13` says so openly,
and their call sites have the same shape as the three stores that are tested, so I am not raising
this as a finding.

## Findings

### MINOR 1: stale line reference left in `TempAdoption.h`

`lib/Serialization/TempAdoption.h:7-9` still says the interrupted window is
"between PersistableStore.cpp:38 (remove the destination) and :39 (rename…)". The remove and the
rename are actually at `PersistableStore.cpp:39` and `:40`. The citation was already off by one at
the merge base, so this PR did not introduce the error. But the PR removed the same rotting
`:38/:39` citation from `PersistableStore.h`, replacing it with "an interrupted rename in
writeDocToFileAtomic" (`PersistableStore.h:130`), and it edited `TempAdoption.h` in the same change.

**Fix:** use the symbolic wording here as well. For example: "between writeDocToFileAtomic's remove
of the destination and its rename of the temp file over it".

### MINOR 2: `HighlightFileAction.h` names the wrong sharer of the load rule

`src/util/HighlightFileAction.h:6-8` says the load-side rule "now lives in
Serialization/TempAdoption.h, shared with PersistableStoreBase::readDocFromFileAdopting."
After this PR, HighlightFile loads through `loadAdopting` (`HighlightFile.cpp:26`), not
`readDocFromFileAdopting`. The pointer now sends a reader to the one adopting path HighlightFile
does not use. The file is not in the diff, but the PR made the comment wrong.

**Fix:** name `PersistableStoreBase::loadAdopting`. `HighlightFile.h:12-14` already words it
correctly and can serve as the model.

VERDICT: CLEAR
