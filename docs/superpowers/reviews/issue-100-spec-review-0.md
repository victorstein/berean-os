Tier: heavy

# Spec review 0 — issue #100, streaming store writer

Reviewed: `docs/superpowers/specs/2026-09-27-issue-100-design.md` (the spec), against
`gh issue view 100 --repo victorstein/berean-os`, the research note
`docs/superpowers/research/2026-09-27-issue-100-research.md`, and the code at `cbabe9aa`.

## Verified and sound

Checked against the source and found correct. These need no change:

- **The String cost.** ArduinoJson 7.4.2's String writer concatenates every 31 bytes
  (`ArduinoStringWriter.hpp:26-47`). `String::changeBuffer` reallocs to
  `(maxStrLen + 16) & ~0xf` (`WString.cpp:205-212`), and `concat` calls `reserve(newlen)`
  (`:329-339`).
- **The 200 KB budget and its measurement.** `PassageDoc::SAVE_BYTE_BUDGET = 200000`
  (`PassageDoc.h:31`). `getMaxAllocHeap` reads `MALLOC_CAP_INTERNAL`, so the issue's proposed
  metric is the wrong one (`Esp.cpp:171-173`).
- **The OOM-truncation gap is real.** On a failed `concat` the String writer returns 0 and
  drops bytes (`ArduinoStringWriter.hpp:26-31,41-47`). `SDCardManager::writeFile` compares
  against the shortened String (`SDCardManager.cpp:292-294`).
- **The writer shape.** ArduinoJson's generic `Writer<T>` duck-types `write(uint8_t)` and
  `write(const uint8_t*, size_t)` (`Serialization/Writer.hpp:12-27`). `TextFormatter` writes
  strings one character at a time (`TextFormatter.hpp:39-64,169-171`). The host `HalFile` is
  not a `Print` (`test/stubs/HalStorage.h:61`), so a duck-typed adaptor is required.
- **Device `HalFile`.** `close()` returns `bool` (`lib/hal/HalStorage.h:94`), and SdFat
  propagates the `sync()` result (`SdFat/src/FatLib/FatFile.cpp:128-133`,
  `FsLib/FsFile.cpp:58-63`). Each `write` takes `storageMutex` (`HalStorage.cpp:138-141,160`).
  `openFileForWrite` is `O_RDWR | O_CREAT | O_TRUNC` (`SDCardManager.cpp:337`).
- **`BufferedFileWriter`.** Its OOM passthrough and its short-write reporting through `flush()`
  work as claimed (`BufferedFile.h:28-57`). A `512`-byte buffer is under the
  `CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=4096` threshold, and no `custom_sdkconfig` line in
  `platformio.ini:94-117` overrides it.
- **The fake.** `mkdir` refuses an existing target and, with `pFlag`, creates missing parents
  (`HalStorageFake.cpp:105-114`). `writeFile` never calls `HalFile::write` (`:128-133`), so TDD
  step 3 genuinely fails on today's code. TDD step 2 fails as stated (`:130`).
- **Every CRTP store path is under `/.crosspoint`** (`SdPaths.h:13-19`, asserted `:38-44`).
  `PersistableStoreTest` uses `/.crosspoint/probe.json` (`PersistableStoreTest.cpp:25`), so
  parent derivation leaves both groups unchanged.

## Findings

### MAJOR-1 — Only one of the two host suites that compile `PersistableStore.cpp` gets the new include path

**Claim** (spec "Files touched", lines 174-177): add `${REPO_ROOT}/lib/Memory` to
`test/storage_io/CMakeLists.txt` for `BufferedFile.h`'s `<Memory.h>`. "`test/CMakeLists.txt`
... is not touched."

**Problem.** A second host suite compiles the real `PersistableStore.cpp`:
`test/persistable_store/CMakeLists.txt:3-13`, registered at `test/CMakeLists.txt:128`. Its
include path is only `test/stubs` and `lib/Serialization`, plus `${REPO_ROOT}` and
`${REPO_ROOT}/lib` from `crosspoint_test_common` (`test/CMakeLists.txt:38-41`). Once
`PersistableStore.cpp` includes `BufferedFile.h`, `PersistableStoreTest` stops compiling.

The failure is misleading on this host. macOS is case-insensitive, so `<Memory.h>` silently
resolves to the SDK's `<memory.h>`, and the error surfaces as an undeclared identifier rather
than a missing file. That is easy to misdiagnose.

**Evidence.** A syntax-only compile of `#include <BufferedFile.h>` with exactly
`PersistableStoreTest`'s include dirs gives:

