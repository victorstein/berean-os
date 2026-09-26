# Issue #99 research: an in-memory HalStorage fake for host tests

Branch `fix/99-halstorage-test-fake`, base `2f303f6f` (release 1.16.0). Every claim
below was checked by reading the file or running the command it cites.

## 1. What owns the behaviour

### The stub today

`test/stubs/HalStorage.h` is 22 lines. It declares a bare `HalFile` with five
methods, has no `HalStorage` class, and defines no `Storage` macro
(`test/stubs/HalStorage.h:15-22`). The only bodies for those methods are no-ops in
`test/pagination/GfxRendererFake.cpp:127-131`.

Exactly one suite puts `test/stubs` in front of `HalStorage.h`:
`PaginationInvarianceTest` (`test/pagination/CMakeLists.txt:26`). It needs `HalFile`
only because `TextBlock.cpp`'s serialize/deserialize call it
(`test/stubs/HalStorage.h:7-10`). `minibidi_arabic`, `bible_search_index`,
`bible_search_scanner` and `font_page_slots` also put `test/stubs` on their include
path, but only for `Logging.h` and `Arduino.h` (the comments on their CMakeLists
lines, found with `grep -rn stubs test --include=CMakeLists.txt`).

The stub's `HalFile` signatures already differ from the real ones:

| | stub (`test/stubs/HalStorage.h`) | real (`lib/hal/HalStorage.h`) |
|---|---|---|
| `read(void*, size_t)` | returns `size_t` (:17) | returns `int` (:86) |
| `position()` | non-const (:20) | `const` (:85) |
| base class | none | `: public Print` (:61) |

A fake that `PassageFile.cpp`'s `HalFileReader` compiles against has to match the
real `int read(void*, size_t)` and `int read()` (`src/study/PassageFile.cpp:20-24`).
Changing the stub's signatures means changing `GfxRendererFake.cpp:127-131` in the
same change, or the pagination suite stops linking.

### The real surface the fake must mirror

`lib/hal/HalStorage.h:13-57` is the `HalStorage` class, and `:59` is
`#define Storage HalStorage::getInstance()`. Each method forwards to `SDCardManager`
under `storageMutex` (`lib/hal/HalStorage.cpp:38-40`, `HAL_STORAGE_WRAPPED_CALL`).
These are the semantics the fake has to reproduce, since the code under test
depends on them:

- **`readFile` caps at 50,000 bytes and truncates silently.** It returns `""` if
  the SD card isn't initialised or the file won't open
  (`freeink-sdk/libs/hardware/SDCardManager/src/SDCardManager.cpp:190-210`,
  `maxSize = 50000` at :202). An empty file and a failed read therefore look the
  same to the caller, and `readDocFromFileChecked` classifies both as `Unreadable`
  (`lib/Serialization/PersistableStore.cpp:50-53`).
- **`writeFile` removes an existing destination before re-creating it**
  (`SDCardManager.cpp:282-284`). It returns true only if every byte was written
  (`:293`).
- **`rename` refuses an existing destination.** `SDCardManager::rename` is
  `vol().rename(path, newPath)` (`SDCardManager.h:61`). SdFat opens the new entry
  with `O_CREAT | O_EXCL | O_WRONLY`
  (`.pio/libdeps/x4pro/SdFat/src/FatLib/FatFile.cpp:974`, SdFat 2.3.1, read from
  the sibling worktree `fix-63-honour-read-status`, which has built libdeps). The
  SDCardManager dependency is `"greiman/SdFat": "^2.3.1"`
  (`freeink-sdk/libs/hardware/SDCardManager/library.json`).
  `PersistableStore.cpp:35-38` depends on this, which is why it removes the file
  before the rename.
- `exists`, `remove` and `mkdir` pass straight through to SdFat
  (`SDCardManager.h:57-59`).

### Code under test that reaches `Storage`

This inventory came from grepping each `.cpp` for `Storage.`, `file.` and
`readDocFrom`/`writeDocTo`:

| File | Storage / HalFile calls used |
|---|---|
| `lib/Serialization/PersistableStore.cpp` | `mkdir`, `writeFile`, `remove`, `rename`, `exists`, `readFile` |
| `src/study/TagPaletteFile.cpp` | `exists`, `rename`, `remove`, `mkdir`, plus `readDocFromFileChecked` and `writeDocToFileAtomic` |
| `src/study/ChapterCompletionFile.cpp` | same set as TagPaletteFile |
| `src/util/HighlightFile.cpp`, `src/util/BookmarkFile.cpp` | same set, plus `openFileForRead` in BookmarkFile (:24) |
| `src/study/PassageFile.cpp` | `exists`, `openFileForRead`, `HalFile::size/read()/read(buf,n)` (streamed, :31-47), `rename`, `remove`, `mkdir` |
| `src/study/MigrationRunner.cpp` | `listFiles`, `mkdir`, `readDocFromFileAdopting` ×3, `writeDocToFileAtomic` ×2 |
| `src/study/UnitIndexCache.cpp` | `open` ×3, `openFileForRead` ×6, `openFileForWrite`, `mkdir`, `file.read/seek/size/write` |
| `src/study/BibleSearchStore.cpp` | `exists`, `openFileForRead`, `file.fileSize` |
| `src/network/CatalogIndexStore.cpp` | `exists`, `mkdir`, `openFileForRead`, `remove` ×6, `rename`, `file.fileSize/read` |
| `src/study/StudyStore.cpp` | none directly (0 matches) |

