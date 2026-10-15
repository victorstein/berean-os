# Research: berean-o-15 (t2): `PreachingTopicsFile` and the `bible.json` passes

Branch `feature/preaching-topics-store`, cut from `batch/20261015-predicaci-n-193x` at `eecbf1d1`.
Spec: `docs/superpowers/specs/2026-10-14-preaching-tool-design.md` §3, §4, §7, §9, D4, D5.
Every claim below was read in this worktree on 2026-10-15.

## 1. Who owns the behaviour today

Nothing does yet. `grep -rn "preaching" src lib` turns up nothing, and the only mentions are in the
spec. Both halves of the task are new files under `src/study/`, plus one constant in
`lib/Serialization/SdPaths.h`.

The neighbours they build on:

| Concern | Owner | Evidence |
|---|---|---|
| SD paths | `lib/Serialization/SdPaths.h` | `HOME_VERSE_FILE` at `:27`, with the "not under PASSAGES_DIR" comment at `:26`. Every path has a `static_assert(isUnder(...))` at `:50-62`. |
| Small `/.berean/` JSON file | `src/study/TagPaletteFile.{h,cpp}` | `load` → `loadAdopting` (`.cpp:18-23`); `save` measures, then calls `writeDocToFileAtomic` (`.cpp:25-35`). |
| Atomic write, adopting read | `lib/Serialization/PersistableStore.{h,cpp}` | `writeDocToFileAtomic` (`.h:68`), `readDocFromFileChecked` (`.h:76`), `readDocFromFileStreamed` (`.h:81`, `.cpp:141-162`), `loadAdopting` (`.h:115,120`, `.cpp:217-246`). |
| Load outcome | `lib/Serialization/TempAdoption.h` | `AdoptedLoad {Loaded, Empty, RecoveredFromTemp, Failed}` (`:69-74`); `adoptedLoad()` (`:79-92`). |
| Tag vocabulary | `lib/StudyStore/StudyStore/TagPalette.h` | `TagId` is a scoped `uint16_t` enum (`:23`); `MAX_ACTIVE_TAGS = 200` (`:37`); `isActive` (`:51`); `activeIdsByName` (`:61`). |
| Passage row format | `lib/StudyStore/StudyStore/PassageDoc.{h,cpp}` | `FORMAT_VERSION = 4` (`.h:27`); `toJson` keys (`.cpp:269-300`); `fromJson` (`.cpp:304-339`). |
| A passage in memory | `lib/StudyStore/StudyStore/TaggedPassage.h:31-45` | `start` (Unit), `documentSpine`, `snippet`, `displayText` (`PassageText`), `reference`, `tags`. |
| PSRAM allocators | `src/study/PsramJsonAllocator.{h,cpp}` | `json()` and `text()` (`.h:18,20`), both on `heap_caps_malloc(..., MALLOC_CAP_SPIRAM)` (`.cpp:4,12,19`). |
| The `bible.json` path | `src/activities/launcher/HomeVerse.cpp:31-34` | `snprintf("%s/%s.json", sdpaths::PASSAGES_DIR, study::BIBLE_PUB_KEY)`; `BIBLE_PUB_KEY = "bible"` (`lib/StudyStore/StudyStore/PubKey.h:37`). |
| A read-only streaming pass over `bible.json` | `src/activities/boot_sleep/StudyPassageScan.cpp` | `pickFromFile` (`:181-199`), `offerFile` (`:101-137`), `offerRow` (`:57-98`). |

## 2. Current control flow

### 2.1 How a small store loads, and how its caller latches saving off

`TagPaletteFile::load` (`TagPaletteFile.cpp:18-23`) calls
`PersistableStoreBase::loadAdopting(PATH, &readDocFromFileChecked, accept, &palette)`. Then:

1. `readAdopting` (`PersistableStore.cpp:171-199`) reads the primary file. Only if it is
   **Missing** does it look at `<path>.tmp`. A `.tmp` that parses is **renamed into place**
   (`:188-197`).
2. `loadAdoptingInto` (`:217-231`) hands the document to `accept` (`fromJson`).
3. `adoptedLoad` (`TempAdoption.h:79-92`) maps the outcome:
   - unreadable or unparseable primary → `Failed`;
   - parsed but rejected → `Failed`;
   - no file and no usable `.tmp` → `Empty`.

**The latch lives in the caller, not the file module.** `StudyStore::openPublication` sets
`saveDisabled_ = true` on `LoadResult::Failed` (`src/study/StudyStore.cpp:72-76`, and `:80-83` for passages). `save()`,
`addTagName()` and `retireTag()` and every other mutator check it first (`:152`, `:243`, `:259`, `:321`, `:365`…). `closePublication`
deliberately leaves it set (`:107-108`). Every caller outside `StudyStore` repeats the same `Failed`
check by hand, for example `MigrationRunner.cpp:210-211`. That hand-repeated check is exactly what
the brief wants T5 unable to forget.

