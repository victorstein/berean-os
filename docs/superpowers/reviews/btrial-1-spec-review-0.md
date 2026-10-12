Tier: heavy

# Spec review 0: btrial-1, a name rank in TagPalette and its StudyStore consumers

Reviewed: `docs/superpowers/specs/2026-10-12-btrial-1-design.md` (commit `9bc7ad6e`), checked against
the brief (`hpipe brief --task t1`), the research note
`docs/superpowers/research/2026-10-12-btrial-1-research.md`, the parent spec
`docs/superpowers/specs/2026-10-11-tag-chip-picker-design.md` (D1, D2, §1, §6), and the code on
`feature/tag-order-rank`.

## Verdict in one paragraph

The design is sound. Every contract in the brief is met: the signatures are exact, `activeIds()` is
untouched, rebuilds happen only on an appending `add` or an accepted `fromJson`, there is a nothrow
key buffer with an allocation-order fallback, and all four CMake targets are covered. The four
deviations from the parent are each justified and each one is listed. I found no BLOCKER and no
MAJOR. The five MINORs below are about verification gaps and one wrong rationale. The implementer
can fix them inline at the plan phase.

## What I checked by running it

All probes are in `.../scratchpad/review0/`.

- **The ordering table, run end to end.** I re-implemented the spec's key builder and comparator
  exactly as written: skip `C2 BF`/`C2 A1`, fold through the real `Fold.cpp`, zero-pad to 24 bytes,
  `memcmp`, then the raw name, then the id. I ran every Step 1 row through it. Every expected order
  holds:
  - `[2]Alfa [3]alfa [1]beta`
  - `esperanza, éxodo, Ezequiel`
  - `año < anzuelo`, `ñu < nube`
  - `¡Ánimo!, paz, ¿Por qué?, reino`
  - `zeta < ¿¡Hola`
  - `10 mandamientos < 2 Reyes`
  - `[3]fe [7]fe`
  - the truncated-name tie gives `[4] [9]`

  A sweep of every 2-byte codepoint U+0080..U+07FF, 12 to a 24-byte string, never folded longer
  than its input. So the 24-byte slot holds.
- **Sizes**, checked under host clang 21 and `xtensa-esp32s3-elf-g++` 14.2.0 (static_asserts):
  - `Entry` is 40 B on host and 32 B on xtensa, with or without `uint16_t rank`, so A2 holds.
  - `SortKey` is 26 B.
  - xtensa defines `__CHAR_UNSIGNED__`.

  The two existing brace initialisers (`TagPalette.cpp:25`, `:99`) compile clean under
  `-Wall -Wextra` on both compilers, with no `-Wmissing-field-initializers`.
- **The OOM-test premise (Step 2).** I linked a model of the spec's `rebuildDisplayOrder` against the
  real `test/ditherers/FailingNothrowNew.cpp`, with `-fno-builtin`, on a 27-entry palette, which is
  past libc++'s insertion-sort cutoff.
  - `ScopedNthNothrowFailure(1)` around an appending add fired, and the result was allocation order.
  - `ScopedNthNothrowFailure(2)` did not fire, and the result was name order.

  So `std::sort`, the scratch `std::string` and `byName_.resize` make no nothrow allocation, and
  "exactly one nothrow allocation" is testable as the spec says. ArduinoJson's `DefaultAllocator`
  is `malloc`/`realloc`/`free` (`build/test/_deps/arduinojson-src/src/ArduinoJson/Memory/Allocator.hpp:26-36`).
- **The firmware link (A22).** In `piolib.py`, chain mode adds the same-name `.cpp` of each found
  header to the results (`:414-424`). `search_deps_recursive` then scans it through `depend_on`
  (`:448-475`). So `TagPalette.cpp`'s new includes make `StudyStore` depend on `BibleSearch`,
  `Memory` and `Logging`. The link is grouped (`platformio/builder/tools/piobuild.py:76`,
  `-Wl,--start-group`), so archive order cannot drop `foldAppend`. `pio run` remains the proof, as
  A22 says.
- **The PSRAM threshold.** `CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=4096` appears in every sibling
  worktree's generated `sdkconfig.x4pro` (`:2152` or `:2160`), and `/sdkconfig.*` is gitignored
  (`.gitignore:33`).
- **Include shadowing from `test/stubs` in `migration_planner` and `passage_doc`.** Checked. None of
  their sources include `Arduino.h`, `HalStorage.h` or `Logging.h`, and none uses `__has_include`.
  ArduinoJson includes `<Arduino.h>` only under `ARDUINOJSON_ENABLE_ARDUINO_*` or `ARDUINO`
  (`ArduinoJson.hpp:15-19`, `Polyfills/pgmspace.hpp:7-8`), and the host defines neither.
