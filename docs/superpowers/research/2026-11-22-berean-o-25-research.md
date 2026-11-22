# Research: berean-o-25 — read the preaching cards file (`PreachingHandouts`)

Bead `berean-o-25`, task `t1`, branch `feature/preaching-handouts-reader`, cut from
`6fb52ff5 chore: add the preaching cards design (#279)`. Scope is the plan's Tasks 3–5
(`docs/superpowers/plans/2026-11-22-preaching-cards.md:519-988`): a read-only reader, its host
tests, and the `docs/file-formats.md` section. No UI, strings, `PreachingOrigin` or
`PreachingContent`.

## Who owns this behaviour today

Nobody. `grep -rn 'PreachingHandouts\|preaching-cards' src lib test` returns nothing, so
`src/study/PreachingHandouts.{h,cpp}` and `test/storage_io/PreachingHandoutsTest.cpp` are new
files. Nothing in the firmware reads `/preaching-cards.json` yet, and this task adds no caller:
the Tarjetas tab that will call `summaries()`/`load()` is the follow-up task.

The new reader sits beside its model in `src/study/`; the callers of the model are all in
`src/activities/preaching/` (`PreachingContent.{h,cpp}`, `PreachingSession.{h,cpp}`,
`PreachingTopicActivity.cpp`), which is where the follow-up will call the new one.

## The model: `PreachingPassages`

`src/study/PreachingPassages.{h,cpp}` is the read-only reader the plan mirrors almost line for
line. Its control flow:

1. A public pass (`countByTag`, `PreachingPassages.cpp:161`; `passagesWithTag`, `:190`) clears
   `out`, builds an ArduinoJson **filter** document, and makes the parse document with the
   caller's allocator or the default (`newDocument`, `:49-51`).
2. `readFiltered` (`:59-95`) is shared by both passes, so the filtered `deserializeJson`
   template is instantiated once (`:57-58`). In order: filter overflow → `OutOfMemory` (`:60-63`);
   `Storage.exists` false → `Missing`, checked before opening because a failed open logs
   (`:66-67`); open failure → `Unreadable` (`:70`); size `0` or over the cap → `Unreadable`
   **before** any parse (`:71-76`); stream through `BufferedFileReader` (512 B) wrapped in a
   one-byte `JsonFileReader` (`:22,29-40,78-80`); `NoMemory` or `doc.overflowed()` →
   `OutOfMemory` (`:81-84`); any other parse error → `Unreadable` (`:85-88`); version and
   top-level array shape → `Unreadable` (`:89-93`).
3. The pass then walks the document, `reserve()`s before pushing (`:137,174`), copies each text
   into a `study::PassageText` built on `allocators.text` (`:143-144`), and on a failed
   `assign` clears `out` and returns `OutOfMemory` (`:149-153`) — nothing partial.
4. `LOG_INF` with `millis()` timing at the end (`:185-186,204-205`), `started` marked
   `[[maybe_unused]]` because the host log stub drops its arguments
   (`test/stubs/Logging.h:6-7`).

Differences the plan's reader introduces, all deliberate per spec §2–3
(`docs/superpowers/specs/2026-11-22-preaching-cards-design.md:117-150`):

| | `PreachingPassages` | `PreachingHandouts` (plan Task 4) |
|---|---|---|
| Path | built under `sdpaths::PASSAGES_DIR` (`:53-55`) | constant `FILE_PATH` "/preaching-cards.json" at the SD root, not in `SdPaths.h` |
| Size cap | `PassageDoc::SAVE_BYTE_BUDGET + 4096`, file-local (`:24`) | `MAX_FILE_BYTES` 128 KiB, public (the test needs it) |
| Version | `persist::isKnownFormatVersion` → one `Unreadable` (`:90`) | `v > 1` → `TooNew`; absent/≤0 → `Unreadable`. Adds a `TooNew` status |
| Shape | `doc["p"]` array | `doc["cards"]` array |
| Skip rule | row with no parsable start (`:98`) | verse whose href is empty or > 63 B |

## APIs the plan's code depends on — each checked

