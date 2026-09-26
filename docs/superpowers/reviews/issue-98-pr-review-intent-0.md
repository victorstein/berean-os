# PR #127 review — intent (pass 0)

**PR:** #127, `fix: keep an unusable .tmp on every study-data load`
**Branch:** `fix/98-single-tmp-adoption-policy`
**Against:** issue #98; spec `docs/superpowers/specs/2026-09-26-issue-98-design.md`; plan
`docs/superpowers/plans/2026-09-26-issue-98-plan.md`
**Host tests run by the reviewer:** `cmake -S test -B build/test && cmake --build build/test && ctest --test-dir build/test`,
942/942 passed. This matches the PR's claim.

## Findings

None at BLOCKER, MAJOR or MINOR.

## Issue #98 acceptance criteria

| Criterion | Status | Evidence |
|---|---|---|
| Pick one policy (keep) | Met | `lib/Serialization/TempAdoption.h:192-197` renames the arm to `KeepTempReportEmpty` and gives the A-12 reason in the comment. `grep -rn "Storage.remove" ` finds no call against a store `.tmp` in the five loaders. The only `TempAdoptionAction::` uses outside `TempAdoption.h` are in `PersistableStore.cpp:84,104,122,125`, and none of them removes anything. |
| Replace the five copies with one helper in `lib/Serialization` | Met | `PersistableStoreBase::loadAdopting` (`PersistableStore.cpp:91-106` in the diff; declared at `PersistableStore.h:171`). All five `load()` bodies are now a single call: `ChapterCompletionFile.cpp:21-26`, `PassageFile.cpp` `load`, `TagPaletteFile.cpp:21-24`, `BookmarkFile.cpp:37-44`, and `HighlightFile.cpp:26-28`. `readDocFromFileAdopting` runs on the same private `readAdopting` core, so the sequence exists once. |
| Host tests for the helper against the #99 fake | Met | `test/storage_io/LoadAdoptingTest.cpp`, 11 tests. |
| Verify: a garbage `.tmp` with no primary reports empty and leaves the `.tmp` on disk | Met | `LoadAdoptingTest.cpp`, `AGarbageTempIsKeptAndTheLoadIsEmpty`. The same case is also covered through the real Tag, ChapterCompletion and Passage loaders. |

## Spec requirements

- **A-1 (rename, no remove):** implemented as described above. Historical docs keep the old name, as the spec allows.
- **A-2 / A-7 (helper beside `readDocFromFileAdopting`, one shared core):** implemented with an anonymous-namespace
  `readAdopting`. `readDocFromFileAdopting` keeps `doc.clear()` on the keep arm only, and the ReportFailed rationale
  is carried over. `AdoptingReadTest.cpp` is absent from the diff, so it is unmodified, and it passes.
- **A-3 (`AdoptedLoad` plus aliases):** the enum is in `TempAdoption.h`. All five headers use
  `using LoadResult = AdoptedLoad;` and include `<TempAdoption.h>`. The values and their order match the
  enums they replace, so no caller changed.
- **A-4 (`adoptedLoad` pure mapping, `default` → `Failed`):** matches the §Result type table row for row.
- **A-5 (reader parameter, the same reader for the primary and the `.tmp`):** implemented. `readInto` now
  takes `const char*`. `TheSuppliedReaderReadsBothThePrimaryAndTheTemp` proves the reader is called twice. The
  passage suite's `.tmp` over 50,000 bytes proves the streaming reader is the one used on the `.tmp`.
- **A-6 (context-first function pointer, captureless lambdas, no `std::function` or template):** implemented.
- **A-8 (one `JsonDocument`):** implemented. The spec states the precondition that readers return early on a
  missing file, and the core comment restates it.
- **A-9 (promote before `fromJson`; a rejected promoted file returns `Failed` with the file at the primary):**
  implemented and tested (`ARejectedTempFailsButIsStillPromoted`).
- **A-10 (logging moves under `PERSIST`; TagPaletteFile gains the rejection log):** implemented through the
  shared `LOG_ERR` lines in `loadAdopting`.
- **A-11 (unreadable and unparseable stay one case; `Failed`-on-unreadable deferred):** honoured. The
  follow-up is proposed in the PR's "Not in this PR" section, which is what the spec asked for.
- **A-12 (Bookmark `clear()` and the `LOG_DBG` on `Loaded`):** both kept (`BookmarkFile.cpp:32`, `:45`).
- **A-13 / A-14 (tests; no shared-file edit):** the new tests are in `test/storage_io/CMakeLists.txt` only, and
  the dependencies are exactly the ones §Testing lists.
- **§Error handling, the zero-length passage log:** `readInto` now logs `"%s is empty"`, and
  `AnEmptyTempIsKeptAndTheLoadIsEmpty` covers that branch.
- **§Call sites, stale comments:** the `PersistableStore.h` comments are updated to point at
  `KeepTempReportEmpty`. `readDocFromFileChecked` is described as the default `DocReader`, and the renaming
  hazard is stated once, on `loadAdopting`, naming all eight files. `HighlightFile.h:9-15` names
  `loadAdopting`. The per-switch comments in Bookmark and Highlight are removed. The
  `test/storage_io/CMakeLists.txt` header is refreshed.

## Scope

- **No reduction.** Every item in §Goal and §Testing is present.
- **No expansion.** No `save()` path, on-disk format, UI string or caller of the five `load()` functions changed.
  The other `.tmp` users (`BibleSearchIndexer`, `ProgressFile`, `FontDownloadActivity`) are untouched, as the
  non-goals require.
- `git log main..HEAD` also shows `0e0e7641 chore(main): release 1.16.4`. That comes from a stale local
  `main`. `gh pr diff 127 --name-only` does not include the release-please files, so the PR does not carry them.

## Tests exercise behaviour

The loader tests assert what ends up on the card: `fileBytes` of the `.tmp` and the primary, and whether each
exists. They also assert what reached the store: `target.v`, `target.calls`, and the equality of the loaded
record. They do not restate the implementation. Two tests are worth singling out:

- The unreadable-`.tmp` case (`failReadsOf(TMP_PATH)`) is the transient-failure scenario that motivates the
  whole policy.
- The passage fixture over the read cap has an `ASSERT_GT(bytes.size(), 50000u)` guard, so the test cannot pass
  vacuously.

The exhaustive `adoptedLoad` test iterates over the inputs to `tempAdoptionAction`, not over the
`TempAdoptionAction` values directly. It still reaches all five actions, because Ok gives UseLoaded, the Missing
combinations give the next three, and Unreadable or ParseError give ReportFailed. The `default` arm is
unreachable by construction, so this is not a gap.

## Plan conformance

The test names and their order match the plan exactly (plan lines 225-970). The +28 count the plan predicts at
line 1266 matches the PR's 914 → 942. The PR explains its one departure from the plan: the
`grep -rn "Storage.remove(tmp"` check was expected to produce no output but matches unrelated cache files. The
PR substitutes narrower greps. I re-ran them and both return nothing outside `TempAdoption.h` and
`PersistableStore.cpp`. The device recipe in the PR matches spec §Testing.

VERDICT: CLEAR
