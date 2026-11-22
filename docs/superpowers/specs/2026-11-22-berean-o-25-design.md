# Design: berean-o-25 — read the preaching cards file (`PreachingHandouts`)

Bead `berean-o-25`, surface `data`, tier `standard`. Parent design:
`docs/superpowers/specs/2026-11-22-preaching-cards-design.md` §2–3. Parent plan:
`docs/superpowers/plans/2026-11-22-preaching-cards.md` Tasks 3–5 (`:519-988`), which this spec
adopts with the corrections in "Departures from the plan". Research:
`docs/superpowers/research/2026-11-22-berean-o-25-research.md`.

**Modelled on** `src/study/PreachingPassages.{h,cpp}` and its host test
`test/storage_io/PreachingPassagesTest.cpp`.

## Problem

The owner hands out printed preaching cards, and Predicación will gain a Tarjetas tab listing
them (parent spec, Goal). A Mac script writes the cards and every verse their articles cite to
`/preaching-cards.json`, and the owner uploads it over File Transfer. Nothing in the firmware reads
that file: `grep -rn 'PreachingHandouts\|preaching-cards' src lib test` returns nothing.

## Goal

A read-only reader, `PreachingHandouts`, with two passes:

1. `summaries(json, out)` — each handout's title and the number of verses `load()` would return,
   for the list.
2. `load(index, allocators, out)` — one handout's section headings and verses.

Both return `Ok | Missing | Unreadable | TooNew | OutOfMemory`, and `out` holds nothing unless the
result is `Ok`. Host tests prove it, and `docs/file-formats.md` documents the file.

## Non-goals

- Any caller. No UI, strings, `PreachingOrigin`, `PreachingContent` or `PreachingSession`
  change: those are the follow-up task. The new code is compiled but not called, so the linker
  drops it from the image until then (`-Wl,--gc-sections` in
  `~/.platformio/packages/framework-arduinoespressif32-libs/esp32s3/pioarduino-build.py`).
- Writing the file, adopting a `.tmp`, or any save budget. The device never writes it (parent
  spec D3).
- The generator script (parent plan Tasks 1–2), which lives outside this repo.
- Extracting a shared `JsonFileReader`; see A14.

## Architecture

Two new files in `src/study/`, beside the model:

```
src/study/PreachingHandouts.h     constants, Status, Summary, Verse, Handout, summaries(), load()
src/study/PreachingHandouts.cpp   readFiltered() shared by both passes, then the two walks
test/storage_io/PreachingHandoutsTest.cpp   9 tests, in StorageIoTest
```

The header is the plan's Task 4 Step 1 verbatim, the implementation its Step 2, the test its
Task 3 Step 1, and the docs section its Task 5, with "about 50 KB" changed to "about 65 KB".

### Public surface

| Name | Value | Why |
|---|---|---|
| `FILE_PATH` | `"/preaching-cards.json"` | A1 |
| `FORMAT_VERSION` | `1` | A3 |
| `MAX_FILE_BYTES` | `128 * 1024` | A4; public because the over-cap test sizes its file from it |
| `MAX_HREF_BYTES` | `63` | `ReaderEntryIntent::HREF_BYTES` (64) less its NUL (`src/activities/reader/ReaderEntryIntent.h:23,30,48-50`) |
| `MAX_HANDOUTS` | `200` | A7 |
| `MAX_TITLE_BYTES` | `96` | A8 |

```cpp
struct Summary { std::string title; uint16_t verses = 0; };
struct Verse   { uint16_t section = 0; std::string reference; std::string href; study::PassageText text; };
struct Handout { std::string title; std::vector<std::string> headings; std::vector<Verse> verses; };
```

`Verse::text` is a `study::PassageText` (`lib/StudyStore/StudyStore/PassageText.h:29-87`), the
same move-only, allocator-aware text `PreachingPassages::Passage` holds
(`src/study/PreachingPassages.h:34-39`), so the follow-up can hand it to the existing verse cards
without a copy. Headings are indexed by `Verse::section` rather than nested, so a handout is two
flat vectors, each reserved once.

## Data and control flow

### `readFiltered(doc, filter)` — shared, as `PreachingPassages.cpp:59-95`

1. Filter document overflowed → `OutOfMemory` (model `:60-63`).
2. `!Storage.exists(FILE_PATH)` → `Missing`, without logging (model `:66-67`; A5).
3. `openFileForRead` fails → `Unreadable` (model `:70`).
4. `file.size()` is 0 or over `MAX_FILE_BYTES` → `Unreadable`, logged, before any parse
   (model `:71-76`).
