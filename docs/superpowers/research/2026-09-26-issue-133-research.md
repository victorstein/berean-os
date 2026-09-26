# Issue #133 — research

Issue #133 asks for host tests of `PersistableStore<T>::loadFromFile` in two cases: the primary
file is missing and `.tmp` holds either a future-version document or garbage. The change is
test-only. This note records how the code behaves today, what tools are installed, and what the
nearest existing tests look like. Every claim below was read or run in this worktree at `06e53ecf`.

## Files that own the behaviour

| Concern | Location |
|---|---|
| CRTP load: adopting read, then `fromJson`, then the refusal flag | `lib/Serialization/PersistableStore.h:214-234` |
| Save guard | `PersistableStore.h:152-157` (`saveBlockedByRefusedLoad`), checked first in `saveToFile` (`:183`) and `saveToFileAtomic` (`:201`) |
| `loadRefused` member, written only by `loadFromFile` | `PersistableStore.h:49-52` |
| Adopting read used by the CRTP path | `lib/Serialization/PersistableStore.cpp:101-113` (`readDocFromFileAdopting`) |
| Shared private adoption, also used by `loadAdopting` | `PersistableStore.cpp:70-97` (`readAdopting`); `loadAdopting` at `:115-130` |
| Refusal rule | `lib/Serialization/FormatVersion.h:21-32` (`loadRefusedAfter`): `Ok → !accepted` (`:23-24`), `Missing → false` (`:25-26`), `Unreadable`/`ParseError` → unchanged (`:27-30`) |
| Adoption decision and status mapping | `lib/Serialization/TempAdoption.h:35-48` (`tempAdoptionAction`), `:54-66` (`adoptedReadStatus`); the rationale for keeping an unusable `.tmp` is at `:26-31` |
| Atomic write | `PersistableStore.cpp:23-45`: write `<path>.tmp`, `remove(path)`, `rename(tmp, path)` |

The issue's line citations all match the current tree. The one exception is
`test/persistable_store/PersistableStoreTest.cpp:62-106`: the file's last test actually ends at
`:113`.

## Current control flow on the two cases

**`.tmp` holds a future-version document, primary missing.**

1. `loadFromFile` locks `storeMutex` and calls `readDocFromFileAdopting(T::getFilePath(), doc)`
   (`PersistableStore.h:218-221`).
2. `readAdopting` reads the primary with `readDocFromFileChecked`. `Storage.exists` is false, so the
   read returns `Missing` (`PersistableStore.cpp:48-49`, `:72`).
3. `.tmp` exists and parses as JSON, so `tempParsed` is true (`:77-81`). `tempAdoptionAction`
   returns `PromoteTempAndUseIt` (`TempAdoption.h:45-46`).
4. `.tmp` is renamed onto the primary path (`PersistableStore.cpp:87`). Promotion happens before
   any version check, so the rename does not depend on `fromJson`.
5. `adoptedReadStatus` maps `PromoteTempAndUseIt` to `Ok` (`TempAdoption.h:57-58`).
6. `fromJson` refuses the document because `v` is greater than `FORMAT_VERSION`. `ok` is false,
   and `loadRefusedAfter(Ok, false, _)` returns `true` (`FormatVersion.h:23-24`).
   `loadFromFile` returns false and leaves the in-memory value as it was.
7. The next `saveToFileAtomic()` stops at `saveBlockedByRefusedLoad()` (`PersistableStore.h:201`).
   It never writes `.tmp` or touches the primary.

**`.tmp` is garbage, primary missing.**

1–2. The primary read is `Missing`, as above.
3. `.tmp` exists, but `readDocFromFileChecked` on it returns `ParseError`
   (`PersistableStore.cpp:56-59`), so `tempParsed` is false. The action is
   `KeepTempReportEmpty` (`TempAdoption.h:46`).
4. Nothing is renamed or removed. The partly parsed document is cleared
   (`PersistableStore.cpp:104-111`). `adoptedReadStatus` returns `Missing` (`TempAdoption.h:60-61`).
5. `loadRefusedAfter(Missing, …)` returns `false` (`FormatVersion.h:25-26`). `fromJson` is not
   called, and `loadFromFile` returns false at `PersistableStore.h:224`.
