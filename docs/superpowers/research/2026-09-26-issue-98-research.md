# Issue #98 research: one `.tmp` adoption policy, in one place

Branch `fix/98-single-tmp-adoption-policy`, base `6c57bcd0` (#121, the storage fake).
Every claim below was checked by reading the cited line or running the cited command.

## 1. What owns the behaviour

### The shared helper — keeps the `.tmp`

`PersistableStoreBase::readDocFromFileAdopting` (`lib/Serialization/PersistableStore.cpp:64-107`):

- Reads the primary with `readDocFromFileChecked` (`:65`); only when that is `Missing` does it
  look at `<path>.tmp`, reading it **into the same `doc`** (`:70-74`).
- Switches on `tempAdoptionAction` (`:76`):
  - `PromoteTempAndUseIt` → `Storage.rename(tmp, path)`; `LOG_INF "Recovered …"` on success,
    `LOG_ERR "Failed to promote …"` on failure, and the document is still returned (`:78-89`).
  - `DeleteTempReportEmpty` → `doc.clear()` (`:96`) and **no delete**; the comment at `:97-101`
    carries the policy.
- Returns `adoptedReadStatus(primary, action)` (`:106`): a `DocReadStatus`, so a promoted
  `.tmp` and a loaded primary are both `Ok`, and `ReportFailed` keeps `Unreadable`/`ParseError`.

Callers: `PersistableStore<T>::loadFromFile` (`PersistableStore.h:186`),
`MigrationRunner.cpp:63,90`, `PubKeyRegistry.cpp:23,56,82`, `MeetingWeekCache.cpp:23`
(`grep -rn "readDocFromFileAdopting(" src`).

### The decision table — shared already

`lib/Serialization/TempAdoption.h`: `TempAdoptionAction` (`:22-28`), `tempAdoptionAction`
(`:30-43`), `adoptedReadStatus` (`:49-61`). Pure `constexpr`, host-tested in
`test/temp_adoption/TempAdoptionTest.cpp` (12 tests, run below). The enumerator is still named
`DeleteTempReportEmpty` (`:26`) even though the shared helper does not delete.

### The five hand-rolled loaders — delete the `.tmp`

Each one re-implements the read-primary / maybe-read-tmp / switch sequence, and each deletes on
`DeleteTempReportEmpty`:

| Loader | whole `load()` | delete arm | primary reader | `LoadResult` enum |
|---|---|---|---|---|
| `src/study/ChapterCompletionFile.cpp` | `:20-63` | `:55-57` | `readDocFromFileChecked` (`:25`) | `ChapterCompletionFile.h:22` |
| `src/study/PassageFile.cpp` | `:56-99` | `:91-93` | local streaming `readInto` (`:31-48`) | `PassageFile.h:21-26` |
| `src/study/TagPaletteFile.cpp` | `:21-63` | `:55-57` | `readDocFromFileChecked` (`:26`) | `TagPaletteFile.h:16` |
| `src/util/BookmarkFile.cpp` | `:32-86` | `:78-80` | `readDocFromFileChecked` (`:41`) | `BookmarkFile.h:19-24` |
| `src/util/HighlightFile.cpp` | `:25-72` | `:64-66` | `readDocFromFileChecked` (`:30`) | `HighlightFile.h:22-27` |

All five `LoadResult` enums have the same four values in the same order —
`Loaded, Empty, RecoveredFromTemp, Failed` — declared five times
(`grep -rn "enum class LoadResult" src lib` returns exactly these five).

## 2. Current control flow of a per-store `load()`

Taking `ChapterCompletionFile::load` (`:20-63`) as the representative:

1. Build `primaryPath` and `tmpPath = primaryPath + ".tmp"` (`:21-22`).
2. Read primary into `primaryJson` (`:24-25`).
3. If `Missing`: `Storage.exists(tmp)`; if present, read it into a **second** `JsonDocument`
   `tempJson` (`:27-35`).
4. `switch (tempAdoptionAction(...))` (`:37`):
   - `UseLoaded` → `record.fromJson(primaryJson)`; `Loaded`, else `LOG_ERR` + `Failed` (`:38-41`).
   - `ReportEmpty` → `Empty` (`:43-44`).
   - `PromoteTempAndUseIt` → rename first (log on failure), then `fromJson(tempJson)`;
     `RecoveredFromTemp`, else `LOG_ERR` + `Failed` (`:46-53`).
   - `DeleteTempReportEmpty` → **`Storage.remove(tmpPath)`** → `Empty` (`:55-57`).
   - `ReportFailed` → `Failed` (`:59-60`).

### Where the five copies differ from each other

- **Reader.** `PassageFile` streams through a `HalFileReader` over `HalFile`
  (`PassageFile.cpp:14-48`) because passages can exceed `SDCardManager::readFile`'s 50,000-byte
  cap (`PassageFile.h:10-13`). The other four use `readDocFromFileChecked`, which goes through
  `Storage.readFile` (`PersistableStore.cpp:51`). A single helper therefore has to take the reader
  as a parameter — the issue's `loadAdopting(path, reader, fromJson)` shape.
- **`fromJson` shape.** Four are members, `bool fromJson(JsonVariantConst)`
  (`ChapterCompletion.h:50`, `PassageDoc.h:95`, `TagPalette.h:65`, `HighlightDoc` via
  `HighlightFile.cpp:44`). Bookmarks use a free function
  `BookmarkDoc::fromJson(JsonVariantConst, std::vector<BookmarkEntry>&)` (`BookmarkDoc.h:50`).
- **Side effects outside the switch.** `BookmarkFile::load` clears the output vector first
  (`:33`) and `LOG_DBG`s the count on `Loaded` (`:58`).
- **Logging.** Module tags differ (`COMPLETE`, `PASSAGE`, `TAGS`, `BKM`, `HLFILE`).
  `TagPaletteFile`'s promote-then-reject path returns `Failed` with no `LOG_ERR`
  (`TagPaletteFile.cpp:51-52`); the other four log it. None of the five logs a successful
  recovery; the shared helper does (`PersistableStore.cpp:82`).

### Where the per-store copies differ from the shared helper

| | shared helper | per-store copies |
|---|---|---|
| unusable `.tmp` | kept (`PersistableStore.cpp:97-101`) | removed |
| result type | `DocReadStatus` (promoted == `Ok`) | 4-way `LoadResult` (`RecoveredFromTemp` distinct) |
| `Unreadable` vs `ParseError` | preserved | both → `Failed` |
| `fromJson` rejection | caller's problem | folded into `Failed`, including after a promote |
| documents held | one `doc` | two (`primaryJson`, `tempJson`) |
| reader | `readDocFromFileChecked` only | per store (streaming for passages) |

## 3. Why keeping the `.tmp` is safe (the #51 argument, re-verified today)

Design `docs/superpowers/specs/2026-09-17-issue-51-design.md:312-339` (A-12). Rechecked against
the current tree:

1. **The delete buys nothing.** `SDCardManager::writeFile` removes an existing destination
   before re-creating it (`freeink-sdk/libs/hardware/SDCardManager/src/SDCardManager.cpp:276-284`).
   All five `save()`s go through `writeDocToFileAtomic` (`ChapterCompletionFile.cpp:74`,
   `PassageFile.cpp:112`, `TagPaletteFile.cpp:75`, `BookmarkFile.cpp:107`, `HighlightFile.cpp:87`),
   which writes `<path>.tmp` with `Storage.writeFile` (`PersistableStore.cpp:31`). The next save
   reclaims the bytes regardless.
2. **A delete can destroy the only copy.** `SDCardManager::readFile` returns `""` both when the
   card is not initialised (`:191-194`) and when the open fails (`:196-199`). That classifies as
   `Unreadable` (`PersistableStore.cpp:52-54`), so `tempParsed == false`, so the delete arm. The
   fake reproduces exactly this with `storage_fake::failReadsOf` (`test/stubs/HalStorageFake.h:27-29`).
3. **An over-cap `.tmp` would be silently removed.** A `.tmp` past the 50,000-byte cap
   (`SDCardManager.cpp:202`) reads back truncated and fails to parse — delete arm again.
   (This one does not apply to passages, whose reader streams.)

A `.tmp` beside a *present* primary is never consulted by any path (`TempAdoption.h:17-21`), so
keeping an unusable `.tmp` cannot later shadow or overwrite a good primary.

`load()` callers act on `Empty` by treating the store as new; none of them reads the `.tmp`
directly. `grep -rn '\.tmp' src` finds the five loaders plus three unrelated `.tmp` files with
their own paths — `BibleSearchIndexer.cpp:283`, `ProgressFile.h:34` (`progress.bin.tmp`),
`FontDownloadActivity.cpp:87` — none of which uses `TempAdoptionAction`. Call sites:
`StudyStore.cpp:43,49,57`, `MigrationRunner.cpp:208,238,277,379`, `LauncherActivity.cpp:83`,
`EpubReaderActivity.cpp:1771`, `EpubReaderBookmarksActivity.cpp:34`.

## 4. Constraints a single helper has to respect

- **`lib/` must not include `src/`** — the reason #64 moved the decision out of
  `src/util/HighlightFileAction.h` (commit `6156de32` message, "Design notes"). So the helper in
  `lib/Serialization` cannot name any of the five `LoadResult` enums; the shared result type has
  to live in `lib/Serialization`, and the per-store enums either alias it or are mapped.
- **No `std::function`, prefer function pointer + context** (`CLAUDE.md`, "Template and
  `std::function` bloat"). The reader and `fromJson` hooks need that shape, or a template with
  explicit instantiation.
- **JSON machinery lives in one TU.** `PersistableStore.h:14-22`: `serializeJson`/`deserializeJson`
  are kept in `PersistableStore.cpp` to avoid per-TU `.isra` clones. `PassageFile.cpp:42` already
  instantiates `deserializeJson` with its own reader type in its own TU.
- **Single-task ownership.** Every file in play is owned by the Arduino loop task
  (`ChapterCompletionFile.h:14-17`, `PassageFile.h:15-16`, `BookmarkFile.h:11-16`,
  `HighlightFile.h:17-19`; the renaming-read hazard at `PersistableStore.h:80-87`). A helper adds
  no concurrency; it must not add a lock on this read path either (`data-dev.md`,
  "Never lock `storageMutex` on a read path the renderer sits behind").
- **Shared test file.** `test/CMakeLists.txt` is report-don't-edit (`.claude/agents/data-dev.md`,
  "Shared files"). It already has `add_subdirectory(storage_io)` (`:103`) and
  `add_subdirectory(temp_adoption)` (`:102`), so tests added to those existing suites need no
  shared-file line. `test/storage_io/CMakeLists.txt` is not on the shared list.

## 5. Installed versions

| Tool / package | Version | Evidence |
|---|---|---|
| PlatformIO Core | 6.1.19 | `~/.platformio/penv/bin/pio --version` |
| ArduinoJson (firmware) | 7.4.2 | `platformio.ini:151` |
| ArduinoJson (host tests) | v7.4.2 | `test/CMakeLists.txt:31` |
| GoogleTest | v1.17.0 | `test/CMakeLists.txt:17` |
| CMake | 4.4.2 | `cmake --version` (project minimum 3.16, `test/CMakeLists.txt:1`) |
| Host compiler | Apple clang 21.0.0 | `c++ --version` |

Baseline, run on this branch before any change:

    cmake -S test -B build/test && cmake --build build/test --target StorageIoTest TempAdoptionTest
    build/test/storage_io/StorageIoTest      → [  PASSED  ] 36 tests.
    build/test/temp_adoption/TempAdoptionTest → [  PASSED  ] 12 tests.

## 6. Host-test coverage today, and the dependency

- #99 (the storage fake) is **closed**, by PR #121, merged 2026-09-26T01:55:04Z
  (`gh issue view 99`, `gh pr view 121`). The fake is `test/stubs/HalStorageFake.{h,cpp}`, with
  failure hooks `failReadsOf`, `failRenamesFrom`, `failWritesTo` (`HalStorageFake.h:27-37`).
- `test/storage_io/` compiles the real `PersistableStore.cpp` and `TagPaletteFile.cpp` against it
  (`test/storage_io/CMakeLists.txt:8-18`).
- The shared helper's keep-the-`.tmp` behaviour is already pinned:
  `AdoptingRead.AnUnparseableTempReportsMissingAndStaysOnTheCard`
  (`test/storage_io/AdoptingReadTest.cpp:61-67`). That is the exact assertion the issue's
  "Verify" asks of the new helper.
- `TagPaletteFileTest.cpp:1-3` deliberately **skips** the `DeleteTempReportEmpty` arm, citing
  this issue. None of the other four loaders' `.cpp` files is compiled by any host suite today;
  `HighlightFile.cpp` pulls `lib/Epub` (`HighlightDoc.h`) and `lib/PathFlatten`, and
  `BookmarkFile.cpp` pulls `BookmarkUtil.cpp` → `PathFlatten`, `Utf8`.

## 7. Nearest existing example of this kind of change

**#64 / commit `6156de32`** ("adopt an orphaned `.tmp`…", closes #51). It took a load-side rule
duplicated in `src/`, moved it into `lib/Serialization/TempAdoption.h`, added a shared I/O helper
(`readDocFromFileAdopting`) beside `readDocFromFileChecked`, and changed the four then-existing
adopters mechanically (`git show --stat 6156de32`: `PassageFile.cpp`, `TagPaletteFile.cpp`,
`BookmarkFile.cpp`, `HighlightFile.cpp`, 13–15 lines each). It explicitly left the adopters on
their own switch (A-6) and the delete in place (A-12) — the two things this issue now reverses.

The fifth copy arrived afterwards with `ChapterCompletionFile` in #78 (`1269fced`,
`git log -S TempAdoptionAction -- lib src`).