```
lib/Serialization/BufferedFile.h:29:25: error: use of undeclared identifier 'makeUniqueNoThrow'
lib/Serialization/BufferedFile.h:77:25: error: use of undeclared identifier 'makeUniqueNoThrow'
2 errors generated.
```

The same compile with `-I lib/Memory` added is clean. The spec's own gate, "the full host suite
via CMake/ctest" (line 287), would fail.

**Fix.** Add `${REPO_ROOT}/lib/Memory` to `test/persistable_store/CMakeLists.txt` as well, and
list that file under "Files touched". Neither change touches the shared `test/CMakeLists.txt`.

### MAJOR-2 — A-4's `serializeJson == measureJson` check cannot fail, so its stated purpose is false

**Claim** (A-4, lines 96-103): a save succeeds only if the count `serializeJson` returned equals
`measureJson(doc)`. "The check is what makes a short write detectable even if a future sink stops
reporting through `flush()`." The error table (line 248) lists "`serializeJson` count ≠
`measureJson`" as a failure mode.

**Problem.** `serializeJson`'s return value is the sum of the adaptor's own `write` returns
(`CountingDecorator.hpp:16-22`, fed from `serialize.hpp:19-24`). The spec's `JsonFileWriter`
always returns `1` and `n` (spec lines 193-194), by design: "Reports every byte as taken". So the
count is always exactly `measureJson(doc)`, whatever reaches the card.

The check therefore:

- never fires;
- cannot detect a sink that stops reporting through `flush()` — that is exactly the case where it
  also reports full counts;
- makes the error-table row unreachable and untestable;
- adds a full CPU pass over a document of up to 200 KB. `PassageFile::save` already measures once
  (`PassageFile.cpp:70`), so each save pays three passes.

This is a half-applied carry-over from the research note (lines 62-64). There the comparison made
sense, because the String writer does return 0 on a dropped byte (`ArduinoStringWriter.hpp:28-29`).
The spec's own TDD step 3 relies on this: it asks the implementer to confirm the test fails when
`flush()` is ignored. That only works because the count check does not catch the short write.

**Fix.** Drop the `measureJson` comparison and its error-table row. State that integrity rests on
two checks: `BufferedFileWriter::flush()`, which latches any short `HalFile::write` in both the
buffered and passthrough paths (`BufferedFile.h:41,62`), and the `close()` result.

The goal "a save whose bytes did not all reach the card never replaces the destination"
(line 42) is unchanged. If a belt-and-braces check is still wanted, compare
`buffered.position()` with the file size after close. Do not compare against a count the adaptor
fabricates.

### MAJOR-3 — The on-device verification cannot observe a transient peak

**Claim** ("What only the human tester can verify", lines 298-302): log
`ESP.getMinFreePsram()` / `ESP.getMaxAllocPsram()` immediately before and after a passage save.
"Before this change the PSRAM low-water mark should dip by roughly the file size."

**Problem.** Both measurements miss the String:

- `getMaxAllocPsram` / `getFreePsram` are point samples (`Esp.cpp:189-199`). The String is freed
  before `writeDocToFileAtomic` returns, so an "after" sample never sees it.
- `getMinFreePsram` is `heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM)`, a low-water mark
  **since boot** (`Esp.cpp:189`). It dips only if the save sets a new all-time low. Any earlier
  PSRAM peak larger than the save hides it. CLAUDE.md's PSRAM table lists such peaks: the catalog
  index (~217 KB) and the Bible search build (~2.9 MB).

So the "before" run can show no dip, and the comparison then proves nothing. A false pass is the
likely outcome on a card that has been used.

**Evidence.** The installed ESP-IDF provides a scoped low-water mark for exactly this case:
`heap_caps_monitor_local_minimum_free_size_start()` / `_stop()`
(`framework-arduinoespressif32-libs/esp32s3/include/heap/include/esp_heap_caps.h:258,268`).
Its header says it "allows to detect local lows of the minimum_free_bytes value that wouldn't be
detected otherwise".

**Fix.** Rewrite the device check in four steps:

1. Call `heap_caps_monitor_local_minimum_free_size_start()`.
2. Record `heap_caps_get_free_size(MALLOC_CAP_SPIRAM)`, then do the save.
3. Log the free size minus `heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM)` with
   `LOG_DBG("MEM", ...)`.
4. Call `..._stop()`.

Before the change the delta should be about the file size (> 50 KB); after, about 0. Repeat the
same probe for `MALLOC_CAP_INTERNAL`, where the expected delta is about 512 B.

### MINOR-1 — TDD step 4 is labelled fail-first but passes on today's code