**A gap the spec should not copy.** `persist::loadRefusedAfter` (`FormatVersion.h:22-33`), the
guard behind `PersistableStore<T>::loadRefused` (`PersistableStore.h:52,165-170,236`), latches
**only** on a parsed-but-rejected file. For `Unreadable` and `ParseError` it keeps the previous
value (`:28-31`). So on a first load a corrupt file does **not** latch there.

The brief asks for a corrupt file to latch too. The `AdoptedLoad::Failed` shape already does that:
`ReportFailed` → `Failed` (`TempAdoption.h:88-90`), so the latch should key on `Failed`, as
`StudyStore` does, and not on `loadRefusedAfter`.

### 2.2 How a small store saves

`TagPaletteFile::save` (`TagPaletteFile.cpp:25-35`):

1. `toJson`.
2. `measureJson(json) > persist::DEFAULT_SAVE_BUDGET` → `TooLarge`.
3. Otherwise `writeDocToFileAtomic` → `Ok` or `WriteFailed`.

`writeDocToFileAtomic` streams to `<path>.tmp` through a 512 B buffer, then renames over the target
(`PersistableStore.h:62-68`). `HalStorage` takes `storageMutex` on every file call (CLAUDE.md, "SdFat
is not thread-safe").

`persist::fitsBudget(bytes, budget)` (`SaveBudget.h:26`) is the shared predicate. Its `<=` is the
same comparison as TagPaletteFile's `!(>)`.

### 2.3 The single-task assumption

`loadAdopting` and the adopting read **rename**. They are safe only because every reader and writer
of these files runs on the Arduino loop task. `PersistableStore.h:106-114` lists the files that rely
on that, `TagPaletteFile` among them. A new `preaching.json` read through `loadAdopting` joins that
list. It must stay loop-task-only. The comment there is the place to name it.

### 2.4 How `bible.json` is laid out and read today

**The row keys `PassageDoc::toJson` writes** (`PassageDoc.cpp:269-300`):

| Key | Holds |
|---|---|
| `v` | file version, 1–4 |
| `p` | the array of rows |
| `u` | compact start Unit |
| `e` | end Unit |
| `f`, `fe` | fingerprints |
| `d` | document |
| `s` | `documentSpine` |
| `x` | snippet |
| `w` | display text; absent when empty (`:281`) |
| `h` | whole flag |
| `r` | reference |
| `g` | pendingUpgrade |
| `t` | tag ids |
| `k` | links |

**`fromJson`'s row rules** (`PassageDoc.cpp:304-339`). A pass has to match them, or it would count
rows the store itself drops:

- A row whose `u` does not parse is skipped (`:311-312`).
- `s` reads as `uint16_t`, absent as 0 (`:321`).
- `x` and `r` are cut with `utf8SafeSummary` to 120 and 48 bytes (`:322-323`).
- Tag ids over `UINT16_MAX` are dropped, then normalised (`:325-329`). `normaliseTags` dedupes, so
  a repeated id in one row is one tag.
- An unknown version is refused (`:306-307`).
- `displayTextFromJson` (`:91-109`) refuses a whole row with an empty `w`, or a legacy `w` outside
  121–384 bytes. The **whole document** then fails to load. The file is the store's, so a pass that
  only reads it can still use each row on its own; see §5.

**Row order on disk is insertion order, not Bible order.** `HighlightsActivity.cpp:67-69` treats it
as oldest-first. `StudyStore::passagesWithTag` (`StudyStore.cpp:159-171`) returns file order. So the
tag pass must sort. `operator<(Unit, Unit)` (`Unit.cpp:38-51`) orders by kind, then book, chapter,
verse and offset.

**The existing read-only pass, `StudyPassageScan`.** It reads `bible.json` daily for Home's verse
(`HomeVerse.cpp:146-149` → `pickFromFile`). It:

- checks `Storage.exists` first, because a missing Bible file is ordinary and a failed open logs
  (`:182-186`);
- closes the probe handle before reopening the same path (`:188-189`);
- refuses a file over `MAX_FILE_BYTES = PassageDoc::SAVE_BYTE_BUDGET + 4096` (`:28,104-108`);
- does **not** use `PassageFile::load`, "a rename, and this path must never write to the study
  store" (`:115-117`);
- parses the **whole** document into a PSRAM `JsonDocument` via `readDocFromFileStreamed`
  (`:118-119`);
- turns an allocation failure into an OOM flag through `doc.overflowed()` (`:122-123`);
- refuses an unknown version or a missing `p` (`:126-130`);
- feeds the loop task's watchdog every 32 rows (`:35,133`);
- logs its time from the caller (`HomeVerse.cpp:147-152`).

So the repo already pays a ~204 KB whole-document PSRAM parse of `bible.json`, once a day.

**No ArduinoJson filter is used anywhere in the firmware.**
`grep -rn "DeserializationOption" src lib` returns nothing. The other streaming parser in the
tree is `lib/JsonParser/StreamingJsonParser`, a SAX-style parser used by the network code. Its token
buffer is `TOKEN_BUF_SIZE = 512` (`StreamingJsonParser.h:22`), and a longer string sets
`tokenOverflow` and fires **no callback** (`StreamingJsonParser.cpp:227-243`). Whole texts have no
cap (`PassageDoc.h:40-43`), so that parser would silently drop a long passage's text. It cannot do
the tag-passages pass.

## 3. Installed versions

| Tool | Version | Evidence |
|---|---|---|
| ArduinoJson (firmware) | 7.4.2 | `platformio.ini:155`, `bblanchon/ArduinoJson @ 7.4.2` |
| ArduinoJson (host tests) | v7.4.2 | `test/CMakeLists.txt:28-32`, `GIT_TAG v7.4.2` |
| PlatformIO Core | 6.1.19 | `~/.platformio/penv/bin/pio --version`. `pio` is not on PATH (`which pio` → not found). |
| CMake | 4.4.2 | `cmake --version` |
| Host compiler | Apple clang 21.0.0 | `c++ --version` |
| Ninja | not installed locally | CI configures `-G Ninja` (`.github/workflows/ci.yml:145`); locally the default Makefiles generator works. |
| clang-format | 21, from the primary checkout | `.venv/bin/clang-format` symlinks there; `bin/bootstrap` set it up. |

ArduinoJson 7 provides `DeserializationOption::Filter`. With a filter document such as
`filter["p"][0]["t"] = true`, the parser keeps only the marked fields of every element of `p`.

**Baseline.** These commands:

```
cmake -S test -B build/test -DCMAKE_BUILD_TYPE=Release
cmake --build build/test --target StorageIoTest -j8
./build/test/storage_io/StorageIoTest --gtest_brief=1
```

give `[==========] 69 tests from 6 test suites ran. [  PASSED  ] 69 tests.` `build/` is gitignored
(`.gitignore:13`).

## 4. The nearest existing examples

- **Store: `TagPaletteFile`** (`src/study/TagPaletteFile.{h,cpp}`) and its host test
  `test/storage_io/TagPaletteFileTest.cpp`.
  - Namespace API: `using LoadResult = AdoptedLoad`, `load(T&)`, `enum class SaveResult : uint8_t
    {Ok, TooLarge, WriteFailed}`, `save(const T&)`, `inline constexpr const char* PATH`.
  - Its test resets the fake in `SetUp` (`storage_fake::reset()`), then round-trips the file and
    asserts that no `.tmp` is left behind.
  - `PassageFile` (`src/study/PassageFile.cpp:28-46`) adds a check worth copying:
    `json.overflowed()` → `WriteFailed`, before the budget check, so a short OOM document never
    passes the budget and gets written.
- **Session latch: `StudyStore::saveDisabled_`** (§2.1). It is the only existing "hold the latch
  for the session" mechanism. No small wrapper object exists, so the brief's session object is a
  new class, but it wraps an existing rule rather than inventing one.
- **Read-only pass: `study_passage_scan::pickFromFile`** (§2.4), for the existence check, the size
  cap, the version check, the PSRAM document, OOM via `overflowed()`, the watchdog, and the timing
  log. It is the model for the two passes. The difference is that it parses the whole document
  unfiltered.
- **Allocator injection for host tests: `HomeVerseFile`.** The firmware passes
  `PsramJsonAllocator::json()` (`HomeVerse.cpp:94,110`); the host test passes `nullptr`
  (`test/home_layout/HomeVerseFileTest.cpp:68-72`). `PassageDoc::Allocators` (`PassageDoc.h:55-58`)
  is the same idea for texts, with `defaultTextAllocator()` (malloc) on the host
  (`PassageText.h:22-25`).

## 5. Constraints the design has to meet

1. **`PsramJsonAllocator.cpp` does not build on the host.** It includes `<esp_heap_caps.h>`
   (`:4`), and no stub provides it (`ls test/stubs`: `Arduino.h HalDisplay.h HalStorage.h
   HalStorageFake.{h,cpp} Logging.h ObfuscationUtilsStub.cpp`). Both passes must therefore **take**
   a `study::TextAllocator` and an `ArduinoJson::Allocator*` from the caller, the HomeVerseFile way.
2. **`src/util/TaskWatchdog.h` does not build on the host either** (`<esp_task_wdt.h>`). A pass
   that feeds the watchdog needs it behind a seam, or behind a header the host suite can stub.
   `millis()` is stubbed to return 0 (`test/stubs/Arduino.h:20`), so timing logs compile on the
   host.
3. **"Missing" is not "unreadable" is not "zero".** `Storage.exists` false → unavailable-because-
   missing. A failed open, read or parse, or an unknown `v` → unavailable. A parsed file with no
   matching rows → zero. All three are distinct (spec §9). StudyPassageScan already makes the first
   two distinct (`:182-186`, `:119-130`).
4. **Never write.** `PassageFile::load` and `loadAdopting` rename a `.tmp` into place, so neither
   pass may use them. Use `readDocFromFileStreamed`, or a filtered equivalent, as StudyPassageScan
   does (`:115-119`).
5. **Not holding every text.**
   - **Counts.** A filter of `{"v":true,"p":[{"u":true,"t":true}]}` keeps no text at all. The
     document then holds about one small array per row; the passage *texts* are never allocated.
     Memory is that document plus one `uint16_t` count per picked tag.
   - **Tag passages.** ArduinoJson cannot filter by value, so a filtered document still holds
     `w`/`x`/`r` for **every** row, transiently, in PSRAM. That peak is bounded by the file
     (≤ ~204 KB, StudyPassageScan's cap), and the repo already accepts it daily (§2.4).
     - The alternative is to bound memory to one row: seek past `"p":[`, then `deserializeJson` one
       element at a time. That is a pattern this repo does not have.
     - A third option is two filtered passes: the first keeps `u`/`s`/`t` to find the matches; the
       second keeps texts. It does not help, because the second pass still keeps every row's text.
     - The choice is a spec decision. The brief's "must not hold the whole document's texts" is
       stated for the counts pass.
6. **Flash.** `PersistableStore.h:17-22` says each TU that instantiates `deserializeJson` emits its
   own ~0.5 KB `.isra` clones, which is why parsing is centralised there. A filtered
   `deserializeJson` is a different instantiation wherever it lives. Keep it to **one** helper in
   **one** TU, shared by both passes. `lib/Serialization/PersistableStore.*` is outside T2's
   declared files, so that helper belongs in the new `src/study/Preaching*` file.
7. **Row validity.**
   - A pass should skip rows `fromJson` would skip: an unparseable `u`, tag ids over `UINT16_MAX`.
   - It should count a repeated id in one row once.
   - For a row whose `w` `fromJson` would refuse, it can either use the row anyway or treat the file
     as unavailable. StudyPassageScan uses the row on its own terms (`:63-67`).
   - The spec should pick one. The brief's fallback ("`snippet` when `displayText` is empty, e.g. a
     legacy passage") implies using the row.
8. **The palette for pruning.** `StudyStore::palette()` (`StudyStore.h:40`) is filled only by
   `openPublication` (`StudyStore.cpp:72`). From Home, no publication need be open. So `save`
   should take a `const study::TagPalette&` and leave it to the caller (T3/T5) to supply one. Doing
   that needs `TagPaletteFile::load`, and its `Failed` must latch too.
9. **Locals under 256 B.** A `char path[sizeof(PASSAGES_DIR) + 16]` (`HomeVerse.cpp:36`) is fine.
   `JsonDocument` objects are small handles. 200 counts of `uint16_t` is 400 B, so they belong in a
   reserved `std::vector`, not on the stack.
10. **Test suite wiring.** `test/storage_io/CMakeLists.txt:9-27` lists its sources explicitly.
    - New tests, `src/study/PreachingTopicsFile.cpp` and the passes' `.cpp` are appended there.
    - `lib/StudyStore/StudyStore/TagPalette.cpp`, `PassageDoc.cpp`, `Unit.cpp` and `Utf8.cpp` are
      already linked (`:21-26`).
    - The top-level `test/CMakeLists.txt` needs no change. The suite is already registered, and the
      brief says so.

## 6. Tier

Standard holds. The change stays on the `data` surface. The new on-disk file and its format are
fixed by the owner's spec (D4, §7), so no format decision is open. Nothing migrates, and no existing
format, `PassageDoc::FORMAT_VERSION` or `TagPalette` changes. The one shared-file edit,
`SdPaths.h`, is in T2's declared files. No tier change.

## 7. Open questions for the spec phase

1. The tag-passages pass: a filtered whole-file PSRAM document (bounded, established), or
   element-at-a-time parsing (tighter, new pattern). §5.5.
2. A row whose `w` `PassageDoc::fromJson` would refuse: use the row with the `x` fallback, or treat
   the file as unavailable. §5.7.
3. The watchdog seam for host builds. §5.2.
