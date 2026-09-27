# Issue #100 research — streaming store writer

Branch `fix/100-streaming-store-writer`, based on `e41880e3` (release 1.16.9).
`git log HEAD..origin/main` was empty when this was written.

## Who owns the behaviour

| Concern | File |
|---|---|
| Atomic write (the String serialisation) | `lib/Serialization/PersistableStore.cpp:23-45` |
| Non-atomic write (same String pattern) | `lib/Serialization/PersistableStore.cpp:12-21` |
| Declarations and their contracts | `lib/Serialization/PersistableStore.h:59-66` |
| Streaming reader adaptor (the one to move) | `src/study/PassageFile.cpp:13-28` (`HalFileReader`), used by `readInto` at `:30-50` |
| Passage save (budget, mkdir, atomic write) | `src/study/PassageFile.cpp:66-78` |
| The 200 KB budget | `lib/StudyStore/StudyStore/PassageDoc.h:31` — `SAVE_BYTE_BUDGET = 200000` |
| Existing buffered writer over `HalFile` | `lib/Serialization/BufferedFile.h:26-72` (`serialization::BufferedFileWriter`) |
| Host fake | `test/stubs/HalStorageFake.{h,cpp}`, suite `test/storage_io/` |

The issue's line numbers have drifted by one: the hardcoded mkdir is now
`PersistableStore.cpp:24` (issue says `:23`) and the String serialisation is
`:28-29` (issue says `:27-28`). `PassageDoc.h:31` and `PassageFile.cpp:16-47`
still match.

## Current control flow of a passage save

1. `PassageFile::save` builds a `JsonDocument` (`PassageFile.cpp:67-68`).
2. `measureJson(json) > SAVE_BYTE_BUDGET` refuses before any I/O (`:70-73`).
3. `Storage.mkdir(sdpaths::PASSAGES_DIR)` (`:75`).
4. `writeDocToFileAtomic(target, json)` (`:77`), which:
   - `Storage.mkdir(sdpaths::CROSSPOINT_DIR)` unconditionally (`PersistableStore.cpp:24`),
     for a `/.berean/passages/...` path;
   - `String json; serializeJson(doc, json);` (`:28-29`);
   - `Storage.writeFile(tmpPath, json)` (`:31`) — the SDK removes an existing
     file, opens `O_RDWR|O_CREAT|O_TRUNC`, `f.print(content)`, and returns
     `written == content.length()` (`freeink-sdk/libs/hardware/SDCardManager/src/SDCardManager.cpp:276-295`, `:336-343`);
   - `Storage.remove(final)` then `Storage.rename(tmp, final)` (`:39-44`).

### What the String costs, mechanically

- ArduinoJson 7.4.2's `Writer<::String>` buffers `ARDUINOJSON_STRING_BUFFER_SIZE`
  = 32 bytes and `concat`s every 31 (`.pio/libdeps/x4pro/ArduinoJson/src/ArduinoJson/Serialization/Writers/ArduinoStringWriter.hpp:25-45`,
  `Configuration.hpp:264-265`).
- Arduino-ESP32 3.3.7's `String::concat` → `reserve(newlen)` → `changeBuffer`,
  which `realloc`s to `(maxStrLen + 16) & ~0xf` — exact size rounded to 16, no
  geometric growth (`framework-arduinoespressif32/cores/esp32/WString.cpp:172-183`, `:205-212`, `:329-339`).
  A 200,000-byte document is therefore on the order of 200,000 / 31 ≈ 6,450
  `realloc` calls, the late ones each moving a block approaching 200 KB.
- `CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=4096`
  (`framework-arduinoespressif32-libs/esp32s3/sdkconfig:2154`): once the String
  passes 4 KB it lives in PSRAM, so this is PSRAM churn plus a contiguous block
  of up to 200 KB, not internal-SRAM exhaustion. The first ~4 KB of growth is in
  internal SRAM.

### A correctness gap the issue does not name

