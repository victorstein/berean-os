# Design: berean-o-15 (t2): the preaching topics store and the `bible.json` passes

Surface: `data`. Parent spec: `docs/superpowers/specs/2026-10-14-preaching-tool-design.md` (§3, §4,
§7, §9, D4, D5). Research: `docs/superpowers/research/2026-10-15-berean-o-15-research.md`. Every
`file:line` below was read on 2026-10-15 at `eecbf1d1`.

Modelled on:

- **Store:** `src/study/TagPaletteFile.{h,cpp}` and its test `test/storage_io/TagPaletteFileTest.cpp`.
  `PassageFile::save` (`src/study/PassageFile.cpp:28-46`) adds the `overflowed()` check.
- **Session latch:** `StudyStore::saveDisabled_` (`src/study/StudyStore.cpp:72-83,107-108,152,243,259`).
- **Read passes:** `study_passage_scan::pickFromFile` / `offerFile`
  (`src/activities/boot_sleep/StudyPassageScan.cpp:101-137,181-199`).
- **Allocator injection for host tests:** `HomeVerseFile` (the firmware passes
  `PsramJsonAllocator::json()`, `src/activities/launcher/HomeVerse.cpp:94`; the host passes
  `nullptr`, `test/home_layout/HomeVerseFileTest.cpp:68`). Its `CountdownJsonAllocator` (`:42`)
  covers OOM.

## Problem

T3 and T5 need two things from `src/study`, and neither exists today (`grep -rni preaching src lib`
returns nothing).

**1. Somewhere to keep the tags the owner picks as preaching topics.** That file is user data, so
every rule in CLAUDE.md's storage discipline applies:

- atomic write;
- a byte budget checked before writing;
- a version a future build refuses;
- saving latched off after a failed load.

**2. Two read-only passes over the Bible's tagged passages, `/.berean/passages/bible.json`.**

- **Counts:** per picked tag, how many Bible passages carry it.
- **One tag's passages:** in Bible order, with their texts.

The file can be ~204 KB (`StudyPassageScan.cpp:28`; `PassageDoc.h:38`), so both passes have to
stream it. Neither may ever write it.

## Goal

- **`PreachingTopicsFile`** (`src/study/PreachingTopicsFile.{h,cpp}`). One object owns:
  - the load of `/.berean/preaching.json`;
  - the picked ids;
  - the load-refused latch;
  - the save.

  A caller cannot save without the latch being checked.
- **`PreachingPassages`** (`src/study/PreachingPassages.{h,cpp}`). It has two functions, `countByTag`
  and `passagesWithTag`. Each streams `bible.json` read-only and returns a status that tells
  "missing", "unreadable" and "out of memory" apart from a successful result with zero passages.
- **`sdpaths::PREACHING_FILE`** in `lib/Serialization/SdPaths.h`.
- Host tests in `test/storage_io/` for every case the brief lists.

## Non-goals

- Any UI, string or `tr()` key. Those are T3 and T5.
- Any change to `tags.json`, the passages files, `PassageDoc::FORMAT_VERSION` or `TagPalette` (parent
  spec, Non-goals).
- Any change to `lib/Serialization/PersistableStore.*`. It is outside T2's declared files
  (parent spec §10).
- Counting or listing passages from any publication other than the Bible (D5).
- Display order of the picked tags. `TagPalette::activeIdsByName` (`TagPalette.h:61`) decides it, in
  T3.
- Loading the tag palette. The caller supplies it (A9).

## Architecture

```
lib/Serialization/SdPaths.h      + PREACHING_FILE = "/.berean/preaching.json" (+ static_assert)
src/study/PreachingTopicsFile.h  class PreachingTopicsFile   (load / tags / saveRefused / save)
src/study/PreachingTopicsFile.cpp
src/study/PreachingPassages.h    namespace PreachingPassages (Status, TagCount, Passage,
src/study/PreachingPassages.cpp                               countByTag, passagesWithTag)
test/storage_io/PreachingTopicsFileTest.cpp
test/storage_io/PreachingPassagesTest.cpp
test/storage_io/CMakeLists.txt   + the two tests and the two .cpp sources
```

Neither file includes `PsramJsonAllocator.h`, `TaskWatchdog.h` or anything else ESP-IDF-only. Both
build in the existing `StorageIoTest` executable, against `test/stubs`.

### 1. `sdpaths::PREACHING_FILE`