5. Stream: `BufferedFileReader` of 512 bytes (`lib/Serialization/BufferedFile.h:74-77`) under a
   one-byte `JsonFileReader`, into `deserializeJson(doc, reader, Filter(filter))` (model `:78-80`).
6. `NoMemory` or `doc.overflowed()` → `OutOfMemory`; any other error → `Unreadable` (model
   `:81-88`).
7. `v = doc["v"] | 0`. `v > FORMAT_VERSION` → `TooNew`. `v < 1`, or `cards` not an array →
   `Unreadable` (A3).

### `summaries(json, out)`

Filter keeps `v`, and per card `title` and `sections[].verses[].href` only, so no verse text is
parsed into the document. Then for each card, up to `MAX_HANDOUTS`: title through
`utf8SafeSummary(…, 96)` (`lib/Utf8/Utf8.h:32`), and a count of verses whose href passes
`hrefOpens` (non-empty, ≤ 63 bytes), saturating at `UINT16_MAX`. `out.reserve()` before the loop.
One `LOG_INF` with the count and `millis()` timing, as the model (`:185-186`).

### `load(index, allocators, out)`

Filter keeps `v`, and per card `title`, `sections[].heading` and `sections[].verses[].{ref,href,text}`.
The parse document uses `allocators.json`.

1. `index >= cards.size()` or `index >= MAX_HANDOUTS` → `Unreadable`, logged (A7).
2. Count the card's verses across its sections, reserve `headings` and `verses` once.
3. Per section, up to `UINT16_MAX` sections: push the heading (cut at 96 B), then per verse:
   - href fails `hrefOpens` → log the ref and href length, skip (A6);
   - else build `Verse{section, utf8SafeSummary(ref, 48), href, PassageText(allocators.text)}`
     and `assign` the text, whole (A8). A failed `assign` → `out = Handout{}`, `OutOfMemory`
     (model `:149-153`).
4. `LOG_INF` with the verse count and timing.

### Memory

Both passes allocate only inside the call; nothing is held after return beyond `out`.

- **Parse document.** The filter does not select by index, so `load()` parses every card's verse
  text: about 65 KB of strings for today's file, plus ArduinoJson's slot overhead, transiently.
  With `PsramJsonAllocator::json()` (`src/study/PsramJsonAllocator.h:18`) that lands in PSRAM; a
  nullptr would put it in internal SRAM, because ArduinoJson's pool blocks fall under the
  4,096-byte PSRAM routing threshold (`src/study/PreachingPassages.h:18-20`). See A9.
- **Output.** One handout's texts through `allocators.text`: PSRAM on the device via
  `PsramJsonAllocator::passageDoc()` (`:22`). The largest card, 77 verses, includes the
  9,153-byte Levítico 13 text.
- **Internal SRAM, while a handout is open.** Only `Verse::text` goes through the allocator.
  `title`, `headings`, and each verse's `reference` and `href` are `std::string` on the default
  heap. An href such as `1001061144-split6.xhtml#chapter6_verse34` (40 B) is past the small-string
  buffer, so that is one internal-SRAM allocation per verse: about 77 for the largest card, a few
  KB, held until the caller drops the handout. This is deliberate, and the same trade the model
  makes with `Passage::reference` (`src/study/PreachingPassages.h:35`). These are short, bounded
  strings (≤ 63, 48 and 96 bytes), and the follow-up copies the href into
  `ReaderEntryIntent::href`.
- **Stack.** The largest local is the `BufferedFileReader` object; its 512-byte buffer is
  heap-allocated with `makeUniqueNoThrow` (`BufferedFile.h:77`). Locals stay under 256 B.
- **Limits.** ArduinoJson 7.4.2 holds strings up to 65,535 bytes on a 32-bit target
  (`Configuration.hpp:140-145` in the fetched source); nesting is 7 levels against a default limit
  of 10 (`:93`).

## Error handling

| Condition | Status | Logged | `out` |
|---|---|---|---|
| No file | `Missing` | no | empty |
| Open fails | `Unreadable` | by `openFileForRead` | empty |
| 0 bytes, or > 128 KiB | `Unreadable` | yes | empty |
| Malformed JSON, truncated JSON | `Unreadable` | yes | empty |
| `v` absent, 0, negative, non-numeric | `Unreadable` | yes | empty |
| `v` > 1 | `TooNew` | yes | empty |
| `cards` absent or not an array | `Unreadable` | yes | empty |
| `load` index past the end or ≥ 200 | `Unreadable` | yes | empty |
| Filter or parse out of memory | `OutOfMemory` | yes | empty |
| A verse text cannot be allocated | `OutOfMemory` | yes | empty, earlier verses released |
| A verse's href empty or > 63 B | that verse skipped | in `load` | the rest |

No path writes, renames or deletes anything on the card.

