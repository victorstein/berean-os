# Research: btrial-1, a name rank in TagPalette and its StudyStore consumers

Bead `btrial-1`, task `t1`, run `berean-os-20261012-tag-chip-picker-2l3m`, branch
`feature/tag-order-rank` (cut from `batch/20261012-tag-chip-picker-2l3m` at `59c370cf`). Batch spec:
`docs/superpowers/specs/2026-10-11-tag-chip-picker-design.md`, §1 and §6. Surface `data`, tier
`heavy`.

Every claim below cites a line I read or a command I ran on 2026-10-12.

## 1. Who owns the behaviour

| Behaviour | Owner |
|---|---|
| Tag entries, ids, names, active flag | `lib/StudyStore/StudyStore/TagPalette.h:68-75` (`Entry{id, name, active}`, `entries_`, `nextRaw_`) |
| Allocation-order list | `TagPalette::activeIds()`, `TagPalette.h:57-58`, `TagPalette.cpp:57-64` |
| Name lookup (the linear-scan model for `nameRank`) | `TagPalette::name()`, `TagPalette.cpp:42-47` |
| Picker / filter list of tags | `StudyStore::activeTags()`, `src/study/StudyStore.cpp:159-163`; comment `StudyStore.h:42-49` |
| Passage-row tag summary | `StudyStore::tagNamesFor()`, `StudyStore.cpp:184-194`; comment `StudyStore.h:59-61` |
| The collation fold | `BibleSearch::foldAppend`, `lib/BibleSearch/BibleSearch/Fold.h:18-23`, `Fold.cpp:106-123` |

`TagChipView::buildEntries` (`src/activities/reader/TagChipView.cpp:16`) is the only other reader of
`activeIds()` (grep below). It belongs to the sibling grid-base task.

```
$ grep -rnE 'activeIds\(\)|activeTags\(\)|tagNamesFor|palette_\.add' src lib test
src/study/StudyStore.cpp:159 / :161 / :184 / :250
src/study/StudyStore.h:49 / :61
src/activities/reader/HighlightsActivity.cpp:94          (tagNamesFor)
src/activities/reader/TagChipView.cpp:16                 (activeIds, sibling task)
src/activities/reader/TagPickerActivity.cpp:27,35,79,166,272   (activeTags)
lib/StudyStore/StudyStore/TagPalette.h:58, TagPalette.cpp:57
```

## 2. Current control flow

**Mutations of `entries_`.** Four paths change it. Only two change the set of names:

- `add` with a new name appends (`TagPalette.cpp:21-26`). It refuses once 200 tags are active
  (`:21`, `MAX_ACTIVE_TAGS`, `TagPalette.h:38`) or when ids run out (`:22`).
- `add` with an existing name revives that entry in place (`:15-19`).
- `retire` flips `active` (`:29-33`).
- `fromJson` returns false *before* `entries_.clear()` for a non-object or an unknown version
  (`:79-81`, clear at `:83`). Otherwise it rebuilds from the array, dedupes by id only (`:94-97`),
  truncates a name to 24 bytes (`:91-92`) and always returns true (`:105`). A rank rebuild therefore
  belongs only on the success path. The early returns leave `entries_` and any rank untouched.

**Who calls `add`.**

- `StudyStore::addTagName` (`StudyStore.cpp:246-260`): it detects an existing name by
  `activeCount()` not moving (`:252`). On a failed save it rolls back with `retire` (`:254-257`).
- `MigrationPlanner::adoptTagNames`, one `add` per name in a loop (`MigrationPlanner.cpp:23-25`).
- `planMigration`, one `add` per passage tag (`MigrationPlanner.cpp:42-44`).

**Who calls `fromJson`.**

- `TagPaletteFile::load` (`src/study/TagPaletteFile.cpp:18-23`). `StudyStore::openPublication`
  calls it on every book open (`StudyStore.cpp:71`), and `MigrationRunner` calls it once
  (`MigrationRunner.cpp:209-210`).
- `StudySleepScreen::tagName`, which builds a throwaway palette only to read one name
  (`src/activities/boot_sleep/StudySleepScreen.cpp:161-175`).

`loadAdopting` calls the acceptor only when a document was loaded or a temp file promoted
(`lib/Serialization/PersistableStore.cpp:221-228`). So a missing file never reaches `fromJson`,
and `palette_` keeps its previous contents: `closePublication` does not reset it
(`StudyStore.cpp:99-108`). A rank that is object state beside `entries_` stays coherent through all
of this, and through a copy (`test/storage_io/TagPaletteFileTest.cpp:112` copies a palette).