- `study::PassageText` (`lib/StudyStore/StudyStore/PassageText.h:29-87`): move-only;
  `assign(std::string_view)` returns false on allocation failure and keeps the previous text
  (`:53-66`); an empty string assigns without allocating (`:55-58`). `operator==` with
  `std::string_view` exists (`:89`), so the test's `EXPECT_EQ(verse.text, "Seven.")` compiles.
  `TextAllocator` is `{allocate, release}` function pointers (`:15-20`).
- `study::PassageDoc::Allocators` (`lib/StudyStore/StudyStore/PassageDoc.h:55-58`):
  `{TextAllocator text = defaultTextAllocator(); ArduinoJson::Allocator* json = nullptr;}`. The
  plan's `load(index, {}, out)` therefore means malloc text and ArduinoJson's default allocator,
  as on the host. `MAX_REFERENCE_BYTES = 48` (`:44`).
- `utf8SafeSummary(std::string, size_t)` (`lib/Utf8/Utf8.h:32`): collapses whitespace, trims,
  caps without splitting UTF-8. The plan uses it for titles (96 B), headings (96 B) and refs
  (48 B); verse text is not cut.
- `ReaderEntryIntent::HREF_BYTES = 64` and `char href[HREF_BYTES]`
  (`src/activities/reader/ReaderEntryIntent.h:23,30`); its setter refuses an empty link or one
  `>= HREF_BYTES` (`:48-50`). So `MAX_HREF_BYTES = 63` is exactly what the intent can hold.
- `serialization::BufferedFileReader(HalFile&, size_t)` (`lib/Serialization/BufferedFile.h:74-77`).
- `PsramJsonAllocator::json()` and `::passageDoc()` (`src/study/PsramJsonAllocator.h:18,22`):
  what the firmware caller (follow-up task) must pass, per the header comment the plan copies
  from `PreachingPassages.h:18-20`.
- Why the SD root: File Transfer returns 403 for any protected path
  (`src/network/FileRoutes.cpp:124`, `webroutes::isProtectedWebPath`,
  `src/network/WebRouteUtils.h:8`), and uploading is how the file reaches the card (spec D1).

## Limits checked against the real file

The brief gives the owner's generated file: 65,151 bytes, 77 verses in the largest handout over
8 sections, one verse text of 9,153 bytes.

- **Size:** 65,151 < 131,072 (`MAX_FILE_BYTES`). Over `SDCardManager::readFile`'s 50,000-byte
  silent truncation (CLAUDE.md "Storage discipline"), which is why the reader must stream —
  and the model already does.
- **String length:** ArduinoJson 7.4.2 stores string lengths in 2 bytes on any target with
  pointers wider than 2 bytes, "up to 65535 characters"
  (`build/test/_deps/arduinojson-src/src/ArduinoJson/Configuration.hpp:140-145`). The 9,153-byte
  Levítico 13 text fits on both the host and the ESP32-S3.
- **Nesting:** root object → `cards` → card → `sections` → section → `verses` → verse is 7
  levels; ArduinoJson's default limit is 10 (`Configuration.hpp:93`).
- **Counts:** 77 verses and 8 sections fit `Summary::verses`/`Verse::section` (`uint16_t`).
- `docs/file-formats.md` must say "about 65 KB", not the plan's "about 50 KB"
  (plan `:986`; brief).

## Host test harness

- Suite: `test/storage_io/CMakeLists.txt` builds one executable, `StorageIoTest`, from eight test
  files (`:9-17`) plus real sources including `src/study/PreachingPassages.cpp` (`:24`) and
  `lib/Utf8/Utf8.cpp` (`:30`); `src/study` and `lib/StudyStore` are already on the include path
  (`:33-41`). It links `ArduinoJson` and `GTest::gtest_main` (`:43-47`) and is registered with
  `gtest_discover_tests` (`:49`). `test/CMakeLists.txt:109` adds the subdirectory, so the plan
  needs no edit to the shared `test/CMakeLists.txt` (which `.claude/agents/data-dev.md` says not
  to touch).
- The plan's edit to the comment on `test/storage_io/CMakeLists.txt:2` matches the current text
  ("the TagPaletteFile, PassageFile and PreachingTopicsFile loaders").