The issue asks for a *first round* of tests: the atomic write, `.tmp` adoption, and
one store's round trip. The minimum fake surface for that is `exists`, `remove`,
`rename`, `mkdir`, `readFile` and `writeFile`. #98's `loadAdopting(path, reader,
fromJson)` helper takes a reader, and `PassageFile` streams through
`openFileForRead` and `HalFile::read`, so the fake also needs `openFileForRead`
and a readable `HalFile` (`read()`, `read(buf,n)`, `size()`) to stay general enough
for #98. Stores that call `open(path, oflag)` need `oflag_t`/`O_RDONLY`, which the
real header gets from `<common/FsApiConstants.h>` (`lib/hal/HalStorage.h:4`).
Nothing in the first round calls it.

## 2. Current control flow

**Atomic write:** `writeDocToFileAtomic` (`PersistableStore.cpp:22-44`) runs these
steps in order:

1. `mkdir("/.crosspoint")`
2. `serializeJson` into an Arduino `String`
3. `writeFile("<path>.tmp")`; a failure returns false with the primary untouched
4. `remove(path)`, whose result is ignored
5. `rename(tmp → path)`; a failure returns false, the primary is already gone, and
   the `.tmp` is left behind

A host test can observe every one of these steps through a path→bytes map.

**Adopting read:** `readDocFromFileAdopting` (`PersistableStore.cpp:63-106`) works
like this:

- It calls `readDocFromFileChecked`, which maps `exists`/`readFile`/parse results
  to `DocReadStatus` via `classifyDocRead` (:46-61).
- Only when the primary is `Missing` does it probe `<path>.tmp`.
- `tempAdoptionAction` (`lib/Serialization/TempAdoption.h`) decides the outcome:
  - `PromoteTempAndUseIt` renames the `.tmp` into place. If the rename fails it
    logs, the document is still returned, and the `.tmp` stays.
  - `DeleteTempReportEmpty` clears the doc and deliberately **leaves** the `.tmp`
    (:89-101).

**Per-store loaders:** all five hand-roll that same switch and all five
`Storage.remove(tmp)` on `DeleteTempReportEmpty`:

- `TagPaletteFile.cpp:55-57`
- `ChapterCompletionFile.cpp:56`
- `PassageFile.cpp:92`
- `BookmarkFile.cpp:79`
- `HighlightFile.cpp:64`

That divergence is issue #98. #99's first-round tests must not encode either policy
for the per-store loaders in a way that #98 would then have to reverse. The
shared-helper tests should assert the shared helper's documented policy (keep the
`.tmp`).

**Budget:** `TagPaletteFile::save` checks `measureJson > persist::DEFAULT_SAVE_BUDGET`
before calling `mkdir("/.berean")` and `writeDocToFileAtomic`
(`TagPaletteFile.cpp:65-76`).

## 3. Why none of this builds on the host today

`test/temp_adoption/TempAdoptionTest.cpp:3-7` states it directly: "PersistableStore.cpp
cannot be built on the host: PersistableStore.h:3 includes <Arduino.h>
unconditionally, and test/stubs/HalStorage.h is a bare HalFile with no Storage
singleton." `test/highlight_file/CMakeLists.txt:1-4`,
`test/credential_integrity/CMakeLists.txt:12-13` and
`test/bookmark_save_action/CMakeLists.txt:2` repeat the same limitation.

Compiling and linking `PersistableStore.cpp` on the host needs four things:

1. **`String`.** `PersistableStore.cpp:13,27,50` uses Arduino `String` (`isEmpty()`,
   `serializeJson(doc, String&)`, `deserializeJson(doc, String)`), and `HalStorage.h`
   uses it in `readFile`, `writeFile` and `listFiles`. `test/stubs/Arduino.h:9-12`
   defines only `millis()` and `micros()`.
   - Off-Arduino, ArduinoJson 7.4.2 sets `ARDUINOJSON_ENABLE_ARDUINO_STRING 0`
     (`build/test/_deps/arduinojson-src/src/ArduinoJson/Configuration.hpp:177-178`).
   - A stub `String` still works as **input**: ArduinoJson adapts any type with
     `c_str()`/`data()` plus `length()`/`size()`
     (`Strings/Adapters/StringObject.hpp:14-17`).
   - As **output**, `is_std_string<T>` requires `T& append(const char*)` and `void
     push_back(char)` (`Serialization/Writers/StdStringWriter.hpp:15-19`). A class
     that inherits from `std::string` fails that test, because `append` returns
     `std::string&`, not `String&`. The fallback `Writer<T>` calls
     `dest.write(uint8_t)` and `dest.write(const uint8_t*, size_t)`
     (`Serialization/Writer.hpp:11-26`), so a stub `String` has to provide one of
     those two shapes.
2. **`ObfuscationUtils`.** `PersistableStore.cpp:5,122` calls
   `obfuscation::deobfuscateFromBase64`. `ObfuscationUtils.cpp` includes
   `<esp_mac.h>`, `<mbedtls/base64.h>` and `<base64.h>`, so the host can't build
   it. Linking `PersistableStore.cpp` therefore needs a link-time body for
   `deobfuscateFromBase64`, which the first-round tests never call.
3. **`Logging.h`.** Already stubbed as no-ops (`test/stubs/Logging.h:6-8`).
4. **`HalStorage.h` with a `Storage` singleton.** This is the fake the issue asks
   for.

## 4. Installed tool and package versions

| Tool | Version | Evidence |
|---|---|---|
| CMake (local) | 4.4.2 | `cmake --version` |
| Compiler (local) | Apple clang 21.0.0, arm64-apple-darwin25.5.0 | `c++ --version` |
| Ninja (local) | **not installed** | `cmake -G Ninja` → "unable to find a build program corresponding to Ninja". Configured with Makefiles instead. |
| GoogleTest | v1.17.0 | `test/CMakeLists.txt:17` |
| ArduinoJson (host) | v7.4.2 | `test/CMakeLists.txt:31`, pinned to match `platformio.ini:151` (`bblanchon/ArduinoJson @ 7.4.2`) |
| SdFat (firmware) | 2.3.1 | `library.properties` in built libdeps. Constraint `^2.3.1`. |
| freeink-sdk submodule | `310ec61` (2026-08-16) | `git -C freeink-sdk log -1` |
| CI host job | ubuntu-latest, apt `cmake ninja-build`, `-G Ninja -DCMAKE_BUILD_TYPE=Release`, no sanitizers | `.github/workflows/ci.yml:168-194` |

**Baseline:** `cmake -S test -B build/test -DCMAKE_BUILD_TYPE=Release && cmake
--build build/test -j8 && ctest --test-dir build/test -j8` gives "100% tests
passed out of 851". The first build succeeded, so the fresh-configure gtest race
didn't happen this time.

**Sanitizers:** a deliberate out-of-bounds read compiled with `c++
-fsanitize=address,undefined` was caught locally ("AddressSanitizer:
stack-buffer-overflow"), so ASan/UBSan works on this toolchain. There is no
`-fsanitize` anywhere in `test/CMakeLists.txt` or `.github/` (grep, 0 matches).

## 5. Nearest existing example

- **Link-time fake of a real header:**
  `test/pagination/GfxRendererFake.cpp:1-20`, with its CMake note at
  `test/pagination/CMakeLists.txt:1-10`. The real `GfxRenderer.h` compiles
  unmodified, and the fake supplies the bodies, so "if layout code starts calling
  a renderer method this file does not define, the test link fails loudly". The
  same seam fits here: `HalStorage` has no virtuals, and `Storage` is a macro over
  a static `getInstance()` (`lib/hal/HalStorage.h:48,53,59`).
  - The difference: `lib/hal/HalStorage.h` itself includes `<Print.h>`,
    `<freertos/semphr.h>` and `<common/FsApiConstants.h>` (:3-5). So unlike
    `GfxRenderer.h`, the real header can't be compiled unmodified without further
    stubs for those three. Whether to shadow the header, as the `test/stubs`
    pattern does, or stub its three includes is a design decision for the spec.
- **Header shadowing on the include path:** `test/stubs/HalDisplay.h:1-14`, which
  explains why a stub earlier on the include path is enough and carries no
  include-order hazard, because host targets never put `lib/hal` on the path.
- **A suite that builds real library `.cpp` files against ArduinoJson:**
  `test/tag_palette/CMakeLists.txt` builds `lib/StudyStore/StudyStore/TagPalette.cpp`
  and links `ArduinoJson` and `GTest::gtest_main`.
- **The decision-logic suites the new tests sit beside:** `test/temp_adoption/`,
  `test/save_budget/` and `test/doc_read_status/`.

## 6. Constraints on where the change lands

- `test/CMakeLists.txt` is a shared append point, so the data-dev guide says to
  report and not edit it (`.claude/agents/data-dev.md`, "Shared files"). A new
  suite needs one `add_subdirectory(...)` line there, and that line goes in the PR
  description for the orchestrator to apply.
- `test/stubs/HalStorage.h` is currently consumed only by the pagination suite.
  Any signature change has to keep `test/pagination` linking.
- `berean-os` is public, so fixtures must be synthetic JSON and not publisher text
  (project memory "Public repo test fixtures").
- The sanitizer job named in the issue's "also worth doing" touches
  `.github/workflows/ci.yml`. That is outside the `data` surface and optional for
  the issue.

## Open questions for the spec

1. Should the fake replace `test/stubs/HalStorage.h` (with the pagination no-op
   bodies migrating into a shared fake `.cpp`), or sit beside it as a separate
   header opted into per suite?
2. Where should `String` come from: a `String` added to `test/stubs/Arduino.h`
   (which every stubs-path suite would see), or a separate header?
3. Is the ASan/UBSan CI job in scope for this PR or a follow-up?