**Consumers.**

- `activeTags()` pushes `{id, name}` for each `activeIds()` entry. It does not `.reserve()` first
  (`StudyStore.cpp:160-161`), although CLAUDE.md "The resource protocol" rule 7 requires it.
- `tagNamesFor()` walks the passage's stored `tags` in stored order (`StudyStore.cpp:187`). It
  resolves each id through `tagName()`: UNLABELLED gets its translated label (`:180`), and any other
  id goes through `palette_.name()` (`:181`). It skips an empty name, so an unknown id is dropped
  (`:188-189`), and joins with `", "`.
  - A passage holds at most 8 tags (`PassageDoc.h:45`, `MAX_TAGS_PER_PASSAGE`), stored as
    `std::vector<TagId>` (`TaggedPassage.h:41`).
  - It carries UNLABELLED only when it carries nothing else (`TagPalette.h:29-30`). UNLABELLED
    therefore never competes for position, and its `UINT16_MAX` rank is moot in practice.
- `tagNamesFor` hot path: `HighlightsActivity::rebuildRowItems` calls `tagsValueFor` once for
  **every filtered passage**, not once per row on screen (`HighlightsActivity.cpp:97-106`).
  `tagsValueFor` caps the joined string at 24 bytes (`:90-95`), so the row shows whichever tag comes
  first in display order.

## 3. The fold, checked by running it

`foldAppend(std::string& out, std::string_view)` (`Fold.h:23`) appends to a `std::string` and
reserves on it (`Fold.cpp:107`). There is no raw-buffer overload. I ran a host probe in the
scratchpad, linked against the real `Fold.cpp`:

```
$ c++ -std=c++20 -I lib/BibleSearch/BibleSearch probe.cpp lib/BibleSearch/BibleSearch/Fold.cpp && ./probe
¿Por qué?   -> "\xC2\xBFpor que?"
¡Ánimo!     -> "\xC2\xA1animo!"
Ñandú       -> "nandu"
año -> "ano", anzuelo -> "anzuelo";  fold(año) < fold(anzuelo): 1
fold(¿x) > fold(zz): 1
2-byte codepoints U+0080..U+07FF: fold never longer: 1 (max out 2)
a 24-byte name cut mid-codepoint: in 24 out 24, last byte 0x20
```

These results confirm the following:

- `¿`/`¡` pass through unfolded and sort after `z`, so D2's skip is needed. The reason is that
  U+00BF and U+00A1 lie below `LATIN_FIRST` = U+00C0 (`Fold.cpp:48`, `:116-119`).
- ñ→n (`Fold.cpp:61`). "año" sorts before "anzuelo", as D2 accepts.
- A fold is never longer than its input. That includes a name that `fromJson`'s resize cut
  mid-sequence (`TagPalette.cpp:92`): each malformed byte becomes one space (`Fold.cpp:111-112`). The
  spec's 24-byte key slot (§1 "Cost and bound") holds, because names are at most 24 bytes in both
  `add` (`TagPalette.cpp:13`) and `fromJson` (`:92`).

## 4. Findings that sharpen the brief

**F1. The key buffer needs a scratch `std::string`.** `foldAppend` writes only to a `std::string`
(`Fold.h:23`). Folding "into a flat key buffer" therefore means one of two things:

- fold into a scratch string, then copy into the slot;
- or change `Fold.h`, which another surface owns. That is out of scope here.

The repo already does the first:

- `TypedReference.cpp:241-242` reserves one scratch string once (`scratch.reserve(KEY_BYTES)`).
- `normalise` reuses it: `scratch.clear(); foldAppend(scratch, text);` (`:138-140`), then copies the
  bytes into a fixed `char[]` (`:149`).

The scratch allocation goes through the throwing `operator new` (`std::allocator`), not through
`makeUniqueNoThrow`. Under `-fno-exceptions` an OOM there aborts (`lib/Memory/Memory.h:9-10`). The
spec's OOM fallback covers the key buffer only. The spec phase must say this, or bound the scratch.

**F2. `lib/StudyStore` gains three dependencies, not one.** `LOG_ERR` and `makeUniqueNoThrow` are
new to the library. No file in it includes `<Logging.h>` or `<Memory.h>` today:

```
$ grep -rln 'Logging.h\|Memory.h' lib/StudyStore     # no output
```

On the host:

- Without `lib/Memory` on the include path, `<Memory.h>` resolves to the C library's
  `<memory.h>`. That is a recorded gotcha (`lib/GfxRenderer/BitmapHelpers.h:9-11`).