**Claim** (lines 259-262): "Each step: write the test ... see it fail for the stated reason." Step
4 is the > 50,000-byte save-then-load round trip.

**Problem.** Step 4 gives no failure reason, and none exists. Today's
`writeFile` → `readInto` path already round-trips it on the fake: `writeFile` has no size cap
(`HalStorageFake.cpp:128-133`), and the reader streams (`PassageFile.cpp:30-50`). It is a
characterisation test. That is valuable because the issue asks for it, but it does not prove the
String is gone. The streaming path is proved by step 3's hook, which bites only through
`HalFile::write`.

**Fix.** Mark step 4 as expected to pass before and after, as a regression guard. Name step 3 as
the test that proves the write streams.

### MINOR-2 — A mechanism comment the change falsifies is missing from "Files touched"

**Problem.** `lib/Serialization/TempAdoption.h:26-30` explains why an unusable `.tmp` is kept:
"the next save truncates it, since `SDCardManager::writeFile` removes the destination before
re-creating it". After this change no store save goes through `writeFile`. The truncation comes
from `openFileForWrite`'s `O_TRUNC` (`SDCardManager.cpp:337`), which A-5 already cites.

The spec corrects the two `BookmarkFile`/`HighlightFile` comments (line 171) for the same reason
but misses this one. Separately, `PersistableStore.h:86-87` ("passes a streaming reader") can now
name `readDocFromFileStreamed`.

**Fix.** Add `TempAdoption.h` to "Files touched", with the mechanism changed to
`openFileForWrite`'s `O_TRUNC`. Optionally name the new reader in the `DocReader` comment.

### MINOR-3 — Wrong line citation for the PSRAM threshold

**Claim** (spec line 21; research note line 49):
`CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=4096` is at
`framework-arduinoespressif32-libs/esp32s3/sdkconfig:2154`.

**Evidence.** `grep -n SPIRAM_MALLOC_ALWAYSINTERNAL .../esp32s3/sdkconfig` gives
`2260:CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=4096`. Line 2154 is a brownout-detector option. The
value is right; only the line number is wrong.

**Fix.** Cite `:2260`.

### MINOR-4 — The data-flow pseudocode passes a temporary to a non-const reference

**Claim** (line 217): `written = serializeJson(doc, JsonFileWriter(buffered))`.

**Problem.** The custom-destination overload is `serializeJson(JsonVariantConst, TDestination&)`
(`Json/JsonSerializer.hpp:135`, forwarding to `serialize.hpp:20-21`). An rvalue does not bind to
it, so this does not compile.

**Fix.** Use a named local: `JsonFileWriter sink(buffered); serializeJson(doc, sink);`. The spec
leaves naming to the plan, so a one-word note is enough.

### MINOR-5 — A-10 rewrites a function no test exercises, on a weak flash rationale

**Claim** (A-10, lines 137-143): stream `writeDocToFile` too, because keeping its String "keeps the
String serializer instantiated in this TU for dead code".

**Problem.** Its only caller, `saveToFile()`, is a template member no firmware store instantiates
(`grep -rnE "writeDocToFile\(|saveToFile\(\)" src lib test`). So `writeDocToFile` is unreferenced,
and section GC removes it from the image whatever its body. The flash argument does not hold.

The test side is also thin. The only tests that reach `saveToFile()` expect it to refuse before
writing (`PersistableStoreTest.cpp:72,127`), so the rewritten body — `openFileForWrite` without
the remove, then the A-4 checks — has no host coverage. TDD step 5 claims regression coverage for
A-10 that does not exist.

**Fix.** Either add one `AtomicWriteTest`-style case asserting `writeDocToFile`'s byte-exact
output and its short-write failure, or leave `writeDocToFile` alone apart from the mkdir. Either
way, drop the flash rationale.

## Summary

No BLOCKER. The design's core choices hold up against the code and are correctly motivated:

- reusing `BufferedFileWriter` over a duck-typed adaptor;
- a 512-byte internal-SRAM buffer;
- keeping the adaptors inside `PersistableStore.cpp`;
- closing before the rename;
- deriving the parent directory from the path.

The three MAJORs are fixable inline without changing scope or reversing a goal:

- **MAJOR-1** is a one-line CMake addition.
- **MAJOR-2** removes a check that cannot fire and states which checks carry the integrity
  guarantee.
- **MAJOR-3** replaces an unreliable device measurement with the scoped ESP-IDF low-water API.

BLOCKERS: 0, MAJORS: 3, MINORS: 5.

VERDICT: CLEAR