- **Other citations:** checked and correct.
  - `TagPalette.cpp:13-26`, `:42-47`, `:78-106`
  - `StudyStore.cpp:159-163`, `:184-194`, `:246-260`
  - `PassageDoc.cpp:17-27`, `:151`, `:197`, `:329`
  - `MigrationPlanner.cpp:24`, `:44`
  - `TagPickerActivity.cpp:160-167`, `:216-225`
  - `TagPaletteFileTest.cpp:112`, `TypedReference.cpp:138-150`, `:241-247`
  - `IndexBuilder.cpp:311-323`, `SdCardFont.cpp:1088-1098`, `FontInstaller.cpp:95`
  - the four `TagPalette.cpp` targets, the only ones in `test/*/CMakeLists.txt`
  - `tag_palette` registered at `test/CMakeLists.txt:115`

  The `fromJson` wrap bug that A12 relies on is filed as discovery `t1/x1` (`hpipe discoveries`).

## Findings

### MINOR 1: the batch-branch window (A18) is recorded only where its owners will not read it

- **Claim.** A18 says that from this task's landing until t5's, the picker stores a passage's tags in
  display order. It says "`t5` must not be dropped from the batch without moving the commit to
  `activeIds()`". The owner device checks include "The current picker (before `t5`) lists tags
  alphabetically" (spec line 412).
- **Problem.** The A18 analysis is correct. `commitAndFinish` iterates `tags_`
  (`TagPickerActivity.cpp:216-225`), and `tags_` is `STUDY.activeTags()` (`:27`, `:35`, `:79`).
  But the constraint lives only in this spec. The t5 worker and the orchestrator do not read it. If
  t5 is split out or deferred, the batch ships what the parent's Non-goals forbid: a changed stored
  order and a changed sleep-screen pill (parent spec lines 34-37).

  The device check is also unrunnable as written. The batch is tested and shipped as one PR, by
  which point t5 has replaced the picker. That makes the check either an intermediate-build check
  nobody will run, or a mislabelled final-state check.
- **Evidence.**
  - `hpipe status`: "ships as one PR … 0 of 5 landed".
  - `hpipe brief --task t5`, brief lines 55-56 and acceptance line 83: the commit restores
    `activeIds()`.
  - Spec lines 483-490, 412.