## Assumptions — each a decision the review can attack

- **A1. The file is at the SD root, as a constant in the reader's header, not in `SdPaths.h`.**
  File Transfer refuses protected paths (`src/network/FileRoutes.cpp:124`), which rules out
  `/.berean/` for a file the owner uploads (parent spec D1). `SdPaths.h` holds only paths under
  the firmware's own roots (parent spec §2).
- **A2. Read-only.** No atomic write, budget, `.tmp` adoption or owning writer task: the device
  never writes the file (parent spec D3). CLAUDE.md's store rules 1, 2 and 5 are about writes;
  rules 3 (stream past ~40 KB) and 4 (refuse a newer version) are met.
- **A3. A newer version is its own status, `TooNew`, not `Unreadable`.** The model folds both into
  `Unreadable` through `persist::isKnownFormatVersion` (`src/study/PreachingPassages.cpp:89-92`,
  `lib/Serialization/FormatVersion.h:14-16`), which cannot tell the two apart. The repo already
  separates them elsewhere: `BibleSearch::IndexReader::Status`
  (`lib/BibleSearch/BibleSearch/IndexReader.h:34`) and
  `BibleSearchFingerprintFile::Load` (`src/study/BibleSearchFingerprintFile.h:48`). The follow-up
  UI shows the same message for both (parent spec §4); the split is for the log and is required by
  the acceptance criteria.
- **A4. The cap is 128 KiB, checked on `file.size()` before parsing.** Today's file is 65,151
  bytes. It is over `SDCardManager::readFile`'s 50,000-byte truncation (CLAUDE.md "Storage
  discipline"), so the reader streams rather than calling `Storage.readFile`. The generator
  refuses to write past the same cap.
- **A5. A missing file is ordinary and is not logged.** `Storage.exists` is checked before opening
  because a failed open logs (model `:66-67`). A card with no cards file is the default state.
- **A6. A verse whose href the reader cannot open is skipped, by both passes.** Empty, or over 63
  bytes, which would not fit `ReaderEntryIntent::href`. `summaries()` counts with the same
  `hrefOpens` test, so a summary's count always equals `load()`'s verse count for that card. Only
  `load()` logs a skip, once per open, not once per list render.
- **A7. At most 200 handouts.** `summaries()` stops at 200; `load()` refuses an index ≥ 200 as
  `Unreadable`. 200 fits the `uint8_t` index the follow-up adds to `PreachingOrigin`
  (`src/activities/preaching/PreachingOrigin.h:8,11`, where `question` is already a `uint8_t`).
  An index past the end means the file changed under the caller; `Unreadable` lets the UI show
  its unreadable-cards message.
- **A8. Titles and headings are cut at 96 bytes, refs at 48, texts never.** Through
  `utf8SafeSummary`, which also collapses whitespace and never splits a UTF-8 sequence
  (`lib/Utf8/Utf8.h:25-32`). 48 is `PassageDoc::MAX_REFERENCE_BYTES`
  (`lib/StudyStore/StudyStore/PassageDoc.h:44`), the cap the model applies to a reference
  (`src/study/PreachingPassages.cpp:143`). A verse text is whole, as the model's `w` is
  (`:145-147`) and as the parent spec requires.
- **A9. `load()` parses the whole file, not just the card at `index`.** ArduinoJson's filter
  applies one shape to every array element and cannot select by position. The cost is about
  65 KB of transient PSRAM; skipping to the n-th card in a hand-written streaming parser would be
  a new pattern this repo does not have, for a saving that does not matter at this size.
- **A10. Only the top level is validated.** Inside a card, an absent `title` reads as empty, an
  absent or non-array `sections` or `verses` as none, an absent `heading` as empty. The model is
  lenient the same way below its top-level check (`src/study/PreachingPassages.cpp:98,143-148`).
  The generator is the only writer, and a malformed card is better shown thin than hiding the
  other cards.
- **A11. A section with no openable verses keeps its heading.** `load()` pushes every section's
  heading, so `headings.size()` is the section count and a `Verse::section` is always a valid
  index. Whether to draw a heading with nothing under it is the follow-up's call.
- **A12. Counts saturate.** Verse counts at `UINT16_MAX`, sections at `UINT16_MAX`, as the model
  saturates its counts (`src/study/PreachingPassages.cpp:182`). Unreachable under the 128 KiB cap.
- **A13. The reader runs on the caller's task and takes no lock of its own.** Every `HalFile`
  call takes `storageMutex` (CLAUDE.md "SdFat is not thread-safe"), and there is no writer to
  coordinate with. The follow-up calls it from the loop task, as `PreachingSession.cpp:151` and
  `PreachingContent.cpp:81` call the model.