Beside `HOME_VERSE_FILE` (`SdPaths.h:27`), under the same "Not under PASSAGES_DIR" comment (`:26`),
which already covers it:

```cpp
inline constexpr char PREACHING_FILE[] = "/.berean/preaching.json";
static_assert(isUnder(PREACHING_FILE, BEREAN_DIR));
```

It also gets `static_assert(!isUnder(PREACHING_FILE, PASSAGES_DIR))`. That turns the brief's "must
not sit under `/.berean/passages/`" into a compile error. No other path asserts a negative today. It
is one line, and the reason is the one `:26` gives.

### 2. `PreachingTopicsFile`

```cpp
// src/study/PreachingTopicsFile.h
#pragma once
#include <SdPaths.h>
#include <TempAdoption.h>
#include <cstddef>
#include <cstdint>
#include <vector>
#include "StudyStore/TagPalette.h"

class PreachingTopicsFile {
 public:
  static constexpr int FORMAT_VERSION = 1;
  static constexpr size_t SAVE_BYTE_BUDGET = 2048;
  static constexpr const char* PATH = sdpaths::PREACHING_FILE;

  using LoadResult = AdoptedLoad;
  enum class SaveResult : uint8_t { Ok, TooLarge, WriteFailed, Refused };

  explicit PreachingTopicsFile(size_t saveBudget = SAVE_BYTE_BUDGET);

  LoadResult load();
  const std::vector<study::TagId>& tags() const;
  bool saveRefused() const;
  SaveResult save(const std::vector<study::TagId>& picked, const study::TagPalette& palette);
};
```

**`load()`** calls `PersistableStoreBase::loadAdopting(PATH, &readDocFromFileChecked, accept, this)`,
the same call as `TagPaletteFile.cpp:18-23`. `accept` parses into a **local** vector and only
assigns `tags_` once the whole document has passed. That is `PersistableStore.h:153-155`'s rule:
"fromJson returns false … before touching any member".

`accept` refuses, returning false, when any of these holds:

- the root is not an object;
- `persist::isKnownFormatVersion(doc["v"] | 0, FORMAT_VERSION)` is false (`FormatVersion.h:14-16`):
  `v` is absent, 0, negative, or over 1;
- `tags` is not an array;
- `tags` has more than `TagPalette::MAX_ACTIVE_TAGS` (200) elements (`TagPalette.h:37`), counted
  before dedupe;
- any element is not an unsigned integer in `1..UINT16_MAX`.

Otherwise it dedupes, keeping the first occurrence, and accepts. Ids that are not active stay in
the list on load (A6).

After the call:

- `Loaded` or `RecoveredFromTemp` → `tags_` holds the file's ids, and `saveRefused_ = false`.
- `Empty`, a missing file or a `.tmp` it could not use → `tags_` is cleared, and
  `saveRefused_ = false`.
- `Failed`, an unreadable, unparseable or refused file → `tags_` is cleared,
  `saveRefused_ = true`, and the load logs `LOG_ERR`.

**`save(picked, palette)`:**

1. `saveRefused_` → `Refused`, with a `LOG_ERR`. Nothing is touched.
2. Build the list to keep from `picked`: drop ids that are not `palette.isActive` (`TagPalette.h:51`,
   which is false for `UNLABELLED` and unknown ids, `TagPalette.cpp:67-72`), and drop duplicates.
   Reserve `picked.size()` first. The result is at most 200 ids, because `isActive` bounds it at
   the palette's active count.
3. Build `{"v":1,"tags":[...]}` in a default `JsonDocument`. If `overflowed()`, return
   `WriteFailed` (the `PassageFile.cpp:32-37` check).
4. If `!persist::fitsBudget(measureJson(doc), saveBudget_)` (`SaveBudget.h:26`), return `TooLarge`
   with a `LOG_ERR`. Nothing is written.
5. Write through `PersistableStoreBase::writeDocToFileAtomic(PATH, doc)` and nothing else. On
   `true`, `tags_` becomes the kept list and the result is `Ok`. On `false`, the result is
   `WriteFailed`.
6. Any result except `Ok` leaves `tags_` exactly as it was. That is the parent spec §7's "keeps the
   previous selection".

**A new object refuses to save until `load()` has run.** `saveRefused_` starts `true` (A3).

### 3. `PreachingPassages`