- `<Logging.h>` must resolve to the no-op stub `test/stubs/Logging.h:6-8`. The firmware header
  `lib/Logging/Logging.h:3-4` needs Arduino.

Each of the four targets needs different additions:

| Target | Has today | Must add |
|---|---|---|
| `test/tag_palette` (`CMakeLists.txt:6-9`) | `lib/StudyStore`, `lib/Serialization` | `Fold.cpp`, `lib/BibleSearch`, `lib/Memory`, `test/stubs` |
| `test/migration_planner` (`:16-22`) | no stubs, no Memory | the same four |
| `test/passage_doc` (`:11-15`) | no stubs, no Memory | the same four |
| `test/storage_io` (`:28-35`) | `test/stubs`, `lib/Memory` | `Fold.cpp`, `lib/BibleSearch` |

- **Model for the include shape:** `test/bible_search_index/CMakeLists.txt:19-27` adds
  `lib/BibleSearch` ("`<BibleSearch/...>`, as src/study includes it"), `lib/Memory` and
  `test/stubs` ("no-op Logging.h").
- **Adding `test/stubs` cannot shadow anything.** None of the sources those targets compile include
  `Arduino.h`, `HalStorage.h`, `HalDisplay.h`, `Logging.h` or use `__has_include`. I grepped
  `VerseAnchors`, `ParagraphAnchors`, `htmlEntities`, `Utf8` and every `lib/StudyStore` file, and
  the grep exited 1.
- **The root `test/CMakeLists.txt` is untouched.** `tag_palette` is already registered (`:115`).
  data-dev.md's "report, do not edit" rule (`.claude/agents/data-dev.md:23-27`) names that root
  file, not the per-suite files.

**F3. The OOM test needs its own injector.** The only replaced nothrow `operator new[]` lives in
`test/ditherers/FailingNothrowNew.{h,cpp}`. Its header says it "is never linked into another suite"
(`FailingNothrowNew.h:5-7`).

- The CMake side is `-fno-builtin` on the target (`test/ditherers/CMakeLists.txt:16-19`).
- The driving pattern is `ScopedNthNothrowFailure(n)` plus `fired()` (`DitherersTest.cpp:57-80`).

`test/tag_palette` needs its own copy, or the injector has to move somewhere shared. The spec
phase must choose; the brief only says "the pattern the repo's OOM tests already use".

Inside the guard, the rank's key buffer is the only allocation the injector can fail, because the
other allocators bypass it:

- `std::string` and `std::vector` use the throwing form, which never fails
  (`FailingNothrowNew.cpp:24-28`, `:54-55`).
- ArduinoJson's `DefaultAllocator` uses `malloc`/`realloc`/`free`
  (`build/test/_deps/arduinojson-src/src/ArduinoJson/Memory/Allocator.hpp:27-35`).

`ScopedNthNothrowFailure(1)` around an `add` or a `fromJson` therefore hits exactly that buffer,
provided the rebuild makes a single nothrow allocation.

**F4. Migration rebuilds the rank once per new name.** `adoptTagNames` and `planMigration` call
`add` in loops (F2 above, `MigrationPlanner.cpp:24`, `:44`). Under "rebuild on an appending add",
a migration of *k* new names does *k* full rebuilds.

- *k* is at most 200, because `add` refuses past `MAX_ACTIVE_TAGS` (`TagPalette.cpp:21`).
- The migration runs once (`MigrationRunner.cpp:209`).
- The host test `TagPaletteBudget.RefusesBeyondTheTagCeiling` does 200 adds
  (`TagPaletteTest.cpp:134-140`).

This is acceptable, but the spec should state the cost rather than leave it implied.