For the tests, the model is **#121 / `6c57bcd0`**: `test/storage_io/AdoptingReadTest.cpp` for
helper I/O against the fake, and `test/storage_io/TagPaletteFileTest.cpp` for a real per-store
loader compiled unmodified.

## 8. Open questions for the spec

- **Result type.** The helper must return something `lib/`-resident with at least the four
  `LoadResult` values. Whether the five per-store enums become aliases of it (one-line headers,
  no caller changes because every value keeps its name) or stay and are mapped, is a spec choice.
- **Hook shape.** Reader and `fromJson` hooks as function pointer + `void*` context, per
  `CLAUDE.md`; the bookmark free-function `fromJson` and the passages streaming reader are the two
  cases that force a context argument.
- **Recovery log.** Whether the helper logs `LOG_INF "Recovered …"` on promotion as the shared
  helper does (`PersistableStore.cpp:82`) — that line is what the #64 on-device test reads.
- **Enumerator name.** `DeleteTempReportEmpty` will describe no caller once the five stop deleting.
  Renaming it touches `TempAdoption.h` and `TempAdoptionTest.cpp` only.
- **Whether `readDocFromFileAdopting` itself is re-expressed on the new helper**, so the policy
  truly exists in one place, or stays as the `DocReadStatus`-returning sibling.