```cpp
// src/study/PreachingPassages.h
namespace PreachingPassages {

enum class Status : uint8_t { Ok, Missing, Unreadable, OutOfMemory };

// Missing and Unreadable are both "unavailable" to the UI (parent spec §9); kept apart for the log.
constexpr bool isAvailable(Status s) { return s == Status::Ok; }

struct TagCount {
  study::TagId tag;
  uint16_t passages;
};

struct Passage {
  std::string reference;   // <= PassageDoc::MAX_REFERENCE_BYTES
  study::PassageText text;  // "w", else "x"; on the caller's text allocator
  study::Unit start;
  uint16_t spine;           // documentSpine, a hint
};

// One count per entry of `tags`, in the same order, zero included. `json` is the
// parse document's allocator (nullptr: ArduinoJson's default).
Status countByTag(const std::vector<study::TagId>& tags, ArduinoJson::Allocator* json,
                  std::vector<TagCount>& out);

// Every Bible passage carrying `tag`, sorted by spine, then start Unit.
Status passagesWithTag(study::TagId tag, const study::PassageDoc::Allocators& allocators,
                       std::vector<Passage>& out);

}  // namespace PreachingPassages
```

On the device, T3 passes `PsramJsonAllocator::json()` and `PsramJsonAllocator::passageDoc()`
(`PsramJsonAllocator.h:18,22`). The host passes `nullptr` and `PassageDoc::Allocators{}`, which
default to malloc (`PassageDoc.h:55-58`, `PassageText.h:22-25`).

**The path** is built as `HomeVerse.cpp:31-34` builds it:
`snprintf(buf, sizeof(buf), "%s/%s.json", sdpaths::PASSAGES_DIR, study::BIBLE_PUB_KEY)` into
`char[sizeof(sdpaths::PASSAGES_DIR) + 16]` (`:36`), which is 34 B on the stack.

**The shared read**, in one anonymous-namespace function, `readFiltered(path, doc, filter)`:

1. `!Storage.exists(path)` → `Missing`. No error log; a Bible with nothing tagged is ordinary
   (`StudyPassageScan.cpp:182-184`).
2. `Storage.openFileForRead` fails → `Unreadable`.
3. `file.size() == 0`, or more than `PassageDoc::SAVE_BYTE_BUDGET + 4096` → `Unreadable`. That is
   `StudyPassageScan.cpp:28,104-108` and `PersistableStore.cpp:149-152`.
4. `deserializeJson(doc, reader, DeserializationOption::Filter(filter))`. The reader is a local
   `int read()` adapter over a `serialization::BufferedFileReader` of 512 B. That is
   `PersistableStore.cpp:16,22-33`, copied because that one lives in an anonymous namespace (A11).
   - `NoMemory`, or `doc.overflowed()` → `OutOfMemory`.
   - Any other error → `Unreadable`.
5. `persist::isKnownFormatVersion(doc["v"] | 0, PassageDoc::FORMAT_VERSION)` false, or `doc["p"]`
   not an array → `Unreadable` (`StudyPassageScan.cpp:126-130`).

The filter document goes on the default heap. It is a few hundred bytes of internal SRAM, freed on
return. A filter that overflowed → `OutOfMemory`.

**Row rules**, shared by both passes and matching `PassageDoc::fromJson` (`PassageDoc.cpp:310-329`)
and its private `normaliseTags` (`:17-27`):

- A row whose `"u"` does not parse with `study::unitFromCompact` is skipped (`:311-312`).
- The row's tags:
  - read each `t` element as `| 0u`;
  - drop 0 and anything over `UINT16_MAX`;
  - dedupe;
  - stop at `PassageDoc::MAX_TAGS_PER_PASSAGE` (8) distinct ids.

  They go into a local `study::TagId[8]`, 16 B on the stack. A row counts once per tag even if the
  file repeats that tag in the row.

**`countByTag`:**

- Filter: `{"v":true,"p":[{"u":true,"t":true}]}`. No text field is kept, so no passage text is ever
  allocated (A4).
- `out` is cleared, reserved to `tags.size()`, and filled with `{tag, 0}` for every input entry
  before the read. On a failed status `out` is cleared.
- For each row, for each of its tags, a linear search of `out` adds one to the match. At 200 picked
  × 8 × rows this is integer work only. A count saturates at `UINT16_MAX`.
- It logs `LOG_INF("PREACH", "Counts: %u rows, %u tags, %lu ms", …)`. A failure logs `LOG_ERR`
  with the status.

**`passagesWithTag`:**

