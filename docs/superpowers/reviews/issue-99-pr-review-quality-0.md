# Issue #99 — PR #121 code-quality review, pass 0

Scope: `gh pr diff 121` (base `ad8e75f9`, head `d509b1e2`). The code under review is
`test/stubs/{HalStorage.h,HalStorageFake.h,HalStorageFake.cpp,Arduino.h,ObfuscationUtilsStub.cpp}`,
`test/storage_io/*`, the pagination suite's switch onto the fake, and the comment-only edits in
`lib/`, `src/` and the older test suites.

Verification I ran myself: I configured a separate build directory with the uncommitted
`add_subdirectory(storage_io)` in place and built `StorageIoTest` and
`PaginationInvarianceTest`. The results were `[  PASSED  ] 36 tests.` and
`[  PASSED  ] 8 tests.`

## What holds up

- **It follows the existing patterns.** `HalFile` keeps the real header's pimpl shape:
  `class Impl` plus `std::unique_ptr<Impl>` (`test/stubs/HalStorage.h:62-65`, mirroring
  `lib/hal/HalStorage.h`). The `std::string` and `String` overloads of
  `openFileForRead` and `openFileForWrite` delegate to the `const char*` form, the same way
  `lib/hal/HalStorage.cpp:124-130` does (`HalStorageFake.cpp:155-176`). `storage_io/CMakeLists.txt`
  is modelled on `test/pagination`: unmodified sources plus link-time fakes, with `test/stubs` first
  on the include path. The PR also adds no second storage double. It replaces the pagination suite's
  inert `HalFile` bodies (`test/pagination/GfxRendererFake.cpp`, −8 lines) instead of adding a
  second set beside them.
- **Test controls are kept separate from the mirror.** `storage_fake::` lives in `HalStorageFake.h`,
  so `HalStorage.h` keeps only the real signatures. An undeclared method fails at link time instead
  of silently doing nothing. `HalStorage.h:11-13` documents this as a deliberate choice.
- **The fake is pinned by its own tests.** `HalStorageFakeTest.cpp` pins every SD rule the store
  tests depend on: rename and mkdir with `O_EXCL`, remove-before-write, the 50,000-byte read cap,
  and a failed read that looks empty. A later "simplification" of the fake therefore fails in one
  obvious place and not deep inside a store test.
- **The tests assert observable card state and statuses.** They check `fileBytes`, `exists` and
  `DocReadStatus` / `LoadResult` / `SaveResult`, not call sequences:
  - The chained test at `AtomicWriteTest.cpp:56-69` (a failed rename, then an adopting read that
    recovers) checks behaviour across the two functions.
  - The pair at `AdoptingReadTest.cpp:93-102` tests both sides of the 50,000-byte truncation chain.
  - Test names read as specifications, in the same style as `test/tag_palette/TagPaletteTest.cpp`.
    A fixture is used only where a per-test `reset()` is needed.
- **The comments explain why, not what.** Examples are the SD-rule preamble at
  `HalStorageFake.cpp:1-12` and the ArduinoJson-coupling note at `Arduino.h:10-13`. The corrected
  "cannot be host-built" comments are written for the merged state, with no before/after narration.
- **No dead or commented-out code** apart from MINOR 1.
- **Error handling in the fake matches the real contract.** Operations return `false`, `0` or `-1`,
  and never throw or abort.

## Findings

### MINOR 1: `String::operator+=(char)` is dead code

`test/stubs/Arduino.h:45-48` defines `String& operator+=(char c)`. Nothing that includes the stub
calls it:

- On the host `ARDUINOJSON_ENABLE_ARDUINO_STRING` is 0, so `serializeJson` appends through the
  generic `Writer`, which calls only the two `write` overloads (`ArduinoJson/Serialization/Writer.hpp:11-26`).
- `deserializeJson` reads through `begin()` and `end()`.
- `PersistableStore.cpp`, `TagPaletteFile.cpp`, `TagPalette.cpp` and `TextBlock.cpp` never use
  `+=` on a `String`.

To confirm, I removed the operator from a scratch copy of `test/stubs`. All four
`storage_io/*.cpp` files, `HalStorageFake.cpp`, `PersistableStore.cpp`, `TagPaletteFile.cpp` and
`TagPalette.cpp` still compiled (`-fsyntax-only`, clean). The comment at `Arduino.h:10-13` also
justifies every other member but not this one.

**Fix inline:** delete the four lines.

### MINOR 2: two line citations in the new test headers were already stale when merged

- `test/storage_io/AtomicWriteTest.cpp:3` cites `PersistableStore.cpp:22-44`. The function is at
  `lib/Serialization/PersistableStore.cpp:23-45`.
- `test/storage_io/AdoptingReadTest.cpp:3` cites `PersistableStore.cpp:63-106`. The function is at
  `:64-107`.

Both ranges are off by one because merging `main` (#118, `SdPaths.h`) added an include line. The
PR body records the same shift for `TagPaletteFile.cpp`'s warning, but these two comments were
never updated. The `SDCardManager.cpp:202` and `:282-284` citations in `HalStorageFake.cpp:5,7`
are still correct.

**Fix inline:** correct the two ranges. A better fix is to cite the function names
(`writeDocToFileAtomic`, `readDocFromFileAdopting`), which are already in the same sentences and
do not go stale.

### MINOR 3: `AdoptingReadTest` repeats the JSON overhead as a bare `8`

`AdoptingReadTest.cpp:101` asserts `persist::DEFAULT_SAVE_BUDGET - 8`. The `8` is the length of
`{"s":""}`, which `jsonOfSize` already computes as `overhead` at `:22`. If the helper's template
changes, the assertion drifts with nothing to show where the `8` came from.

**Fix inline:** make the overhead a named `constexpr` in the anonymous namespace, and use it in
both places.

## Not findings

- `TextBlock.cpp:393` now gives a `-Wsign-compare` warning on the host. This is because the stub's
  `HalFile::read` returns `int`, which matches `lib/hal/HalStorage.h:86`. The same comparison
  already exists in the device build, so the warning is a correct result of mirroring the real
  signature, not something this PR introduces.
- `TagPaletteFile.cpp:15` gives an `unused variable 'MODULE'` warning. The PR body already notes
  it: `LOG_ERR` is a no-op in the stub. It is not caused by this change.

No BLOCKER or MAJOR findings. The three MINORs are mechanical and can be fixed inline.

VERDICT: CLEAR
