Tier: heavy

# PR #145 intent review 0: streaming store writer (issue #100)

Reviewed against issue #100, the spec `docs/superpowers/specs/2026-09-27-issue-100-design.md`,
and the plan `docs/superpowers/plans/2026-09-27-issue-100-plan.md`. Branch head `f24e3ed4`.

I checked two things locally. `StorageIoTest` gives 67/67 and `PersistableStoreTest` gives 7/7
(`cmake --build build/test --target StorageIoTest PersistableStoreTest`), which matches the PR
body. The code diff also matches the plan's code blocks line for line.

## Issue acceptance criteria

| Issue asks | Where it is met |
|---|---|
| Writer adaptor, `serializeJson` straight into the `.tmp` `HalFile`, then the existing remove-then-rename | `JsonFileWriter` over a 512 B `BufferedFileWriter` (`lib/Serialization/PersistableStore.cpp:36-53,66-71`); `writeDocStreamed` opens, serialises, closes (`:73-88`); `writeDocToFileAtomic` calls it and keeps the remove/rename below (`:103-110`). |
| Reader and writer adaptors in `lib/Serialization`, usable by every store | Both adaptors are in `PersistableStore.cpp`'s anonymous namespace (`:17-53`). The reader is exposed as `PersistableStoreBase::readDocFromFileStreamed` (`PersistableStore.h:81`, `.cpp:140-160`). The writer is reached by every store through `writeDocToFileAtomic`. Spec A-3 explains why the adaptors are not in a header: one TU instantiates the JSON code. The PR body says the same. |
| Create the parent of the path being written, not a hardcoded dir | `ensureParentDirectory` (`PersistableStore.cpp:56-62`) is used in both writers (`:93`, `:104`). The `SdPaths.h` include and `CROSSPOINT_DIR` use are gone. |
| Verify: MEM logging around a large passage save | The PR's "Device verification" section replaces `getMaxAllocHeap` with the scoped `heap_caps_monitor_local_minimum_free_size_*` check. It gives the reason: `getMaxAllocHeap` is internal-only and the removed String lived in PSRAM. This matches spec §"What only the human tester can verify" and is argued, not a silent substitution. The on-device run is correctly left to the human. |
| Verify: round-trip host test once #99 lands | `PassageFileIo.SaveThenLoadRoundTripsAFilePastTheReadCap` (`test/storage_io/PassageFileTest.cpp:70-82`). |

## Spec requirements

- **A-1/A-2 (reuse `BufferedFileWriter`, 512 B heap buffer per save):** met, `PersistableStore.cpp:15,67`.
- **A-4/A-4b (success needs open, `flush()` and `close()`; no `measureJson`):** met, `:74-87`.
  There is no second measure pass.
- **A-5 (partial `.tmp` left):** met. Asserted by `AtomicWriteTest.cpp` in
  `AShortTempWriteLeavesThePrimaryUntouchedAndTheTempBehind` (`fileBytes(TMP_PATH) == {"v`).
- **A-6/A-7 (explicit close before rename; buffer scoped and flushed before close):** met.
  `serializeInto` owns the buffer's lifetime (`:66-71`), and the file is closed after it returns
  (`:82`). The `BufferedFileWriter` destructor's second `flush()` is a no-op once `fill == 0`
  (`BufferedFile.h:31,60-62`).
- **A-9 (derived parent; skip empty or `/`; result ignored; `pFlag` default):** met, `:57-61`,
  `HalStorage.h:34`. It covers every `writeDocToFileAtomic` caller I found
  (`MigrationRunner.cpp:119,174`, `PubKeyRegistry.cpp:47`, `TagPaletteFile.cpp:37`,
  `ChapterCompletionFile.cpp:38`, `PassageFile.cpp:38`, `BookmarkFile.cpp:65`,
  `HighlightFile.cpp:42`, `MeetingWeekCache.cpp:58`). Each caller now gets a superset of the
  directory it got before.
- **A-10 (`writeDocToFile` keeps String body, mkdir only):** met, `:92-101`. It has its own test
  (`TheNonAtomicWriteCreatesTheTargetsOwnParentAndNotCrosspoint`).
- **A-11 (reader moved verbatim, module becomes `PERSIST`, `PassageFile::load` uses it):** met,
  `PersistableStore.cpp:140-160`, `src/study/PassageFile.cpp:22`.
- **A-12 (`failWritesAfter` hook, cleared by `clearFailures`/`reset`, its own tests):** met,
  `test/stubs/HalStorageFake.h:40-42`, `.cpp:33,68,87,231-234`, plus two `HalStorageFakeTest`
  cases.
- **A-13 (report measured sizes, claim no direction in advance):** met. The PR reports measured
  before and after sizes.
- **Comment corrections** (`TempAdoption.h:26-30`, `PersistableStore.h:59-67,94`, `BookmarkFile.cpp`,
  `HighlightFile.cpp`): done. The new `TempAdoption.h` claim (`O_TRUNC`) holds on device:
  `SDCardManager.cpp:337`.
- **Non-goals respected:** the on-disk bytes are unchanged, and the existing byte-exact
  `AtomicWriteTest` passes. `readDocFromFileChecked` is untouched. No other store migrates to the
  streaming reader. The per-caller mkdirs stay. No public signature changed. `test/CMakeLists.txt`
  is untouched.

I found no scope reduction and no scope expansion. The integrity fix (a short write no longer
replaces the destination) is not in the issue text. The spec names it as the integrity half of the
same change (Problem 2), and the PR describes it openly.

## Tests exercise behaviour

- The two short-write tests drive the real path through a fake that fails the way a full card
  does. They assert what the user would observe: the destination is byte-identical, or absent.
  The failure hook only works through `HalFile::write`, so the old `writeFile` path could not pass
  them. The mutation claim holds: with `flush()` ignored, the first test's 3-byte `.tmp` would be
  renamed over `PATH`.
- The parent-directory tests assert the resulting card state (`isDir("/.crosspoint")` is false and
  the file is present). They do not check which mkdir calls were made.
- The 60 KB test round-trips through both new adaptors.

## Plan divergence

None. The commits follow plan steps 1 to 6 in order. The only commit missing is step 7's
`style: format` commit, which the plan requires only if `clang-format-fix` changes something, and
the PR says it changed nothing.

## Findings

### MINOR-1 — The round-trip test does not compare the reloaded passages, although the PR says it does

`test/storage_io/PassageFileTest.cpp:79-81` asserts only that `load` returns `Loaded` and that the
passage count matches. The PR body says the reload "returns it byte-exact", and the spec's goal says
the same (spec line 74). The byte-exact assertion is on the saved file (`:76`), not on what `load`
returns. A reader that dropped or garbled content past 50,000 bytes but kept the element count
would still pass. That risk is low. `ATempPastTheReadCapIsRecoveredWhole` already covers the
streamed read of a >50 KB passage file, and the new 60 KB `AtomicWriteTest` checks the full string
length. The count-only assertion is also exactly what the spec's TDD step 4 and the plan specified,
so this is not a divergence.

Inline fix: assert `serialised(loaded) == bytes` after the load, or reword the PR line to
"saved byte-exact and reloaded with every passage".

## Verdict

The PR meets every acceptance criterion in the issue and every spec assumption from A-1 to A-13.
It respects the non-goals and matches the plan exactly. It has one MINOR, a test-strength and
wording point that can be fixed inline.

VERDICT: CLEAR