- Filter: `{"v":true,"p":[{"u":true,"s":true,"x":true,"w":true,"r":true,"t":true}]}`. The document
  is on `allocators.json`, which is PSRAM on the device (A5).
- The first walk counts matching rows, then `out.reserve(n)`. The second walk fills `out`:
  - `reference` = `utf8SafeSummary(r, PassageDoc::MAX_REFERENCE_BYTES)` (`PassageDoc.cpp:323`).
  - `text` = `PassageText(allocators.text)`, assigned `w` when non-empty. Otherwise it gets
    `utf8SafeSummary(x, MAX_SNIPPET_BYTES)` (`:322`), the snippet fallback (A7).
  - `start` = the parsed `u`.
  - `spine` = `s`, read as `fromJson` and `StudyPassageScan.cpp:92-93` read it: `0` when absent or
    over `UINT16_MAX`.
- If `PassageText::assign` returns false (`PassageText.h:53-67`), the pass returns `OutOfMemory`
  with `out` cleared, and logs `LOG_ERR`.
- The parse document is released, at scope exit, **before** the sort.
- `std::stable_sort` by `(spine, start)`, using `operator<(Unit, Unit)` (`Unit.cpp:38-51`).
  Stable, so two equal keys keep file order (A8).
- It logs `LOG_INF("PREACH", "Tag %u: %u of %u rows, %lu ms", …)`.

Neither pass calls `PassageFile::load`, `loadAdopting` or `readDocFromFileAdopting`, because those
rename a `.tmp` (`PersistableStore.cpp:184-197`). Neither opens anything for write.

## Data and control flow

```
T3 Mis temas tab opens
  PreachingTopicsFile topics;  topics.load()          -> tags() (may be empty), saveRefused()
  PreachingPassages::countByTag(topics.tags(), PsramJsonAllocator::json(), counts)
      Missing/Unreadable/OutOfMemory -> "no counts" (parent spec §9)

T3 topic opens (tag id)
  PreachingPassages::passagesWithTag(id, PsramJsonAllocator::passageDoc(), passages)
      Ok + empty  -> STR_PREACHING_TOPIC_EMPTY
      not Ok      -> "cannot read the verses"

T5 Elegir temas returns
  topics.save(picked, palette)
      Refused | TooLarge | WriteFailed -> popup; topics.tags() unchanged
      Ok                               -> topics.tags() == picked ∩ active, deduped
```

All of this runs on the Arduino loop task (A10). `HalStorage` takes `storageMutex` on every call
(CLAUDE.md, "SdFat is not thread-safe").

## Assumptions

Each one is a behavioural decision, open to review.

- **A1. The latch keys on `AdoptedLoad::Failed`, not on `persist::loadRefusedAfter`.**
  `loadRefusedAfter` does not latch on `Unreadable` or `ParseError` at a first load
  (`FormatVersion.h:28-31`). The brief requires "corrupt" to latch. `Failed` covers unreadable,
  unparseable and refused files (`TempAdoption.h:79-92`). That is what `StudyStore` keys on
  (`StudyStore.cpp:72-76`).
- **A2. The latch lasts as long as the `PreachingTopicsFile` object**, not as long as the process.
  `StudyStore`'s latch outlives a book (`:107-108`), because a singleton store saves across books.
  Here every save goes through an object whose own `load()` read the file. A file that was refused
  is refused again by the next object's `load()`, so it cannot be overwritten in between. T3 and T5
  must share one object for Predicación's lifetime. That falls out naturally: T5's picker returns
  to `PreachingActivity`, which owns the object.
- **A3. A new object refuses saves until `load()` runs.** Without this, a caller that forgot to load
  would write an empty or partial selection over a file it never read. That is the brief's "unable
  to save over a refused file by accident", widened to "over an unread file".
- **A4. Counts keep `u` as well as `t`.** `u` is needed to skip rows the store itself drops
  (`PassageDoc.cpp:311-312`). It costs one short string per row, and no passage text.
- **A5. `passagesWithTag` holds every row's text, transiently, in one filtered PSRAM document.**
  ArduinoJson cannot filter by value. The peak is bounded by the file cap (~204 KB). The repo
  already takes this peak once a day, unfiltered, for Home's verse (`StudyPassageScan.cpp:118-119`).
  The filter removes `k`, `f`, `fe`, `d`, `e`, `h` and `g`. The alternative, one element at a time
  behind a hand-written `"p":[` seek, is a parsing pattern this repo does not have. It would protect
  memory that is PSRAM and is released before the pass returns. Rejected; the brief's "must not hold
  the whole document's texts" is stated for the counts pass.