- **A14. `JsonFileReader` is copied file-local, a third time.** The model's comment records the
  same choice against `PersistableStore.cpp`'s twin (`src/study/PreachingPassages.cpp:27-28`).
  Extracting it would touch two unrelated files for a 10-line class; out of scope for this bead.
- **A15. No `isAvailable` helper.** The model has one because its UI treats `Missing` and
  `Unreadable` alike (`src/study/PreachingPassages.h:25-27`). Tarjetas does not: a missing file
  says where to put it, an unreadable one says it cannot be read (parent spec §4).

## Departures from the plan

1. **"about 65 KB", not "about 50 KB"** in `docs/file-formats.md` (plan `:986`): the generated
   file measures 65,151 bytes (brief).
2. **Whole-suite command.** Plan Task 4 Step 4 runs `ctest --test-dir build/test -R StorageIo`,
   which matches no test and exits 0 (research, "Trap in the plan's verification command"). Run
   `./build/test/storage_io/StorageIoTest` instead.
3. **Whole-chapter labels in the docs.** The `ref` bullet of the `docs/file-formats.md` section
   (plan `:980`) adds: a whole-chapter citation is labelled with the chapter alone, e.g.
   `Salmo 51` (brief). The reader treats it like any other ref.
4. **Two boundaries the plan's tests leave unproved** (spec review 0, MINOR 1). These extend
   existing tests, so the count stays at 9:
   - `AVerseWhoseHrefCannotOpenIsSkipped` adds a verse whose href is exactly `MAX_HREF_BYTES`
     (63) bytes and expects it kept. The summary count becomes 2, and `load()` returns that verse
     and `C`. This pins the `<=` in `hrefOpens`, which an off-by-one would otherwise pass.
   - `ATextThatCannotBeAllocatedIsOutOfMemory` adds a parse-OOM case: a JSON allocator that fails
     every allocation, the model's `CountdownJsonAllocator` with `remaining = 0`
     (`test/storage_io/PreachingPassagesTest.cpp:29-42,185-189`), expects `OutOfMemory` and an
     empty handout.

Everything else in Tasks 3–5 is taken as written.

## Testing strategy

TDD, in the plan's order: the test file and CMake lines first, built to fail on the missing
header; then the reader; then the docs.

Host, `test/storage_io/PreachingHandoutsTest.cpp` in `StorageIoTest`
(`test/storage_io/CMakeLists.txt:9-31`), against `HalStorageFake`
(`test/stubs/HalStorageFake.h:17,23`). Fixture text is made up: the repo is public, so no NWT
text goes in a fixture.

| Test | Proves |
|---|---|
| `SummariesListEveryHandoutWithItsVerseCount` | titles and counts, file order |
| `LoadsOneHandoutsSectionsAndVerses` | headings, `section` indexes, ref, href, text |
| `ASingleArticleHandoutHasOneEmptyHeading` | an empty heading survives; a `"3:4, 5"` ref is kept |
| `AMissingFileIsMissing` | both passes |
| `ANewerFormatIsRefusedAsTooNew` | A3 |
| `UnreadableFilesAreUnreadable` | 0 bytes, `v` 0, `v` absent, `cards` absent, truncated JSON, `MAX_FILE_BYTES + 1` |
| `AnIndexPastTheEndIsUnreadable` | A7 |
| `AVerseWhoseHrefCannotOpenIsSkipped` | A6 in both passes: empty and 64-byte hrefs skipped, a 63-byte href kept |
| `ATextThatCannotBeAllocatedIsOutOfMemory` | a failed `assign`, and a failed parse allocation, each leave nothing partial |

The OOM test injects a failing `TextAllocator` and a failing `ArduinoJson::Allocator`, the
model's mechanisms (`test/storage_io/PreachingPassagesTest.cpp:185-193`). Releasing the earlier
verses on a later failure is not tested separately: `out = Handout{}` destroys the vector, and
each `PassageText` frees itself (`lib/StudyStore/StudyStore/PassageText.h:33,72-76`).

Commands:

```
cmake --build build/test --target StorageIoTest -j
ctest --test-dir build/test -R PreachingHandouts --output-on-failure   # 9 pass
./build/test/storage_io/StorageIoTest                                   # all pass: the base's count + 9
pio run -e x4pro                                                        # grep output for PreachingHandouts: no warnings
./bin/clang-format-fix                                                  # whole tree, after staging the new files
```

The new `.cpp` is a new translation unit, so pio's cache cannot hide its warnings. The formatter
skips untracked files, so stage the new files before running it.

On the device: nothing to verify in this bead, since nothing calls the reader. The owner's device
pass belongs to the follow-up that adds Tarjetas.