On OOM, `String::concat` returns false; the ArduinoJson writer's `flush()`
then keeps its bytes and `write` returns 0, but serialisation continues and the
return value of `serializeJson` is discarded at `PersistableStore.cpp:29`. The
String now holds a prefix of the document. `writeFile` compares against
*that* String's length (`SDCardManager.cpp:292-294`), so it reports success,
and the truncated `.tmp` is renamed over the good file. The atomic write is
only atomic against power loss, not against allocation failure. A streaming
writer must compare bytes written against `measureJson(doc)` and refuse the
rename on a mismatch.

## How the writer adaptor must be shaped

- ArduinoJson writes character by character: `writeString` → `writeChar` →
  `writeRaw(char)` → `writer_.write(uint8_t)` (`Json/TextFormatter.hpp:38-62`,
  `:169-171`). A custom writer is any type with `write(uint8_t)` and
  `write(const uint8_t*, size_t)` (`Serialization/Writer.hpp:11-27`).
- On device, every `HalFile::write` takes `storageMutex` and calls into SdFat
  (`lib/hal/HalStorage.cpp:138-141`, `:160`). Writing `HalFile` directly would be
  one mutex round trip per character. The adaptor needs a buffer; the repo
  already has one — `serialization::BufferedFileWriter`
  (`BufferedFile.h:26-72`): fixed buffer allocated once, degrades to
  passthrough on OOM, `flush()` returns false after any short write. Its only
  user today is `BookMetadataCache` (`lib/Epub/Epub/BookMetadataCache.cpp:84,135,190`).
  A buffer ≤ 4096 bytes stays in internal SRAM per the threshold above.
- On device `HalFile` is itself a `Print` (`lib/hal/HalStorage.h:60`), but the
  host stub is not (`test/stubs/HalStorage.h:61`), so `serializeJson(doc, file)`
  would not compile in `StorageIoTest`. A duck-typed adaptor class with the two
  `write` overloads — the mirror of `HalFileReader` — compiles in both.
- `PersistableStore.h:15-23` explains why `serializeJson`/`deserializeJson` are
  instantiated only in `PersistableStore.cpp`: GCC emits per-TU `.isra` clones
  (~0.5 KB each). The moved adaptors should therefore stay inside
  `PersistableStore.cpp` behind non-template functions (a streaming
  `DocReader` for `loadAdopting`, and the writer inside
  `writeDocToFileAtomic`), not be exposed as a header that every store
  instantiates. Moving `readInto` out of `PassageFile.cpp` also removes the one
  `deserializeJson` instantiation outside `PersistableStore.cpp` on the store
  path (`grep` shows it at `src/study/PassageFile.cpp:44`; the others are in the
  web server and `FontDownloadActivity`).