6. A later `saveToFileAtomic()` is not blocked. `writeDocToFileAtomic` calls
   `Storage.writeFile(tmp)`, which the fake implements as erase-then-write
   (`test/stubs/HalStorageFake.cpp:128-133`, mirroring `SDCardManager.cpp:282-284` per the
   fake's header at `:6-7`). The garbage is replaced. `remove(primary)` finds nothing, and the
   rename to the primary path succeeds. After the save the primary holds the new document and
   `.tmp` is gone.

## Probe: both cases already pass

To confirm the flow above, I appended both cases to `PersistableStoreTest.cpp` temporarily, ran
them, and reverted the file with `git checkout` (`git status --short` was clean afterwards). The
probe asserted:

- **Future `.tmp`:**
  - `loadFromFile()` returns false.
  - The primary holds exactly `NEWER` (`{"v":2,"value":7}`) and `.tmp` is absent.
  - After `value = 42`, `saveToFileAtomic()` returns false.
  - The primary still holds `NEWER` and `.tmp` is still absent.
- **Garbage `.tmp`:**
  - `loadFromFile()` returns false.
  - `.tmp` still holds `not json` and the primary is absent.
  - After `value = 9`, `saveToFileAtomic()` returns true.
  - The primary holds `{"v":1,"value":9}` and `.tmp` is absent.

```
$ ./build/test/persistable_store/PersistableStoreTest --gtest_filter='*Probe*'
[       OK ] PersistableStoreGuard.ProbeFutureTmp (0 ms)
[       OK ] PersistableStoreGuard.ProbeGarbageTmp (0 ms)
[  PASSED  ] 2 tests.
```

The issue's stop condition ("if either case fails, raise a decision") is not triggered, and no
production change is needed. These are characterisation tests, so the TDD "red" step cannot come
from missing behaviour. The spec needs to say how red is shown instead. One option is a temporary,
uncommitted mutation, such as making `loadRefusedAfter`'s `Ok` arm return `false`, or making
`KeepTempReportEmpty` remove the `.tmp`. Each assertion should be seen to fail before it is trusted.

## Installed tools and packages

| Tool | Version | Evidence |
|---|---|---|
| CMake | 4.4.2 | `cmake --version` |
| Compiler | Apple clang 21.0.0 (clang-2100.0.123.102) | `c++ --version` |
| Build generator | GNU Make 3.81. **Ninja is not installed** | `ninja --version` → `command not found`. CI uses `-G Ninja` (`.github/workflows/ci.yml:188`), so locally configure without `-G` |
| googletest | v1.17.0 (FetchContent) | `test/CMakeLists.txt:15-17` |
| ArduinoJson | 7.4.2 in both the host tests and the firmware | `test/CMakeLists.txt:30-31`; `platformio.ini:151` |
| C++ standard (host tests) | C++20 | `test/CMakeLists.txt:4-5` |

Commands that were run and passed (the baseline is 5/5):

```
./bin/bootstrap
cmake -S test -B build/test -DCMAKE_BUILD_TYPE=Release
cmake --build build/test --target gtest -j8        # prebuild gtest to avoid the first-build race
cmake --build build/test --target PersistableStoreTest -j8
./build/test/persistable_store/PersistableStoreTest   # [  PASSED  ] 5 tests.
```

`build/test` is gitignored (`git check-ignore build/test`).

## Test harness already in place

- `test/persistable_store/CMakeLists.txt:3-8` already links the real `PersistableStore.cpp` with
  `test/stubs/HalStorageFake.cpp`. It puts `test/stubs` first on the include path (`:10-13`).
  **No change to `test/CMakeLists.txt` or to this CMakeLists is needed.** `add_subdirectory(persistable_store)`
  is at `test/CMakeLists.txt:127`.
- Fake controls: `test/stubs/HalStorageFake.h:13-39` provides `reset`, `putFile`, `fileBytes`
  (an `std::optional<std::string>`), and the failure hooks. The fake's `rename` refuses an
  existing target and needs the parent directory (`HalStorageFake.cpp:96-103`). `putFile`
  creates parent directories, so seeding `/.crosspoint/probe.json.tmp` alone is enough.
- The suite's fixture, `PersistableStoreGuard` (`PersistableStoreTest.cpp:50-58`), resets the
  card in `SetUp`. On the empty card it then calls `loadFromFile()`, which returns `Missing` and
  clears any refusal left on the singleton by an earlier test. It also zeroes `value`. The file
  already defines helpers the new cases need: `PATH`, `TMP_PATH`, `NEWER`, and `bytesOn()`, which
  returns `"<absent>"` for a missing file (`:39-46`).

## Nearest existing examples

- **Same suite and same idiom:**
  `PersistableStoreGuard.ARefusedLoadBlocksEverySaveAndLeavesTheFileUntouched`
  (`PersistableStoreTest.cpp:62-73`). It does a refused load from a present primary, sets a
  value, and checks that both saves return false. It then asserts the on-disk bytes with
  `bytesOn(PATH) == NEWER` and `bytesOn(TMP_PATH) == "<absent>"`. The future-`.tmp` case is this
  test with the seed moved from `PATH` to `TMP_PATH`.
- **Same suite, save-after-Missing:** `AMissingFileLiftsTheRefusal` (`:75-84`). It asserts the
  exact bytes a successful save writes. The garbage-`.tmp` case ends the same way.
- **The same adoption on the non-CRTP entry point:** `test/storage_io/AdoptingReadTest.cpp`.
  - `AParseableTempIsPromotedIntoPlace` (`:44-50`) and
    `AnUnparseableTempReportsMissingAndStaysOnTheCard` (`:61-67`) cover
    `readDocFromFileAdopting` directly.
  - `test/storage_io/LoadAdoptingTest.cpp` covers `loadAdopting`.
  - Neither suite runs through `PersistableStore<T>` or its `loadRefused` flag. That combination
    is the gap #133 names.

## Constraints for the later phases

- Change only `test/persistable_store/PersistableStoreTest.cpp`. That includes the file's header
  comment, which currently describes it as only #101's guard (`:1-3`).
- Leave the shared append points alone (`test/CMakeLists.txt`, per `.claude/agents/data-dev.md`).
- Before committing, run `./bin/clang-format-fix` over the whole tree (root `CLAUDE.md`,
  "Formatting").
- Test fixtures stay synthetic JSON, not publisher text (memory note
  `public-repo-test-fixtures-no-full-publisher-text`). The existing `NEWER` and `"not json"`
  already follow this.