- **A6. Inactive ids survive a load and are dropped on the next save.** That is the parent spec
  §7's rule. Pruning on load would make the in-memory list differ from the file with nothing
  written, and T3 filters display by the active palette anyway.
- **A7. A row whose `w` `PassageDoc::fromJson` would refuse is used anyway, with the `x`
  fallback.** Examples: a legacy text outside 121..384 B, or `h` without `w`. This pass is a read
  of rows, not a load of the store. Treating the whole file as unavailable over one legacy row
  would hide every topic. A row with neither `w` nor `x` is kept with an empty text, as `fromJson`
  keeps it. T3 still has its reference and Unit.
- **A8. "Bible order" is `(documentSpine, start)`.** That is the brief's wording. `documentSpine`
  is a hint (`TaggedPassage.h:27-30`). For two passages in the same book, a stale hint after an
  edition change could misorder them. That is a display-order issue only, and `start` breaks the
  tie.
- **A9. `save` takes the palette from the caller.** `StudyStore::palette()` is filled only by
  `openPublication` (`StudyStore.cpp:72`; `StudyStore.h:40`). Opening Predicación from Home may
  have no publication open, so where the palette comes from is T3's call. The caller must not
  pass an empty palette produced by a `TagPaletteFile::load` that `Failed`: that would prune every
  pick. **T3/T5 must check that `Failed` and refuse to save**, as `MigrationRunner.cpp:210-211` does.
  This is the one hand-check left to the caller. It belongs to `TagPaletteFile`, not to this store.
- **A10. Loop task only.** `loadAdopting` renames, and is safe only single-task
  (`PersistableStore.h:106-114`). That comment's list of files is outside T2's files, so
  `PreachingTopicsFile.h` carries the same constraint in its own header comment.
- **A11. The filtered parse and its reader adapter live in `PreachingPassages.cpp`, not
  `PersistableStoreBase`.** `PersistableStore.*` is not T2's file. The filtered `deserializeJson` is
  a new template instantiation wherever it sits (`PersistableStore.h:17-22`), so it is kept to one
  call site shared by both passes. Expected flash cost: a few KB, against ~994 KB of headroom
  (parent spec, "Fonts and flash"). The `pio run` size line will show it.
- **A12. No watchdog feeding.** `StudyPassageScan` feeds it because fitting measures every word
  (`StudyPassageScan.cpp:34-35`). The work per row here is a few integer comparisons. The parse
  itself cannot be interrupted, in either file. Leaving it out keeps the passes host-buildable:
  `TaskWatchdog.h` needs `<esp_task_wdt.h>`. If the owner's serial timing (parent spec §9) shows a
  long pass, the caller can feed the watchdog around the call.
- **A13. A missing `bible.json` is `Missing`, which the UI shows as unavailable**, not as zero.
  That is the brief and parent spec §9, owner-settled. `Missing` stays distinct from `Unreadable`
  so a later UI change, or the log, can tell them apart.
- **A14. The budget is a constructor parameter, defaulting to `SAVE_BYTE_BUDGET`.** No pruned list
  can exceed 2048 B: at most 200 ids × 6 B + 16 ≈ 1.2 KB. So the refusal path is reachable only
  with a smaller budget, and the test constructs one. No production caller passes it.
- **A15. Load refuses a file of more than 200 ids before dedupe.** The brief says "a longer list on
  load is refused". The store never writes one, so any longer file was written by something else.

## Error handling

| Case | Result | Writes? | Log |
|---|---|---|---|
| `preaching.json` missing | `Empty`, `tags()` empty, saving allowed | no | none |
| `preaching.json` corrupt, unreadable, `v`>1, >200 ids, bad element | `Failed`, latched | never this session | `LOG_ERR` |
| Interrupted write left only `.tmp` | `RecoveredFromTemp` (renamed into place) | rename only | `LOG_INF` (`PersistableStore.cpp:188`) |
| Save while latched or before load | `Refused` | no | `LOG_ERR` |
| Save over budget | `TooLarge` | no | `LOG_ERR` |
| Save JSON OOM / write or rename fails | `WriteFailed`; old file intact (atomic) | `.tmp` only | `LOG_ERR` |
| `bible.json` missing | `Missing` | no | none |
| `bible.json` empty, over cap, open/parse error, unknown `v`, no `p` | `Unreadable` | no | `LOG_ERR` |
| Parse or text OOM | `OutOfMemory`, `out` empty | no | `LOG_ERR` |
| Picked tag in no passage | `Ok`, its count 0 | no | `LOG_INF` timing |

