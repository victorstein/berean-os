# An in-memory HalStorage fake, and the first host tests of store I/O

Issue #99. Research: `docs/superpowers/research/2026-09-26-issue-99-research.md`
(cited below as "research §N"). Every repo claim below cites a `file:line` that was
read for this spec.

**Modelled on:**

- **`test/pagination/GfxRendererFake.cpp`** (see its header, `:1-20`). A real header
  whose methods have no virtual seam gets its bodies from a test-only `.cpp`, so a
  call the fake doesn't define fails the link loudly instead of diverging silently.
- **`test/stubs/HalDisplay.h:1-14`**. A stub header shadows the real one on the
  include path. Host targets never put `lib/hal` on the path, so there is no
  include-order hazard.

## What changed after the pass-0 review, and why

Review: `docs/superpowers/reviews/issue-99-spec-review-0.md`. All four findings are
accepted.

1. **BLOCKER 1, `String` could not be parsed from.** A-11 cited
   `StringObject.hpp`, which is the adapter for JSON keys and values, not the
   `deserializeJson` input path. For a class type, input goes through
   `Reader<TSource, void_t<typename TSource::const_iterator>>`, which needs
   `const_iterator` plus `begin()`/`end()`
   (`Deserialization/Readers/IteratorReader.hpp:33-39`), or through a default
   `Reader` that calls `read()`. The stub `String` now has `const_iterator`,
   `begin()` and `end()`, and A-11 cites the right file. The reviewer compiled the
   real `PersistableStore.cpp` against the corrected class with no errors or
   warnings. The research note's §3.1 carried the same error and is corrected in
   the same commit.
2. **MAJOR 2, sticky hooks versus the chained test.** A-10 had failure hooks
   cleared only by `reset()`, which also wipes the card, so the atomic-write test
   could never let the follow-up adopting read promote the `.tmp`. `storage_fake`
   gains `clearFailures()`, which drops every hook and keeps files and directories.
   A-10 and the controls block say so, and the chained test calls it between the
   failed write and the adopting read.
3. **MINOR 3, comments this change makes false.** The new stub `HalStorage.h`
   includes the stub `Arduino.h`, so the pagination suite now reaches it too. That
   is harmless because nothing there names `String`, `millis` or `micros`. The
   "none of them includes `<Arduino.h>`" paragraph is rewritten. Three
   comment-only edits are added to Files touched: `test/stubs/Arduino.h:3-7`,
   `test/pagination/CMakeLists.txt:8-10,26` and `TempAdoptionTest.cpp:3-7`. The
   per-store CMakeLists comments (`highlight_file`, `credential_integrity`,
   `bookmark_save_action`) are left alone. They are still true, because those
   stores' `.cpp` files don't get host suites in this change.
4. **MINOR 4, mechanical gaps.**
   - The defined list now includes `HalStorage::HalStorage()` and the definition of
     `HalStorage HalStorage::instance`, which the inline `getInstance()`
     (`lib/hal/HalStorage.h:48,53`) needs.
   - A-2 now says `override` is dropped from `write(const uint8_t*, size_t)` and
     `write(uint8_t)` (`:88,90`) along with the `Print` base.

---

## Problem

`test/stubs/HalStorage.h:15-22` declares a bare `HalFile` with five methods. It has
no `HalStorage` class and no `Storage` macro. The only bodies for those methods are
no-ops (`test/pagination/GfxRendererFake.cpp:127-131`). As a result, no host test
can compile any code that says `Storage.`.

`test/temp_adoption/TempAdoptionTest.cpp:3-7` states the limitation outright, and
`test/highlight_file/CMakeLists.txt:1-4`, `test/credential_integrity/CMakeLists.txt:12-13`
and `test/bookmark_save_action/CMakeLists.txt:2` repeat it.

What's covered today is the *decisions*: `tempAdoptionAction` and
`adoptedReadStatus` (`lib/Serialization/TempAdoption.h`), `classifyDocRead`
(`DocReadStatus.h:18-20`) and `fitsBudget` (`SaveBudget.h`). What isn't covered is
the code that *acts* on those decisions against the card:

- `writeDocToFileAtomic` (`lib/Serialization/PersistableStore.cpp:22-44`)
- `readDocFromFileChecked` (`:46-61`)
- `readDocFromFileAdopting` (`:63-106`)
- the five per-store loaders

