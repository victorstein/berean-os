# Issue #98 spec review, pass 0

**Spec:** `docs/superpowers/specs/2026-09-26-issue-98-design.md`
**Against:** issue #98 (`gh issue view 98 --repo victorstein/berean-os`), research note
`docs/superpowers/research/2026-09-26-issue-98-research.md`, and the tree at `c22f1311`.

## Summary

The spec does what the issue asks. It keeps the `.tmp`, puts the read / adopt / promote sequence in
one `lib/Serialization` function, and host-tests the issue's exact case. I checked every assumption
(A-1 through A-14) against the code and none of them is wrong. The findings below are all MINOR:
comments the spec forgets to update, one error-handling claim that does not hold for the passages
reader, and one rationale that contradicts the spec's own body. None of them reverses a decision or
changes scope.

## What was verified and holds

- **The five delete arms and the keep arm.** They are at `ChapterCompletionFile.cpp:55-57`,
  `PassageFile.cpp:91-93`, `TagPaletteFile.cpp:55-57`, `BookmarkFile.cpp:78-80`,
  `HighlightFile.cpp:64-66` and `PersistableStore.cpp:90-102`, as cited.
- **The SD facts behind the policy.** `SDCardManager::writeFile` removes the destination before it
  writes (`freeink-sdk/.../SDCardManager.cpp:282-284`). `readFile` returns `""` both when the card is
  not initialised (`:191-194`) and when the open fails (`:196-199`).
- **A-3, the alias.** All five enums are `{Loaded, Empty, RecoveredFromTemp, Failed}` in that order.
  `grep -rn LoadResult src lib test` finds only qualified value comparisons and one declared local
  (`EpubReaderBookmarksActivity.cpp:34`). There are no overloads on two different `LoadResult`
  types, which the alias would turn into redefinitions, and no forward declarations. Of the host
  tests, only `test/storage_io/TagPaletteFileTest.cpp` includes one of the five headers, and its
  include path already has `lib/Serialization` (`test/storage_io/CMakeLists.txt:22`). The new
  `#include <TempAdoption.h>` therefore resolves.
- **A-5, the passages reader.** The stub and the real HAL both have
  `openFileForRead(const char*, const char*, HalFile&)` (`test/stubs/HalStorage.h:45`,
  `lib/hal/HalStorage.h:40`). A static member such as `readDocFromFileChecked` and a function in an
  anonymous namespace such as `readInto` both convert to a plain `DocReader` pointer.
- **A-6, the acceptor.** Four stores have `bool fromJson(JsonVariantConst)` (`ChapterCompletion.h:50`,
  `TagPalette.h:65`, `PassageDoc.h:95`, `HighlightDoc.h:69`). Bookmarks use a free function
  (`BookmarkDoc.h:50`). A captureless lambda converts to `DocAcceptor`. ArduinoJson 7.4.2 has
  `JsonDocument::operator JsonVariantConst() const` (`JsonDocument.hpp:332`).
- **A-8, one document.** Both readers return before they touch `doc` when the path is `Missing`
  (`PersistableStore.cpp:48-50`, `PassageFile.cpp:32`).
- **A-13, the passages test.** The fake caps `readFile` at 50,000 bytes (`HalStorageFake.cpp:25`),
  so a `.tmp` over 50,000 bytes that is recovered really does prove the streaming reader ran.
  `PassageDoc::SAVE_BYTE_BUDGET` is 200,000 (`PassageDoc.h:31`), so a document of that size is
  valid. `HalFile::size`, `read(void*, size_t)` and `read()` are all defined in the fake
  (`HalStorageFake.cpp:186,209,219`).
- **A-14, the shared-file list.** `test/CMakeLists.txt:102-103` already registers both suites.
  `.claude/agents/data-dev.md:22-27` lists only `test/CMakeLists.txt`, the translation YAMLs and
  `src/main.cpp` as report-don't-edit.
- **A-11, what keeping buys.** This limit is stated honestly. On a transient read failure of the
  `.tmp`, the palette and passages loaders (`StudyStore.cpp:43-53`) do latch nothing, so a save
  in the same session still truncates the `.tmp`. The spec says so and scopes it out, and the issue
  asks only for "keep".

## Findings

### MINOR 1. Two `PersistableStore.h` comments go stale, and the spec updates only one of them

**Claim.** §Call sites updates `PersistableStore.h:73-74` and nothing else in that header.