**F5. The rank is built and then discarded on the sleep screen.** `StudySleepScreen::tagName`
parses a palette for one `name()` lookup (`StudySleepScreen.cpp:169-174`), so the rank built there
is thrown away. The batch spec already accepts this cost (§1, "`fromJson` also runs on … every
study sleep screen"). I record it so nobody "optimises" it with a lazy rank, which would put a fold
back on the hot path.

**F6. The firmware link should resolve, but is unproven until `pio run`.**

- `lib/StudyStore` has no `library.json`; `lib/BibleSearch` has one with a name only
  (`lib/BibleSearch/library.json`).
- `platformio.ini` sets no `lib_ldf_mode` (grep of `platformio.ini`), so the default `chain`
  applies (`…/platformio/project/options.py:587-594`).
- Chain mode parses the same-name `.cpp` of each included header (`piolib.py:119`,
  `PARSE_SRC_BY_H_NAME = True`; `:414-424`). It then follows that library's includes into
  `depend_on` (`:428-449`, `:451-475`). `TagPalette.cpp`'s new include should therefore make
  `StudyStore` depend on `BibleSearch`, `Memory` and `Logging`.
- No other `lib/` library includes `<BibleSearch/...>` today; only `src/` does
  (`src/activities/reader/TypedReference.cpp:3`, `src/study/BibleSearch*.h`). This is the first
  lib-to-BibleSearch edge.
- `lib/BibleSearch` never references `StudyStore` (grep: no output), so there is no cycle.
- `BibleSearch`'s own sources already include `<Logging.h>` and `<Memory.h>`
  (`VerseTextScanner.cpp:3-4`). That proves those two resolve from a `lib/` library.

## 5. Nearest existing examples

| Need | Model |
|---|---|
| Fold into a fixed buffer through one reused scratch | `src/activities/reader/TypedReference.cpp:138-150`, `:241-247` |
| Sort an index array over flat byte keys: memcmp, then length | `lib/BibleSearch/BibleSearch/IndexBuilder.cpp:311-323` (fails cleanly on a null buffer, `:314-317`) |
| Nothrow index array, `LOG_ERR` on failure, then `std::sort` | `lib/EpdFont/SdCardFont.cpp:1088-1098` |
| Linear id lookup returning a sentinel | `TagPalette::name()`, `TagPalette.cpp:42-47` |
| OOM injection in a host test | `test/ditherers/{CMakeLists.txt:16-19, FailingNothrowNew.h, FailingNothrowNew.cpp, DitherersTest.cpp:57-80}` |
| Host target needing Fold + Memory + Logging stubs | `test/bible_search_index/CMakeLists.txt:3-27` |
| Test style and naming for this class | `test/tag_palette/TagPaletteTest.cpp` (`TEST(TagPalette<Area>, <Behaviour>)`, a reason in the failure message) |

## 6. Installed tools

```
$ cmake --version                    -> cmake version 4.4.2
$ c++ --version                      -> Apple clang version 21.0.0 (clang-2100.0.123.102), arm64-apple-darwin25.5.0
$ ~/.platformio/penv/bin/pio --version -> PlatformIO Core, version 6.1.19   (pio is not on PATH)
$ ~/.platformio/packages/toolchain-xtensa-esp-elf/package.json -> "version": "14.2.0+20251107"
```

The host suite pins GoogleTest `v1.17.0` (`test/CMakeLists.txt:17`) and ArduinoJson `v7.4.2`
(`:31`). The firmware build pins `bblanchon/ArduinoJson @ 7.4.2` (`platformio.ini:155`) and builds
GNU++20 (`platformio.ini:40`).

## 7. Baseline before any change

```
$ cmake -S test -B build/test -DCMAKE_BUILD_TYPE=Release            -> exit 0
$ cmake --build build/test --target TagPaletteTest MigrationPlannerTest PassageDocTest StorageIoTest -j 8
                                                                     -> Built target StorageIoTest (all four)
$ build/test/tag_palette/TagPaletteTest             [  PASSED  ] 14 tests.
$ build/test/migration_planner/MigrationPlannerTest [  PASSED  ] 14 tests.
$ build/test/passage_doc/PassageDocTest             [  PASSED  ] 97 tests.
$ build/test/storage_io/StorageIoTest               [  PASSED  ] 69 tests.
```

`build/` is gitignored (`.gitignore:13`). I have not yet run the full host suite or `pio run`. The
implement phase runs both.

## 8. Tier

The tier stays at `heavy`. The change stays on surface `data` (`lib/StudyStore`, `src/study`)
plus four per-suite test CMake files. It moves no file format and no store version: the palette
file is unchanged, and `FORMAT_VERSION` = 1 at `TagPalette.h:35`.

The public additions, `activeIdsByName()` and `nameRank()`, are the contract the UI siblings
consume. The batch spec already fixes their signatures (§1), and `heavy` already covers a contract.
I found nothing that needs a migration or another surface.

## 9. Open for the spec phase

1. Scratch-string strategy, and whether its throwing allocation is acceptable (F1).
2. Where the OOM injector lives for `test/tag_palette`: a copy or a shared move (F3).
3. The rank's storage: a field in `Entry`, or a parallel vector. Also whether `activeIdsByName`
   sorts per call or keeps a sorted index. The spec fixes only "one `uint16_t` per entry".
4. How a key's length is carried in the 24-byte slot (a length byte, or zero padding). Zero padding
   is safe only if a name can never hold a NUL. `add` takes a `std::string` (`TagPalette.h:46`),
   which can.