The untested layer is where the five loaders drifted apart on `.tmp` handling
(#98). All five `Storage.remove(tmp)` on `DeleteTempReportEmpty`
(`TagPaletteFile.cpp:55-57`, `ChapterCompletionFile.cpp:56`, `PassageFile.cpp:92`,
`BookmarkFile.cpp:79`, `HighlightFile.cpp:64`), while the shared helper
deliberately keeps it (`PersistableStore.cpp:96-100`).

Beyond the missing `Storage`, three more things stop `PersistableStore.cpp` from
building on the host (research §3):

1. **No `String`.** `PersistableStore.cpp:13,27,50` uses an Arduino `String`, and
   `test/stubs/Arduino.h:9-12` defines only `millis()` and `micros()`.
2. **`obfuscation::deobfuscateFromBase64` has no host body.**
   `PersistableStore.cpp:122` calls it, and `ObfuscationUtils.cpp` needs
   `<esp_mac.h>` and `<mbedtls/base64.h>`.
3. **The stub's `HalFile` signatures differ from the real ones.** `read` returns
   `size_t` in the stub (`:17`) but `int` in the real header (`lib/hal/HalStorage.h:86`),
   and `position()` is non-`const` in the stub (`:20`) but `const` in the real one
   (`:85`).

## Goal

1. An in-memory `HalStorage` fake that `PersistableStore.cpp` and the per-store
   loaders compile and link against unmodified. It is a path→bytes map that follows
   the SD card's real semantics, with hooks to inject a failed read, a failed
   rename and a failed write.
2. A first round of host tests covering:
   - the atomic write
   - `.tmp` adoption through the shared helper
   - one store's (`TagPaletteFile`) load/save round trip
3. A fake general enough for #98's `loadAdopting(path, reader, fromJson)` helper
   tests, which need both the whole-file `readFile` path and the streamed
   `openFileForRead` + `HalFile::read` path (`PassageFile.cpp:31-47`).

## Non-goals

- **No production code changes.** Every file under `lib/` and `src/` stays
  byte-identical. If a test exposes a production defect, it is reported as a
  follow-up issue and not fixed here (A-19).
- **No fix for #98's policy split.** No test in this change asserts what a
  per-store loader does on `DeleteTempReportEmpty`. #98 owns that arm and would
  otherwise have to reverse our assertion (A-17).
- **No tests for** `MigrationRunner`, `UnitIndexCache`, `StudyStore.cpp`,
  `BibleSearchStore` or `CatalogIndexStore`. The issue asks for "a first round".
  The fake's declared-but-undefined surface (A-2) makes it cheap to extend later.
- **No ASan/UBSan CI job** (A-18).
- **No concurrency model.** The fake has no `storageMutex` (A-10).

---

## Assumptions, for the review to attack

| | Assumption | Decided in |
|---|---|---|
| **A-1** | The fake **replaces** `test/stubs/HalStorage.h` in place, not beside it. Same name, same include-path shadowing as `test/stubs/HalDisplay.h:1-14`. The pagination suite (the only current consumer, `test/pagination/CMakeLists.txt:26`) moves onto it in the same change. | §Architecture |
| **A-2** | The fake header declares the real public API of `HalStorage` and `HalFile` with **the real signatures** (`lib/hal/HalStorage.h:13-98`), minus three items: `readFileToStream` (takes `Print&`), `HalFile`'s `Print` base, and `class StorageLock`. With the base gone, `override` is dropped from `write(const uint8_t*, size_t)` and `write(uint8_t)` (`HalStorage.h:88,90`); those are the only signature edits. The fake `.cpp` defines only the subset this change and #98 need. Any other declared method fails the **link** when it is first used, which is `GfxRendererFake.cpp:7-10`'s "fails loudly" property. | §Architecture |
| **A-3** | Test controls live in a **separate** header, `test/stubs/HalStorageFake.h` (namespace `storage_fake`), so the `HalStorage` class in the stub stays a mirror of the real one and carries no test-only members. | §Architecture |
| **A-4** | `readFile` returns at most **50,000** bytes, truncating silently, and returns `""` for a missing or failing file, mirroring `SDCardManager.cpp:190-210` (`maxSize` at `:202`). | §Semantics |
| **A-5** | `writeFile` **removes an existing destination first**, then writes, and returns true only on a full write, mirroring `SDCardManager.cpp:282-293`. | §Semantics |
| **A-6** | `rename` **fails if the destination exists**, the source is missing, or the destination's parent directory is missing, mirroring SdFat 2.3.1's `O_CREAT \| O_EXCL` open (`FatFile.cpp:974`, research §1). `mkdir` returns **false if the path already exists**, the same `O_EXCL` (`FatFile.cpp:379`). With `pFlag` (the default, `HalStorage.h:34`) it creates missing parents. | §Semantics |
| **A-7** | Writes (`writeFile`, `openFileForWrite`, a `rename` destination) **require the parent directory to exist**, as on SdFat. `storage_fake::putFile` (test seeding) creates parents implicitly, so a test can stage a card state without going through `mkdir`. | §Semantics |
| **A-8** | **Injected read failure:** `exists` stays true, `readFile` returns `""`, and `openFileForRead` returns false. This is the case `PersistableStore.cpp:96-100` calls "indistinguishable from an empty file". | §Error handling |
| **A-9** | **Injected rename failure:** `rename` returns false and changes nothing. **Injected write failure:** `writeFile` removes an existing destination, then returns false and creates nothing, which is what `SDCardManager.cpp:282-291` does when the open fails after the remove. The write hook goes beyond the issue's two hooks; it is justified by the atomic write's first failure branch (`PersistableStore.cpp:30-33`), which can't be reached without it. | §Error handling |
| **A-10** | Failure hooks are **sticky per path** until cleared, not one-shot. `storage_fake::clearFailures()` drops every hook and keeps the card; `storage_fake::reset()` drops hooks *and* empties the card. The fake is single-threaded, holds no mutex, and doesn't model `storageMutex` (`HalStorage.cpp:38-40`). | §Error handling |
| **A-11** | `String` is added to **`test/stubs/Arduino.h`**. It is a `std::string`-backed class. For `deserializeJson` input, it has `const_iterator`, `begin()` and `end()`, which is what ArduinoJson 7.4.2's `IteratorReader` specialisation requires (`Deserialization/Readers/IteratorReader.hpp:33-39`). For `serializeJson` output, it has `write(uint8_t)` and `write(const uint8_t*, size_t)`, which the default `Writer` calls (`Writer.hpp:11-26`). It also has `c_str()`, `length()` and `isEmpty()` for `PersistableStore.cpp:51` and the fake's own `readFile`/`writeFile`. It deliberately does **not** inherit from `std::string` (research §3: `is_std_string` would reject it). | §Architecture |
| **A-12** | `obfuscation::deobfuscateFromBase64(const char*, size_t, bool*, bool*)`, the only obfuscation symbol `PersistableStore.cpp` references (`:122`), gets a link-time body in `test/stubs/ObfuscationUtilsStub.cpp`. It reports `ok = false`, `tooLong = false` and returns `""`. No test in this change calls it. | §Architecture |
| **A-13** | Shared fake sources are listed per suite in `add_executable`, the way `test/pagination/CMakeLists.txt:11-23` lists `GfxRendererFake.cpp` alongside real `lib/` sources. **No new CMake library target.** | §Architecture |
| **A-14** | One new suite, **`test/storage_io/`**, with one executable (`StorageIoTest`) and four test files. That means **one** `add_subdirectory(storage_io)` line in the shared `test/CMakeLists.txt`, reported in the PR and not committed (`.claude/agents/data-dev.md`, "Shared files"). | §Testing |
| **A-15** | The pagination suite links `test/stubs/HalStorageFake.cpp` and drops its five `HalFile` no-op bodies (`GfxRendererFake.cpp:125-131`). The real `int read(void*, size_t)` makes `TextBlock.cpp:393`'s `!= size` a signed/unsigned comparison warning on the host. The firmware compiles the same expression against the same signature, and the host build has no `-Werror` (`test/CMakeLists.txt:42-46`), so it is **accepted, not suppressed**. | §Architecture |
| **A-16** | `TagPaletteFile` is the round-trip store: a fixed path (`TagPaletteFile.h:25`), and its only dependency, `TagPalette.cpp`, already host-builds (`test/tag_palette/CMakeLists.txt`). | §Testing |
| **A-17** | Adoption is tested **through the shared helper** `readDocFromFileAdopting`, asserting its documented keep-the-`.tmp` policy. The `TagPaletteFile` tests cover only the arms #98 won't change: Loaded, Empty, RecoveredFromTemp, TooLarge and Failed. | §Testing |
| **A-18** | The ASan/UBSan CI job (the issue's "also worth doing") is **out of scope**. It edits `.github/workflows/ci.yml`, which is outside the `data` surface (`.claude/agents/data-dev.md`), and the issue marks it optional. The PR names it as a follow-up. Local ASan/UBSan works (research §4), so the implement phase runs the new suite under it once, by hand, as extra evidence. | §Testing |
| **A-19** | A production defect found by these tests is recorded in the PR as a follow-up issue. It is not fixed here, and a test that would fail on it is not committed red. | §Error handling |

---

## Architecture

### Files touched

| File | Change |
|---|---|
| `test/stubs/HalStorage.h` | Rewritten: full real-signature `HalStorage` + `HalFile` declarations, `Storage` macro (A-1, A-2) |
| `test/stubs/HalStorageFake.h` | New: `storage_fake` test controls (A-3) |
| `test/stubs/HalStorageFake.cpp` | New: the in-memory implementation |
| `test/stubs/Arduino.h` | Adds `String` (A-11); header comment `:3-7` rewritten, since it now reaches pagination through `HalStorage.h` |
| `test/stubs/ObfuscationUtilsStub.cpp` | New: one link-time body (A-12) |
| `test/pagination/GfxRendererFake.cpp` | Drops `:125-131` (A-15) |
| `test/pagination/CMakeLists.txt` | Adds `${REPO_ROOT}/test/stubs/HalStorageFake.cpp` to sources (A-15); comments `:8-10` and `:26` corrected (the stub `Arduino.h` now reaches it; `HalStorage.h` is no longer a no-op) |
| `test/temp_adoption/TempAdoptionTest.cpp` | Comment-only: `:3-7` now points at `test/storage_io/` for the Storage call sequence, instead of saying it is device-verified only |
| `test/storage_io/CMakeLists.txt` + four `*Test.cpp` | New suite (A-14) |
| `test/CMakeLists.txt` | **Not edited.** One line reported in the PR |

No file under `lib/` or `src/` changes.

### The stub header (A-1, A-2)

`test/stubs/HalStorage.h` keeps the real header's shape (`lib/hal/HalStorage.h:13-59`):

- `class HalStorage` with `static HalStorage& getInstance()`, a private
  `static HalStorage instance`, and `#define Storage HalStorage::getInstance()`
- `class HalFile`, move-only with `std::unique_ptr<Impl>` (`:61-98`)

It includes `<Arduino.h>` (the stub, for `String`) and `<fcntl.h>` for `O_RDONLY`,
and declares `using oflag_t = int;`. That typedef matches SdFat's own
`FsApiConstants.h:43` on its fcntl path, so `open(path, oflag)` is declared with the
real default. Nothing calls it in this change, so it stays undefined (A-2).

The header does **not** include `<Print.h>` or `<freertos/semphr.h>`: `HalFile`
doesn't derive from `Print`, and `readFileToStream` and `storageMutex` are omitted.
A future caller that needs `HalFile` as a `Print` fails to compile, which is loud.

### The implementation (`HalStorageFake.cpp`)

State is one file-local struct:

```cpp
struct FakeCard {
  std::map<std::string, std::string> files;  // path -> bytes
  std::set<std::string> dirs;                 // always contains "/"
  std::set<std::string> failRead, failRename, failWrite;
};
```

`HalFile::Impl` holds the path, a position, and whether it was opened for write. It
does **not** cache bytes. Every `read`/`write`/`size` looks the path up in
`FakeCard::files` at call time, so a file removed while a handle is open reads as
empty rather than dangling. Writes land in the map immediately, which matches
SdFat's write-through behaviour for this purpose. Because
`DESTRUCTOR_CLOSES_FILE=1`, the destructor just drops the `Impl`.

**Defined `HalStorage` members** (the subset A-2 promises):

- `HalStorage::HalStorage()` and the definition of `HalStorage HalStorage::instance`,
  which the inline `getInstance()` returns (`lib/hal/HalStorage.h:48,53`)
- `exists`, `remove`, `rename`, `mkdir`, `ensureDirectoryExists`
- `readFile`, `writeFile`
- the three `openFileForRead` overloads and the three `openFileForWrite` overloads
  (`HalStorage.h:40-45`)

**Defined `HalFile` methods:**

- default/move constructors and assignment, destructor
- `read(void*, size_t)` → `int`, `read()` → `int` (`-1` at EOF)
- `write(const uint8_t*, size_t)`, `write(const void*, size_t)`, `write(uint8_t)`
- `seek`, `position() const`, `size`, `fileSize`, `available() const`
- `close`, `isOpen`, `operator bool`, `flush`

That covers `PersistableStore.cpp`, `TagPaletteFile.cpp`, `PassageFile.cpp`'s
`HalFileReader` (`:20-24`) and `TextBlock.cpp`'s `read`/`write`/`seek`/`position`/`size`.

### The test controls (`HalStorageFake.h`, A-3)

```cpp
namespace storage_fake {
void reset();                                          // empty card: only "/" exists, no hooks
void clearFailures();                                  // drops every hook, keeps files and dirs
void putFile(const std::string& path, std::string bytes);  // seeds; creates parent dirs
std::optional<std::string> fileBytes(const std::string& path);
bool isDir(const std::string& path);
void failReadsOf(const std::string& path);
void failRenamesFrom(const std::string& path);
void failWritesTo(const std::string& path);
}
```

Each test fixture calls `reset()` in `SetUp()`. The fake is a process-wide
singleton, the same as `Storage` on the device.

### `String` (A-11)

This gets appended to `test/stubs/Arduino.h`:

```cpp
class String {
 public:
  String() = default;
  String(const char* s) : s_(s ? s : "") {}
  String(std::string s) : s_(std::move(s)) {}
  const char* c_str() const { return s_.c_str(); }
  size_t length() const { return s_.size(); }
  bool isEmpty() const { return s_.empty(); }
  size_t write(uint8_t c) { s_.push_back(static_cast<char>(c)); return 1; }
  size_t write(const uint8_t* p, size_t n) { s_.append(reinterpret_cast<const char*>(p), n); return n; }
  String& operator+=(char c) { s_.push_back(c); return *this; }
  using const_iterator = std::string::const_iterator;
  const_iterator begin() const { return s_.begin(); }
  const_iterator end() const { return s_.end(); }
 private:
  std::string s_;
};
```

`serializeJson(doc, json)` (`PersistableStore.cpp:28`) appends through
ArduinoJson's default `Writer`, which **does not clear the destination first**
(`Writer.hpp:11-26`, unlike `StdStringWriter.hpp:24-26`). Every call site in
`PersistableStore.cpp` serialises into a freshly default-constructed `String`
(`:13`, `:27`), so appending is equivalent. **This is recorded, not relied on
silently:** the first `AtomicWrite` test asserts the exact bytes written.

Two suites now reach the stub `Arduino.h`:

- `font_page_slots`, directly (`test/stubs/Arduino.h:3-7`)
- `pagination`, newly, through the stub `HalStorage.h`, which includes it for
  `String`

Neither names `String`, and pagination names neither `millis` nor `micros` (pass-0
review, finding 3: grep over `test/pagination/*.cpp` and its compiled sources, 0
hits). So the addition is inert for both. The stale comments this makes false are
corrected (Files touched).

---

## Semantics, and where each comes from

| Operation | Fake behaviour | Source it mirrors |
|---|---|---|
| `exists(p)` | true for a file or a directory | `SDCardManager.h:58` → SdFat `exists` |
| `remove(p)` | erases a file; false if absent | `SDCardManager.h:59` |
| `rename(a, b)` | false if `a` is absent, `b` exists, or `b`'s parent dir is missing; otherwise moves the bytes | `SDCardManager.h:61`, `FatFile.cpp:974` (A-6) |
| `mkdir(p, pFlag=true)` | false if `p` exists; creates missing parents when `pFlag` | `SDCardManager.h:57`, `FatFile.cpp:379` (A-6) |
| `readFile(p)` | `""` if absent or read-failed; else the first ≤50,000 bytes | `SDCardManager.cpp:190-210` (A-4) |
| `writeFile(p, s)` | removes `p` if present; false if the parent is missing or write-failed; else stores `s` | `SDCardManager.cpp:276-293` (A-5, A-7) |
| `openFileForRead(m, p, f)` | false if absent or read-failed; else a read handle at position 0 | `SDCardManager.cpp:320` (`O_RDONLY`) |
| `openFileForWrite(m, p, f)` | false if the parent is missing or write-failed; else truncates or creates | `SDCardManager.cpp:337` (`O_RDWR \| O_CREAT \| O_TRUNC`) |

---

## Data and control flow

### Atomic write under the fake (`PersistableStore.cpp:22-44`)

```
mkdir("/.crosspoint")              -> dir created, or false if it exists (ignored, :23)
serializeJson(doc, String)         -> bytes
writeFile(path.tmp, bytes)         -> failWritesTo(path.tmp): false  => return false, primary untouched
remove(path)                       -> result ignored (:38)
rename(path.tmp, path)             -> failRenamesFrom(path.tmp): false => return false,
                                      primary GONE, .tmp holds the new bytes
```

The rename-failure outcome is exactly the state `readDocFromFileAdopting` exists to
recover. One test chains the two to prove it. It calls `storage_fake::clearFailures()`
between the failed write and the adopting read (A-10), because otherwise the
promotion would hit the same rename hook.

### Adopting read under the fake (`PersistableStore.cpp:63-106`)

```
readDocFromFileChecked(path)
  exists? no                  -> Missing
  readFile == ""              -> Unreadable     (empty file OR failReadsOf)
  parse error                 -> ParseError     (includes a >50,000-byte file truncated by A-4)
  else                        -> Ok
if Missing: exists(tmp)? parse(tmp)?
tempAdoptionAction(...)       (TempAdoption.h)
  PromoteTempAndUseIt   -> rename(tmp, path); a failure only logs, and doc is still Ok
  DeleteTempReportEmpty -> doc.clear(); .tmp LEFT on the card (:96-100)
adoptedReadStatus(...)        (TempAdoption.h:48-60)
```

### TagPaletteFile (`src/study/TagPaletteFile.cpp:21-76`)

`save` runs a budget check (`:69`), then `mkdir("/.berean")` (`:74`), then
`writeDocToFileAtomic`. `load` hand-rolls the switch above (`:38-61`).

---

## Error handling

The fake never aborts and never throws. Every failure is a `false` or `""`, just
like the SD path. It is a test fixture, so it doesn't log.

Hooks (A-8, A-9, A-10):

| Hook | `exists` | `readFile` | `openFileForRead` | `writeFile` | `openFileForWrite` | `rename` |
|---|---|---|---|---|---|---|
| `failReadsOf(p)` | unchanged | `""` | false | — | — | — |
| `failWritesTo(p)` | — | — | — | removes `p` if present, then false | false | — |
| `failRenamesFrom(p)` | — | — | — | — | — | false from `p`, no state change |

A defect found by a test is reported rather than fixed (A-19).

---

## Testing strategy

### TDD order

The suite is written first against the new `HalStorage.h` and `HalStorageFake.h`
declarations, so it fails to link. Then `HalStorageFake.cpp` is written until it
passes. The first red is a link failure, not a runtime one, which is expected for a
link-time fake (compare `test/pagination/CMakeLists.txt:3-6`).

Locally, `add_subdirectory(storage_io)` is added to `test/CMakeLists.txt` **in the
working tree only** and never staged (A-14). CI won't build the suite until the
orchestrator applies the reported line.

### `test/storage_io/` — `StorageIoTest`

Sources:

- the four test files
- `test/stubs/HalStorageFake.cpp` and `test/stubs/ObfuscationUtilsStub.cpp`
- `lib/Serialization/PersistableStore.cpp`
- `src/study/TagPaletteFile.cpp`
- `lib/StudyStore/StudyStore/TagPalette.cpp`

Include path: `test/stubs` **first**, then `lib/Serialization`, `lib/StudyStore`
and `src/study` (for `TagPaletteFile.h`). Links `crosspoint_test_common`,
`ArduinoJson` and `GTest::gtest_main`, following `test/tag_palette/CMakeLists.txt`.
All fixtures are synthetic JSON, because this is a public repository.

**`HalStorageFakeTest.cpp`** pins the fake to the SD semantics, so a later
"simplification" of the fake that breaks a real-card property fails here first:

- rename onto an existing destination fails and leaves both files intact (A-6)
- rename into a missing directory fails (A-6, A-7)
- `mkdir` on an existing directory returns false; `mkdir` with parents creates the
  chain (A-6)
- `writeFile` into a missing directory fails (A-7)
- `readFile` of 50,001 bytes returns exactly 50,000 (A-4)
- `failReadsOf`: `exists` true, `readFile` `""`, `openFileForRead` false (A-8)
- `failWritesTo` on an existing file: the file is gone afterwards and the call
  returned false (A-9)
- a streamed read through `openFileForRead` + `read(buf,n)` + `read()` returns the
  bytes and then `-1` at EOF (for #98's reader path)
- `reset()` clears files, directories and hooks (A-10)
- `clearFailures()` drops hooks but keeps files and directories (A-10)

**`AtomicWriteTest.cpp`** (`writeDocToFileAtomic`):

- a fresh write leaves the exact serialised bytes at `path`, no `path.tmp`, and
  `/.crosspoint` existing
- overwriting an existing primary replaces its bytes
- a stale `path.tmp` from an earlier crash doesn't block the write (A-5)
- `failWritesTo(path.tmp)` returns false and leaves the old primary byte-identical
- `failRenamesFrom(path.tmp)` returns false, leaves the primary absent and `.tmp`
  holding the new bytes. After `storage_fake::clearFailures()`, a following
  `readDocFromFileAdopting` returns Ok with the new document and promotes it: the
  primary holds the new bytes and `.tmp` is gone.

**`AdoptingReadTest.cpp`** (`readDocFromFileAdopting`, `readDocFromFileChecked`):

- primary Ok and a parseable `.tmp` present: Ok from the primary, `.tmp` untouched
- neither file present: Missing
- primary absent and `.tmp` parseable: Ok, the primary now holds the `.tmp`'s bytes,
  `.tmp` gone
- primary absent, `.tmp` parseable, rename fails: Ok, the document is loaded, `.tmp`
  still present, primary still absent
- primary absent and `.tmp` garbage: Missing, `doc` empty, **`.tmp` still on the
  card** (`PersistableStore.cpp:96-100`; this is the assertion #98 wants for its
  helper)
- primary present but read-failed: Unreadable, `.tmp` never consulted
- empty primary file: Unreadable
- primary garbage: ParseError, `.tmp` never promoted
- primary of more than 50,000 bytes of valid JSON: ParseError. This documents the
  truncation chain `CLAUDE.md` warns about. A 45,000-byte document
  (`SaveBudget.h:23`) reads Ok.

**`TagPaletteFileTest.cpp`** (A-16, A-17):

- `save` then `load` round-trips the tag names and ids: Ok, then Loaded
- `load` on an empty card returns Empty
- primary absent with a valid `tags.json.tmp`: RecoveredFromTemp, and the primary
  is now in place
- an over-budget palette: `save` returns TooLarge and writes nothing, so neither
  `tags.json` nor `.tmp` exists
- `failReadsOf(tags.json)`: `load` returns Failed and the file is unchanged
- `failWritesTo(tags.json.tmp)`: `save` returns WriteFailed and the old `tags.json`
  is byte-identical

The `DeleteTempReportEmpty` arm is deliberately untested here (A-17, #98).

### Existing suites

`PaginationInvarianceTest` must still pass on the new stub (A-15). The whole suite,
851 tests at baseline (research §4), plus the new ones, must pass under the
Release configuration CI uses (`.github/workflows/ci.yml:188-194`).

### Sanitizers (A-18)

As a one-off, the implement phase configures a second local tree with
`-DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer"` and runs
`StorageIoTest` and `PaginationInvarianceTest` under it. The result goes in the
PR; there is no CI change.

### Gates

- `./bin/clang-format-fix` over the full tree. New files have to be `git add`ed
  first, because the wrapper skips untracked files.
- No `pio run` is needed: no firmware source changes. The PR says so explicitly.

### What only the human tester can verify

Nothing new. This change adds no firmware behaviour. The fake's fidelity to a real
card is argued from source (§Semantics), not measured on hardware. A-6's `mkdir`
return value and A-4's cap are the two points worth a sanity check if a device
session happens anyway.

---

## Shared-file report (for the PR description)

1. **`test/CMakeLists.txt`**, one line, placed after `add_subdirectory(temp_adoption)`
   (`test/CMakeLists.txt:102`):

       add_subdirectory(storage_io)

2. **Follow-up:** an ASan/UBSan configuration of the host suite in
   `.github/workflows/ci.yml` (issue #99's "also worth doing", A-18).
3. **For #98:** link `test/stubs/HalStorageFake.cpp` and
   `test/stubs/ObfuscationUtilsStub.cpp` into the `loadAdopting` suite. Control the
   card through `storage_fake::` in `test/stubs/HalStorageFake.h`.