No path aborts. Every allocation is either an ArduinoJson allocation, reported through
`NoMemory`/`overflowed()`, or a `PassageText::assign`, which reports `false`. No bare `new`, no
`std::function`.

## Testing strategy

TDD against the Storage fake (`test/stubs/HalStorageFake.h`: `putFile`, `fileBytes`,
`failReadsOf`, `failWritesTo`, `failRenamesFrom`). Fixtures are short synthetic JSON, with no
publisher text. `SetUp` calls `storage_fake::reset()`, as `TagPaletteFileTest.cpp` does.

**`PreachingTopicsFileTest.cpp`:**

1. Round trip: save `{12,31,7}` with all three active → load gives `{12,31,7}`. The file is
   `{"v":1,"tags":[…]}`, and no `.tmp` remains.
2. A missing file → `Empty`, `tags()` empty, and `saveRefused()` false. A save then succeeds.
3. Newer version: `putFile` `{"v":2,"tags":[1]}` → `Failed`, `saveRefused()` true. `save` →
   `Refused`, and the file's bytes are unchanged.
4. Corrupt: `{"v":1,"tags":[` → `Failed`, latched, bytes unchanged. An unreadable file
   (`failReadsOf`) does the same.
5. Over the cap: 201 ids → `Failed`, latched. Exactly 200 → `Loaded`.
6. Malformed: a missing `v`, `tags` not an array, an element of `0`, `-1`, `70000` or `"a"` → each
   `Failed`.
7. Duplicates on load: `[5,5,9]` → `{5,9}`.
8. Pruning: a palette where 7 is retired and 99 was never allocated. Save `{12,7,12,99}` → the file
   and `tags()` hold `{12}`.
9. Budget: an object built with budget 10 → `TooLarge`, no file, and `tags()` unchanged.
10. A save failure keeps the selection: `failWritesTo(PATH + ".tmp")` → `WriteFailed`, `tags()`
    still the loaded list, and the old file's bytes are intact.
11. Save without load → `Refused`.
12. `.tmp` recovery: only `PATH.tmp` holds a valid document → `RecoveredFromTemp`, and saving is
    allowed.

**`PreachingPassagesTest.cpp`**, each test writing `/.berean/passages/bible.json` with `putFile`:

1. Counts: rows tagged `[3]`, `[3,4]` and `[4]`, picked `{3,4,5}` → `{3:2, 4:2, 5:0}`, in input
   order. Zero, one and several are all covered.
2. Counts skip a row with an unparseable `u`, and count a row tagged `[3,3]` once.
3. Tag passages, in Bible order: rows written out of order (spine 40 verse 2, spine 12, spine 40
   verse 1) → spine 12, then 40:1, then 40:2.
4. Text fallback: a row with `w` → the text is `w`. A row with no `w` but an `x` → the text is `x`.
   `reference`, `start` and `spine` come through.
5. A row not carrying the tag is not returned. A tag no row carries → `Ok` and empty.
6. A missing file → both passes return `Missing`, which is not `Ok` with zero.
7. Unreadable: `failReadsOf`, `{"v":5,"p":[]}`, `{"v":4}` and a truncated document → `Unreadable`.
8. OOM: a `CountdownJsonAllocator`-style allocator (`HomeVerseFileTest.cpp:42`) set to 0 →
   `OutOfMemory`, with `out` empty. A text allocator that returns `nullptr` → `OutOfMemory`.
9. Never writes: after each pass, `fileBytes` of `bible.json` is unchanged, and no `.tmp` exists
   beside it.

**Commands:**

```
cmake -S test -B build/test -DCMAKE_BUILD_TYPE=Release
cmake --build build/test --target StorageIoTest
./build/test/storage_io/StorageIoTest
```

The baseline is 69 passing. Then run `~/.platformio/penv/bin/pio run -e x4pro` (no new warnings,
and the flash delta recorded for A11) and `./bin/clang-format-fix`.

**On the device (the owner, via T3):**

- the `PREACH` timing lines for both passes on the real `bible.json`;
- `PsramJsonAllocator::logMemory` around a topic open, where internal SRAM should stay flat.