- `SDCardManager::openFileForWrite` truncates with `O_TRUNC` instead of
  removing first (`SDCardManager.cpp:337`); the fake models `writeFile`'s
  remove-first and truncates on `openFileForWrite`
  (`test/stubs/HalStorageFake.cpp:128-132`, `:163-167`). Either way the `.tmp`
  starts empty; a failed write leaves a partial `.tmp`, which the adopting read
  already handles (`PersistableStore.cpp:66-97`; kept when unusable, per #127).

## Directory creation today

`HalStorage::mkdir(path, pFlag = true)` → SdFat `vol().mkdir(path, pFlag)`
(`lib/hal/HalStorage.h:34`, `SDCardManager.h:57`) creates parents, and fails on
an existing target (fake models this: `HalStorageFake.cpp:105-114`). Every
direct caller of `writeDocToFileAtomic` already creates its own parent:

| Caller | Its mkdir |
|---|---|
| `src/study/PassageFile.cpp:77` | `PASSAGES_DIR` at `:75` |
| `src/study/ChapterCompletionFile.cpp:38` | `COMPLETION_DIR` at `:36` |
| `src/study/TagPaletteFile.cpp:37` | `BEREAN_DIR` at `:36` |
| `src/study/PubKeyRegistry.cpp:47` | `BEREAN_DIR` at `:46` |
| `src/study/MigrationRunner.cpp:119,174` | `BEREAN_DIR` at `:118,173` |
| `src/network/MeetingWeekCache.cpp:58` | `BEREAN_DIR` at `:57` |
| `src/util/BookmarkFile.cpp:66` | bookmarks dir at `:65` |
| `src/util/HighlightFile.cpp:44` | highlights dir at `:42` |

The CRTP stores (`PersistableStore<T>::saveToFileAtomic`,
`PersistableStore.h:199-212`) do not, and rely on the hardcoded
`/.crosspoint` mkdir: every one of their paths is under it
(`lib/Serialization/SdPaths.h:14-17`, asserted at `:38-41`). Deriving the parent
from the path covers both groups and makes the per-caller mkdirs redundant
(removing them is optional cleanup, not required). `writeDocToFile` (`:13`) has
the same hardcoded mkdir; `saveToFile()` has no callers (`PersistableStore.h:179-180`,
confirmed by `grep -rn "saveToFile()" src lib`).

## Tests that exist

- `test/storage_io/AtomicWriteTest.cpp` — `writeDocToFileAtomic` byte-exact
  output, temp removal, failure hooks; uses `/.crosspoint/store.json` (`:14-15`).
- `test/storage_io/PassageFileTest.cpp:58-68` — save-then-load round trip of
  **one** small passage. No test saves a document past 50,000 bytes; the only
  large-file test is a pre-seeded `.tmp` read (`:75`).
- Harness: `test/storage_io/CMakeLists.txt` compiles the real
  `PersistableStore.cpp` and `PassageFile.cpp` against the fake. New test files
  need a line in that `CMakeLists.txt` (the per-suite file, not the shared
  `test/CMakeLists.txt`).

## Installed versions

| Tool | Version | Evidence |
|---|---|---|
| ArduinoJson | 7.4.2 | `platformio.ini:154`; `test/CMakeLists.txt:28-32` pins `v7.4.2`; `.pio/libdeps/x4pro/ArduinoJson/library.json` |
| Platform | pioarduino 55.03.37 | `platformio.ini:15` |
| Arduino-ESP32 core | 3.3.7 | `framework-arduinoespressif32/package.json` |
| ESP-IDF libs | 5.5.0+sha.87912cd291 | `framework-arduinoespressif32-libs/package.json` |
| PlatformIO Core | 6.1.19 | `pio --version` |
| Host compiler / CMake | Apple clang 21.0.0, CMake 4.4.2 | `c++ --version`, `cmake --version` |

## Nearest existing examples

- **Reader adaptor to mirror**: `HalFileReader` in `src/study/PassageFile.cpp:15-28`.
- **Buffered write over HalFile**: `serialization::BufferedFileWriter`,
  `lib/Serialization/BufferedFile.h:26-72`.
- **A `Print`-shaped sink fed by a library**: `ScannerPrint`,
  `src/study/BibleSearchStore.cpp:61-78`.
- **Prior change of this kind** (atomic/adopting I/O in `PersistableStore.cpp`
  with a host test in `test/storage_io/`): #127 (`c46f87a2`) and #64 (`6156de32`).

## Scope and tier

Everything sits in the `data` surface: `lib/Serialization` plus
`src/study/PassageFile.cpp`. The public signature of `writeDocToFileAtomic`
need not change, so its nine call sites (including `src/network/MeetingWeekCache.cpp`)
are untouched. No on-disk format changes — the bytes written are the same
`serializeJson` output, which `AtomicWriteTest`'s byte-exact assertion will
confirm. Tier stays `heavy`; nothing found warrants raising it.

## Open questions for the spec

1. Buffer size for the writer: 512 (one SdFat sector) versus larger; must stay
   ≤ 4096 to remain internal SRAM, or deliberately exceed it for PSRAM.
2. Whether `writeDocToFile` (non-atomic, no callers) gets the same streaming
   treatment or keeps its String.
3. Whether to delete the now-redundant per-caller mkdirs in this change.