- Fake storage: `storage_fake::reset()` and `putFile(const std::string&, std::string)`
  (`test/stubs/HalStorageFake.h:17,23`); `PreachingPassages` already exercises `exists`,
  `openFileForRead` and `size()` against it.
- `millis()` is stubbed on the host returning `uint32_t` 0 (`test/stubs/Arduino.h:20`), so the
  plan's timing lines compile.
- OOM pattern: the model test injects a failing text allocator through
  `PassageDoc::Allocators` (`test/storage_io/PreachingPassagesTest.cpp:181-194`); the plan's
  `failAllocation`/`releaseNothing` test is the same mechanism.

**Baseline, run on this branch before any change:**

```
$ cmake -S test -B build/test && cmake --build build/test --target StorageIoTest -j
[100%] Built target StorageIoTest
$ ./build/test/storage_io/StorageIoTest --gtest_brief=1
[==========] 98 tests from 8 test suites ran. (374 ms total)
[  PASSED  ] 98 tests.
```

Pre-existing host warnings in that build, not ours: gtest's `gtest-printers.h:528`
(`-Wcharacter-conversion`), and `-Wunused-const-variable` for `MODULE` in
`src/study/PassageFile.cpp:10` and `src/study/TagPaletteFile.cpp:12` (the log stub swallows its
arguments). The new reader passes `MODULE` to `Storage.openFileForRead`, as the model does, so it
will not add a third.

**Trap in the plan's verification command.** Plan Task 4 Step 4 runs
`ctest --test-dir build/test -R StorageIo`. ctest's names are the gtest names
(`PreachingPassagesIo.*`, …), none contains "StorageIo", and ctest fails open:

```
$ ctest --test-dir build/test -R StorageIo; echo "exit=$?"
No tests were found!!!
exit=0
```

The whole suite must be run as the executable (`./build/test/storage_io/StorageIoTest`) or with
an unfiltered `ctest`. `-R PreachingHandouts` does work: the fixture is `PreachingHandoutsIo`, as
`-R PreachingPassages` matches the model's 12 tests today.

## Installed tool versions

| Tool | Version | Evidence |
|---|---|---|
| CMake | 4.4.2 | `cmake --version` |
| Host compiler | Apple clang 21.0.0 (clang-2100.0.123.102) | `c++ --version` |
| PlatformIO Core | 6.1.19 | `/Volumes/stein/.platformio/penv/bin/pio --version` |
| ArduinoJson (firmware) | 7.4.2 | `platformio.ini:155` |
| ArduinoJson (host) | v7.4.2, pinned to match | `test/CMakeLists.txt:25-33` |
| clang-format | the repo's `.venv/bin/clang-format` (present; `.claude/pipeline-bootstrap` runs `./bin/bootstrap`) | `ls .venv/bin/clang-format` |

**Firmware baseline: not captured.** `pio run -e x4pro` was started on this branch, but this fresh
worktree was still downloading `framework-arduinoespressif32@3.3.7` and its libs when this note was
committed, so no baseline warning count is recorded here. That does not block the acceptance
check: `PreachingHandouts.cpp` is a new translation unit, so pio's build cache cannot have
restored an object for it, and its warnings appear in the first build that compiles it. Grep that
build for `PreachingHandouts`.

## Documentation insertion point

`docs/file-formats.md:577` is `## \`/.berean/passages/<pubkey>.json\``; the section before it,
`/.berean/preaching.json` (`:549-575`), is the style to match (owner, task, `### Version 1`,
a `v` bullet, a JSON example, then field bullets and caps).

## Conclusion

The plan's Tasks 3–5 are implementable as written against today's code: every API they call
exists with the signature they assume, and the real file is within every limit. Two corrections
for the implement phase: run the whole suite with the executable rather than
`ctest -R StorageIo`, and write "about 65 KB" in `docs/file-formats.md`.

The tier stays `standard`: the change is two new files in `src/study`, one host test, one line
pair in `test/storage_io/CMakeLists.txt` and a docs section. It reads a file but writes none, so
no migration, no cache version, and no contract with another surface beyond the header the
follow-up task will call.
