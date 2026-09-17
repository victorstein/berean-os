# An orphaned `.tmp` is never adopted — investigation

Investigated 2026-09-16 on `70041d90`, the tip of `fix/51-adopt-orphaned-tmp`
(identical to `main`), for issue #51.

**Verdict: the issue is correct, its line citations are accurate, and the fix it
asks for already exists in this repo — for four *other* files.** Four study files
adopt an orphaned `.tmp` through a pure, host-tested decision function. The gap
is that the `PersistableStore` path never learned it. So this is not a design
problem; it is a "wire the existing mechanism into the remaining callers"
problem, and the only real question is *which* seam to wire it at.

The scope is also wider than the issue states: **four more files** write through
`writeDocToFileAtomic` and read without adoption, and one of them loses data
harder than "fall back to defaults."

---

## 1. The reported defect, confirmed line by line

`writeDocToFileAtomic` (`lib/Serialization/PersistableStore.cpp:22-44`):

```cpp
  if (!Storage.writeFile(tmpPath.c_str(), json)) {          // :30
  Storage.remove(finalPath.c_str());                        // :38
  if (!Storage.rename(tmpPath.c_str(), finalPath.c_str())) {// :39
```

`readDocFromFileChecked` (`lib/Serialization/PersistableStore.cpp:46-61`) opens
with `if (!Storage.exists(path))` at `:47` and never mentions `tmpPath`. There
is no `.tmp` string anywhere else in the file — `grep -n tmp
lib/Serialization/PersistableStore.cpp` returns only lines 25, 30, 31, 39.

`loadFromFile()` (`PersistableStore.h:157-177`) calls `readDocFromFile` at
`:164` and `return false`s on anything that is not `Ok`, so a `Missing` primary
is indistinguishable from a corrupt one at the store level: the store keeps its
in-memory defaults, and the next `saveToFileAtomic()` writes those defaults over
the `.tmp` (`writeFile(tmpPath...)` at `:30` truncates it before the rename).
The issue's chain is exactly right.

`HalStorage::rename` (`lib/hal/HalStorage.cpp:94-96`) is a thin
`HAL_STORAGE_WRAPPED_CALL` onto SdFat, so the "SdFat's rename does not overwrite"
comment at `PersistableStore.cpp:35-37` is the reason the remove has to come
first. That ordering is not negotiable, which is why adoption on the read side is
the fix rather than a different write sequence.

## 2. The fix already exists: `highlightLoadAction`

`src/util/HighlightFileAction.h:26-47` is a `constexpr` function over
`DocReadStatus` + two booleans returning a five-way `HighlightLoadAction`
(`UseLoaded`, `ReportEmpty`, `PromoteTempAndUseIt`, `DeleteTempReportEmpty`,
`ReportFailed`). Its rule at `:42-45` is precisely what the issue asks for:

> a `.tmp` is never consulted when the primary file is present (whether readable
> or not) — `HighlightFileAction.h:23-25`

Four call sites already switch on it, with near-identical bodies:

| File | Primary read | Adoption block |
| --- | --- | --- |
| `src/util/HighlightFile.cpp` | `:28` | `:39-68` |
| `src/util/BookmarkFile.cpp` | `:41` | `:54-84` |
| `src/study/TagPaletteFile.cpp` | `:27` | `:40-64` |
| `src/study/PassageFile.cpp` | `:62` (own streaming `readInto`) | `:72-100` |

All four promote by `Storage.rename(tmpPath, path)` *before* validating the
parsed content, with the same comment reasoning — e.g. `HighlightFile.cpp:50-53`:
"Promote first: the rename is what rescues the only surviving copy … the primary
path is Missing, so nothing here can be overwritten." That ordering is an
established decision in this repo, not something #51 has to re-litigate.

It landed in `d4413ba9 feat(util): add HighlightFile storage shell with
status-returning load` and was generalised across stores by `#42`/`#47`
(`5d2a34f0`, `d043fd84`). **`git log --oneline -- src/util/HighlightFileAction.h`**
shows two commits total; the file has been stable since.