**Problem.** `PersistableStore.h:76-78` says "readDocFromFileChecked stays for callers that must
read literally the path they name -- the three study files pass it their own `<path>.tmp`". After
this change, no store calls it with a `.tmp` path. It becomes the default `DocReader` passed to
`loadAdopting`. The "three" was already wrong (there are five).

`PersistableStore.h:80-87` calls `readDocFromFileAdopting` "the first read path in this firmware
that RENAMES". It lists which adopting files rely on single-task ownership for safety. After the
change, `loadAdopting` is the renaming read for the five study and annotation files, but the spec
gives its declaration no concurrency contract. §Concurrency only points back at the old comment.
`test/storage_io/CMakeLists.txt:1-3` also still describes the suite as covering "TagPaletteFile's
load/save" only.

**Evidence.** `lib/Serialization/PersistableStore.h:76-88` and `test/storage_io/CMakeLists.txt:1-5`.

**Fix.** Add these to the §Call sites edit list:
- Rewrite `:76-78` so it says `readDocFromFileChecked` is the default `DocReader` for
  `loadAdopting`.
- Move the renaming-read hazard in `:80-87` so it covers both entry points. For example, state it
  once on `loadAdopting` and have `readDocFromFileAdopting` refer to it, and name the five files
  alongside the three `/.berean/` files.
- Refresh the header comment of the storage_io `CMakeLists.txt`.

### MINOR 2. "The reader has already logged" is false for an empty passages `.tmp`

**Claim.** §Error handling: "Unusable `.tmp` returns `Empty` with no log from the helper; the reader
has already logged the read or parse error (`PersistableStore.cpp:53,58`, `PassageFile.cpp:36,44`)."

**Problem.** `readInto` returns `Unreadable` for a zero-length file and logs nothing
(`PassageFile.cpp:39`). A zero-byte `.tmp` is the most likely leftover of an interrupted write:
`writeFile` creates the file, then the power fails before the print. For passages, that `.tmp` is
now kept with no serial line at all. Today it is at least visibly removed. Adding a log to the
shared core would break A-7's promise that the `readDocFromFileAdopting` log lines are unchanged,
so the core is the wrong place for the fix.

**Evidence.** `src/study/PassageFile.cpp:39`: `if (file.size() == 0) return classifyDocRead(true, true, false);`
has no `LOG_ERR` before it. `readDocFromFileChecked` does log the equivalent case
(`PersistableStore.cpp:52-53`).

**Fix.** Either add `LOG_ERR(MODULE, "%s is empty", path)` to that branch of `readInto`, which is a
one-line change in a function the spec already edits under A-5, or correct the claim to name the
exception.

### MINOR 3. A-2's stated reason contradicts §Architecture

**Claim.** A-2 says the helper goes in `PersistableStore.cpp` "because `PersistableStore.cpp` is the
one TU that holds the JSON parser (`PersistableStore.h:14-22`)".

**Problem.** §Architecture then says: "The core calls whatever reader it is given, so it does not
itself instantiate `deserializeJson`". It gives the real reason: the core has to be shared with
`readDocFromFileAdopting` (A-7) without a new header. The assumption table, which is what a reviewer
or a plan writer reads first, gives the reason the body disowns.

**Evidence.** Spec lines 75 and 134-138.

**Fix.** Change A-2's rationale to "because the private core must be shared with
`readDocFromFileAdopting` (A-7), which already lives there".

### MINOR 4. The device recipe says "at boot" when the removal happens when a publication opens

**Claim.** §What only the device can verify: "Boot and open the tag list … Today it is removed at
boot."

**Problem.** Outside a migration, `TagPaletteFile::load` runs from `StudyStore` when a publication is
opened (`StudyStore.cpp:43`). A migration only runs when legacy highlight files exist
(`MigrationRunner.cpp:195-196,208`). The tag list is a reader activity (`TagFilterActivity`, under
`src/activities/reader/`). So the `.tmp` goes away when a book opens, not at boot, and a tester who
checks the card right after boot without opening a book sees nothing either way.

**Evidence.** `src/study/StudyStore.cpp:43`, `src/study/MigrationRunner.cpp:195-209`.

**Fix.** Word the recipe as "boot, open any book and open its tag list". Change "removed at boot" to
"removed when a publication is opened".

VERDICT: CLEAR