- **Fix.**
  - Have the worker state the A18 dependency to the orchestrator in the last message of the
    implement turn (for example: "t1 changes picker commit order until t5 lands; t5 must not be
    dropped").
  - Repeat that note in the plan.
  - Reword the device check against the final batch ("the picker lists tags alphabetically"), or
    drop the "(before `t5`)" qualifier.

### MINOR 2: the test-plumbing table leaves `TagDisplayOrderTest.cpp` out of the target

- **Claim.** The testing strategy says Step 3's five tests go in a new
  `test/tag_palette/TagDisplayOrderTest.cpp`, "both compiled into `TagPaletteTest`" (spec lines
  335-336).
- **Problem.** The "Library dependency and build" table is the actionable spec for
  `test/tag_palette/CMakeLists.txt`. Its "Sources added" column lists only `Fold.cpp` and
  `FailingNothrowNew.cpp` (spec line 254). A `.cpp` that is not in `add_executable` is never
  compiled. `gtest_discover_tests` then finds nothing, and ctest stays green while
  `inDisplayOrder` and the `tagNamesFor` ordering have no coverage. Step 4 relies on exactly those
  tests (spec lines 396-397).

  Separately, Step 0 says to "Add `Fold.cpp`, the include directories and the injector to the four
  targets" (line 338-339). The table, correctly, puts the injector and `-fno-builtin` in
  `test/tag_palette` only.
- **Evidence.**
  - Spec lines 252-257, 335-339, 386-397.
  - `test/tag_palette/CMakeLists.txt:1-4` lists only `TagPaletteTest.cpp` today.
- **Fix.**
  - Add `TagDisplayOrderTest.cpp` to the `test/tag_palette` row's "Sources added".
  - Reword Step 0 to "and, in `test/tag_palette` only, the injector and `-fno-builtin`".
  - In the plan, have the implementer confirm that `ctest -R TagDisplayOrder` reports five tests.

### MINOR 3: "No render task reads the palette" is false for the interim picker

- **Claim.** "Everything runs on the loop task … No render task reads the palette (parent A1)" (spec
  lines 283-288).
- **Problem.** Until t5 removes `tags_`, `TagPickerActivity::buildScreen` calls `STUDY.activeTags()`
  on every build (`TagPickerActivity.cpp:79`). After this change, that call walks `byName_` through
  `activeIdsByName()`, and it runs on the render task:
  - The render loop takes `RenderLock` and calls `render()` (`ActivityManager.cpp:52-59`).
  - `UiListActivity::render` calls `renderUi()` (`UiListActivity.cpp:154-157`).
  - `renderUi()` reaches `buildScreen` through `screenTrampoline` (`:29-31`).

  The conclusion still holds. Every interim mutation that rebuilds (`addTagName`) or flips `active`
  (`retireTag`) runs under `RenderLock` (`TagPickerActivity.cpp:164-166`, `:270-272`, A16), so the
  reader and the rebuild never overlap. But the stated reason is wrong. Someone who later moves a
  rebuild-triggering mutation out of the lock, on the strength of this sentence, would open a real
  race on `byName_`.
- **Evidence.** As cited above.
- **Fix.** Replace the sentence with: "Until t5 lands, the picker's `buildScreen` reads
  `activeTags()` on the render task under the render lock. Every palette mutation that can rebuild
  runs under `RenderLock`, so the two never overlap. After t5, no render task reads the palette
  (parent A1)."

### MINOR 4: the acceptance clause "a `LOG_ERR` is logged" has no verification path

- **Claim.** The error-handling table and the brief's acceptance both say an OOM rebuild logs
  `LOG_ERR` (spec line 294; brief: "On OOM in the rebuild, the order falls back to allocation order
  and a `LOG_ERR` is logged").
- **Problem.** The host tests use the no-op stub, where `#define LOG_ERR(origin, format, ...)`
  expands to nothing (`test/stubs/Logging.h:6`). No log-capturing stub exists anywhere in `test/`.
  The device checks cover only the negative ("no `TAGS` OOM line"). So the `TagPaletteOom.*` tests
  observe the fallback order and nothing else. The tester could reasonably believe the log line is
  tested.
- **Evidence.**
  - `find test -name 'Logging*.h'` returns only `test/stubs/Logging.h`.
  - `grep -rln 'define LOG_ERR' test lib` returns the stub and `lib/Logging/Logging.h`.
- **Fix.** Say under "Testing strategy" that the log line is verified by inspection at PR review, and
  that its format string is checked by `-Wformat` under `pio run` (`platformio.ini:73`). That
  check works because `logPrintf` carries `format(printf, 3, 4)` (`lib/Logging/Logging.h:42`). Do
  not invent a capturing stub for this task.

### MINOR 5: the verification list omits `pio check`, which CI enforces

- **Claim.** The verification commands are the host suite, `pio run` and `./bin/clang-format-fix`
  (spec lines 401-406).
- **Problem.** CI also runs `pio check --fail-on-defect low --fail-on-defect medium --fail-on-defect high`
  (`.github/workflows/ci.yml:76`) with `--enable=all` (`platformio.ini:24`). With no
  `check_src_filters`, it scans `src/` and `include/` (`platformio/check/cli.py:105-111`), and that
  includes the rewritten `StudyStore.cpp` loops. A style defect there fails the batch PR's CI only
  at the very end, after all five tasks have landed. CLAUDE.md's checklist calls for
  `pio check` "when relevant".
- **Evidence.** As cited above.
- **Fix.** Add `~/.platformio/penv/bin/pio check -e x4pro --fail-on-defect low` to the verification
  list, or at least to the implement phase's checks.

## Assumptions attacked and found to hold

- **A1 (both directions cached).** Correct.
- **A3 (throwing `byName_`).** Correct.
- **A4 (zero padding, no length byte).** `fromJson` names cannot contain NUL, because
  `const char*` is used at `TagPalette.cpp:88`. Only `add` can produce one, and the raw-name
  tiebreak orders `"a"` against `"a\0"` (probe: `[2]a [1]a`).
- **A5, A6.** Correct.
- **A7 (throwing scratch).** This is the same failure class as the `push_back` copies that already
  exist. Avoiding it would need a `Fold.h` change, which is out of scope.
- **A9, A10 (fallback is coherent and not sticky).** Correct.
- **A12.** Correct as scoped: the sentinel holds, and the wrap bug is filed.
- **A14 (header-only helper in `lib/StudyStore`).** It needs no new target. `PassageDoc.h` is only
  read for an inline `static constexpr`.
- **A15.** The cap is unreachable: every set path normalises (`PassageDoc.cpp:151`, `:197`, `:329`).
- **A16, A17.** Correct.
- **A20, A21.** The copy matches `FailingNothrowNew.h:5-7`'s "confined to this suite".
- **A22, A23.** Correct.

VERDICT: CLEAR