It is host-tested: `test/highlight_file/HighlightFileActionTest.cpp` has nine
tests across `HighlightLoadAction` and `HighlightSaveAction`, including
`MissingPrimaryWithAParsedTempIsPromoted` (`:44`) and
`MissingPrimaryWithAnUnparseableTempIsDiscarded` (`:48`). Verified green:

```
$ cmake -S test -B build/test && cmake --build build/test --target HighlightFileActionTest -j8
$ ./build/test/highlight_file/HighlightFileActionTest
[==========] 9 tests from 2 test suites ran. (0 ms total)
[  PASSED  ] 9 tests.
```

So the decision logic this fix needs is already written, already exhaustively
tested, and already the repo's convention. **Nothing new needs inventing.**

## 3. The exposed surface is eight files, not four

Everything that calls `writeDocToFileAtomic` (directly or via
`saveToFileAtomic()`) and reads back without adoption:

| # | File written | Read path | Consequence of the lost window |
| --- | --- | --- | --- |
| 1 | `/.crosspoint/settings.json` (`CrossPointSettings.h:408`) | `loadFromFile()` — `main.cpp:411` | every setting back to default |
| 2 | `/.crosspoint/state.json` (`CrossPointState.h:36`) | `loadFromFile()` — `main.cpp:412` | reading position lost |
| 3 | `/.crosspoint/recent.json` (`RecentBooksStore.h:37`) | `loadFromFile()` — `main.cpp:413`, `LauncherActivity.cpp:77`, `PublicationsActivity.cpp:45` | recents list empty |
| 4 | `/.crosspoint/wifi.json` (`WifiCredentialStore.h:50`) | `loadFromFile()` — `WifiSelectionActivity.cpp:95` | every network re-entered by hand |
| 5 | `/.berean/pubkeys.json` (`PubKeyRegistry.h:30`) | `readDocFromFileChecked` — `PubKeyRegistry.cpp:23,52,78` | **see below** |
| 6 | `/.berean/migration-ledger.json` (`MigrationRunner.h:47`) | `readDocFromFileChecked` — `MigrationRunner.cpp:55,74` | migration idempotence guard lost |
| 7 | `/.berean/migration-report.json` (`MigrationRunner.h:46`) | written at `:137`, never read | cosmetic |
| 8 | `/.berean/meeting-weeks.json` (`MeetingWeekCache.h:31`) | `readDocFromFileChecked` — `MeetingWeekCache.cpp:23` | cache refetch; recoverable |

Rows 5-8 are not in the issue's scope paragraph. Rows 5 and 6 matter.

**`PubKeyRegistry::record` is read-modify-write and ignores the read status**
(`PubKeyRegistry.cpp:22-23`): it calls `readDocFromFileChecked(PATH, doc)`
without checking the result, then mutates `doc` and writes it back at `:42`. If
`pubkeys.json` is gone and `pubkeys.json.tmp` holds the real registry, the very
next `record()` writes a document containing **one** entry and the rename
destroys the `.tmp`. That is not "fall back to defaults" — it is a silent
truncation of every other registered publication, on the first download after
the interrupted write.

`MigrationRunner::appendLedger` (`MigrationRunner.cpp:73-83`) has the same
read-ignore-mutate-write shape. Losing the ledger makes `runIfPending`
(`:156`) re-migrate files it already migrated; whether that duplicates passages
depends on `PassageDoc` merge semantics, which I did **not** verify.

## 4. The seam question the issue raises

The issue asks whether adoption belongs in `readDocFromFileChecked` (covers
everything at once) or per store. Two facts bear on it that the issue does not
mention:

1. **`readDocFromFileChecked` is already used to read the `.tmp` itself.**
   `HighlightFile.cpp:36`, `BookmarkFile.cpp:49` and `TagPaletteFile.cpp:35`
   call it with `tmpPath.c_str()`. Adoption inside that function would make
   those calls look for `<path>.tmp.tmp`, and — worse — the shared function
   would promote the `.tmp` before the caller's own `switch` had decided to,
   double-handling the rescue the four callers implement deliberately.
2. **It is currently a pure read.** `PubKeyRegistry::findBySymbol` (`:78`) and
   `MeetingWeekCache::load` (`:23`) call it on read-only paths. Adoption renames
   a file, so putting it there turns every read into a potential write. The
   `storeMutex` comment at `PersistableStore.h:27-36` is emphatic that read
   paths must not acquire locks the render path sits behind; adoption's
   `Storage.exists` + `Storage.rename` both take `storageMutex` inside
   `HalStorage`.

Neither fact forecloses the shared-path option, but both are why the issue's
instinct — "it deserves the same care #28's review applied before widening a
shared function" — is right. An opt-in helper beside `readDocFromFileChecked`,
mirroring `highlightLoadAction`'s split of decision from I/O, is the shape the
repo already has four working instances of. **That is a spec-phase decision, not
a research finding**; I am recording the constraints, not choosing.

## 5. Host-testability

`PersistableStore.cpp` cannot be built on the host: `PersistableStore.h:3`
includes `<Arduino.h>` unconditionally and there is no host stub for it —
`test/highlight_file/CMakeLists.txt:1-4` and
`test/bookmark_save_action/CMakeLists.txt:1-2` both say so explicitly, and it is
why `HighlightFileAction.h` exists as a separate Arduino-free header at all.

So a TDD red test for #51 can only cover a **pure decision function**, on the
model of `DocReadStatus.h:17` (`classifyDocRead`), `SaveBudget.h:26`
(`fitsBudget`) and `HighlightFileAction.h:34` (`highlightLoadAction`) — each of
which has its own single-header test directory
(`test/doc_read_status/`, `test/save_budget/`, `test/highlight_file/`) with a
four-line `CMakeLists.txt` and one `add_subdirectory` line in
`test/CMakeLists.txt`. The I/O around it is device-only and stays the human
tester's job.

Note that `test/CMakeLists.txt` is on data-dev's **report, do not edit** list
(`.claude/agents/data-dev.md`) — if this work adds a test directory, the
`add_subdirectory` line goes in the PR description for the orchestrator.

## 6. Environment, as measured in this worktree

| Tool | Version | How |
| --- | --- | --- |
| CMake | 4.4.2 | `cmake --version` |
| git | 2.54.0 | `git --version` |
| PlatformIO | at `/Volumes/stein/.platformio/penv/bin/pio` — **not on `PATH`** | `ls`; `pio --version` → `command not found` |
| Host OS | Darwin (macOS) | `uname -s` |
| GoogleTest | v1.17.0, FetchContent | `test/CMakeLists.txt:17` |
| ArduinoJson | v7.4.2, pinned to firmware | `test/CMakeLists.txt:31`, `platformio.ini:151` |
| `freeink-sdk` submodule | `310ec61506fc915836db7799a2e7f4fc135a570d` — **already initialised** | `git submodule status` |

Bootstrap state contradicts the batch note in part: the submodule needs no
`git submodule update --init`. `pio` and the clang-format venv
(`/Volumes/stein/Documents/development/personal/berean-os/.venv/bin/clang-format`)
do both need to be put on `PATH` before building firmware or running
`./bin/clang-format-fix`.

`cmake -S test -B build/test` configured cleanly from scratch (exit 0) and the
suite builds; `build/` is gitignored.

## 7. What is not in scope

- **Issue #39** owns the user-visible refusal surface. No
  `lib/I18n/translations/` edit and no new UI belongs in #51.
- The issue's own note stands: the few-millisecond window has **never been hit
  on hardware**, so the defect is read from code, not observed. A deliberate
  delay between `PersistableStore.cpp:38` and `:39` makes it reproducible, and
  only the human tester can confirm the fix on device.
