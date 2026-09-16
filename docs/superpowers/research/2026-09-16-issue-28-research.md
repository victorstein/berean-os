# Bookmark save: what is measured, and what the fix cannot be

Investigation for issue #28, 2026-09-16, on `ded48d17`. Everything below was
measured or read; nothing is estimated from a struct. Later phases can treat it
as settled.

## 1. Reachable at a few hundred bookmarks, not thousands

The issue asked for this number before planning. A bookmark record costs
**≈206 serialised bytes**, and the set reaches the budget at **219 records**:

| Record shape | bytes/record | records to 45,000 | to the 50,000 cap |
|---|---|---|---|
| shallow xpath, no `vo` | 167 | 269 | 299 |
| typical (`DocFragment[27]/…/p[14]/text()[1].117`, `vo` present) | 206 | 218 | 242 |
| deep xpath, 4-digit `DocFragment`, Spanish summary | 251 | 179 | 199 |

Language does not move the figure: `summary` is capped at 72 **bytes** by
`utf8SafeSummary`'s default (`lib/Utf8/Utf8.h:32`), so a Spanish page yields
fewer characters, not more bytes. `xpath` is the only unbounded field
(`buildParagraphXPath`, `lib/ProgressMapper/ChapterXPathResolver.cpp:52-62`);
the rest are fixed-width scalars.

**The priority claim in the issue holds, and per-book scoping does not rescue
it.** The bookmark file is keyed on the book path
(`src/util/BookmarkUtil.cpp:10-12`), and this device's central book is a single
EPUB containing all 66 books — the Bible tile opens one file it finds by name
(`src/activities/launcher/LauncherActivity.cpp:93-99`). 219 bookmarks in "the
Bible" is one per 140 chapters, which a study device is built to invite. The
reader's own reserve is 16 (`src/activities/reader/EpubReaderActivity.cpp:55`),
so nothing in the UI discourages it either.

## 2. The loss chain, confirmed end to end rather than inferred

`SDCardManager::readFile` reads byte-by-byte under `maxSize = 50000` and returns
the short string with no error and no log
(`freeink-sdk/libs/hardware/SDCardManager/src/SDCardManager.cpp:202-210`).

Feeding the real serialiser a 62,021-byte bookmark document cut at 50,000 bytes:

```
cut at 50000 -> IncompleteInput   (243 of 301 records had parsed)
```

So the truncated read is a `ParseError` from `readDocFromFileChecked`
(`lib/Serialization/PersistableStore.cpp:55-59`), flattened to `false` by
`readDocFromFile` (`:63-65`), returned as `false` by `BookmarkFile::load` with
the vector already cleared (`src/util/BookmarkFile.cpp:11-19`). The partial
parse is discarded, which is the one merciful part: the file is not silently
half-loaded, it is wholly lost.

## 3. Criterion 4 cannot be satisfied inside `BookmarkFile`

The primary call site throws the result away:

```cpp
BookmarkFile::load(epub->getPath(), cachedBookmarks);   // EpubReaderActivity.cpp:1747
```

`addBookmark()` then writes that vector back at `:1801`. Returning a
`DocReadStatus`-shaped result from `load()` changes nothing until **both** call
sites act on it — the other is `EpubReaderBookmarksActivity.cpp:33`, which does
check, but only to `shrink_to_fit()`. The published contract conflates the two
states as well: `BookmarkFile.h:11-13` documents "a missing or empty bookmark
file yields an empty list and returns false", so today `Missing` and
`ParseError` are indistinguishable by design, not by oversight.

## 4. A refusal must roll back `cachedBookmarks`, or the UI lies

`addBookmark()` inserts the entry (`:1796`), sets `currentPageBookmarked = true`
(`:1798`), and only then saves (`:1801`). A save that refuses leaves the page
looking bookmarked and the popup still reading "Bookmark added"
(`:1291`) — and the bookmark is gone at the next open.
`study::PassageDoc::add` is the shape that gets this right: push, measure,
`pop_back()` on overflow (`lib/StudyStore/StudyStore/PassageDoc.cpp:32-36`).

The user-visible surface criterion 3 asks for **already exists**:
`showBookmarkMessage` + `GUI.drawPopup` (`:1290-1291`) needs a third state, not
new UI. What does not exist is the string. `STR_HIGHLIGHTS_TOO_LARGE` reads
"This book already has too many highlights" (`lib/I18n/translations/english.yaml:390`)
— wrong noun, and the YAML is another task's. Name the key needed in the
hand-back and leave the wiring out.

## 5. What a document at the budget costs in memory

Measured with the pinned ArduinoJson (v7.4.2, `test/CMakeLists.txt:28-33`) under
a counting allocator:

| | bytes |
|---|---|
| bookmark JSON at the budget (219 records) | 45,129 |
| `JsonDocument` pool while parsing it | 53,892 |
| the `String` holding the file, alive across `deserializeJson` (`PersistableStore.cpp:50-59`) | 45,129 |
| **concurrent heap on load** (split corrected below — NOT all internal) | **≈99,000** |
| same shape on save (pool 53,659 + serialised `String`) | ≈99,000 |

> **CORRECTED 2026-09-16, after `reviews/issue-28-spec-review-0.md`.** The byte
> counts above are right; the sentence that followed them — "ArduinoJson's
> default allocator is `malloc`, so this is internal SRAM, not PSRAM" — was
> wrong for this build, and the ≈99,000 figure must **not** be read as an
> internal-SRAM total. Corrected split below.

This firmware sets `CONFIG_SPIRAM_USE_MALLOC=y` and
`CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=4096`
(`~/.platformio/packages/framework-arduinoespressif32-libs/esp32s3/sdkconfig:2146-2147`;
`platformio.ini:91-113`'s `custom_sdkconfig` touches no `SPIRAM_*` key), and
`heap_caps_realloc_default` routes an allocation larger than that limit to
`MALLOC_CAP_SPIRAM` (`framework-espidf/components/heap/heap_caps.c`, the
`size <= malloc_alwaysinternal_limit` branch). The 45,129-byte Arduino `String`
grows by `realloc` (`framework-arduinoespressif32/cores/esp32/WString.cpp:212`),
so it lives in **PSRAM**. The `JsonDocument` pool does not: each variant pool is
at most 4,096 bytes (`ArduinoJson/Configuration.hpp:112-120`) and string nodes
are individually smaller, so every block takes the internal branch.

| at a 45,129-byte document | internal SRAM | PSRAM |
|---|---|---|
| load | ≈54 KB (pool) | ≈45 KB (the `String`) |
| save | ≈54 KB (pool) | ≈45 KB (the serialised `String`) |

So the transient internal cost at `persist::DEFAULT_SAVE_BUDGET` is ≈54 KB —
the same cost `HighlightFile` already pays at the same budget
(`src/util/HighlightFile.h:42`), not a new one. The conclusion this section
originally drew from the wrong number — that a bookmark budget of 45,000 is
"a number the device cannot afford to reach" and needs a `MAX_BOOKMARKS` — does
not follow, and the spec dropped the record cap because of it.

Two consequences survive the correction:

- **`ESP.getFreeHeap()` alone cannot verify any of this.** It is
  `heap_caps_get_free_size(MALLOC_CAP_INTERNAL)`
  (`framework-arduinoespressif32/cores/esp32/Esp.cpp:163-165`), so it cannot see
  the PSRAM half. Pair it with `ESP.getFreePsram()` (`Esp.cpp:182-187`).
- **Streaming the read buys less than it appears to.** `src/study/PassageFile.cpp:15-43`'s
  reader over `HalFile` removes the 50,000-byte cap and the `String` — but the
  `String` is the PSRAM half, so the internal peak stays at the pool either way.

## 6. `writeDocToFileAtomic` trades one loss window for another

`writeDocToFileAtomic` removes the destination before renaming
(`PersistableStore.cpp:35-42`), so a power loss in that window leaves `.tmp` as
the only copy. Both stores that use it already handle this and bookmarks would
not: `HighlightFile::LoadResult::RecoveredFromTemp` and `highlightLoadAction()`
(`src/util/HighlightFileAction.h:26-32`), and `PassageFile.cpp:62-93`.
Criterion 2 on its own is not a completed fix; `load()` has to decide what a
stray `<path>.tmp` means.

## 7. There is no host test for any of this, and `BookmarkFile.cpp` cannot have one

It reaches `Arduino.h` through `PersistableStore.h`, and there is no host stub
(`src/util/HighlightFileAction.h:8-19` documents why). The precedent is to
extract the branch logic into an Arduino-free header and test that: the
`highlight_file`, `doc_read_status` and `save_budget` suites exist for exactly
this reason. `test/` has no bookmark suite at all today.

## How these were measured

`BookmarkFile::save`'s document was rebuilt key-for-key against ArduinoJson
v7.4.2 and the repo's own `lib/Utf8/Utf8.cpp`, on the host:

```sh
git clone --depth 1 -b v7.4.2 https://github.com/bblanchon/ArduinoJson.git aj
g++ -std=c++20 -I aj/src -I lib/Utf8 measure.cpp lib/Utf8/Utf8.cpp -o measure
```

Byte counts come from `measureJson`, the same function every budgeted store in
this repo calls. Heap figures come from a counting `ArduinoJson::Allocator`, so
they are the pool's own accounting rather than an allocator's overhead; **the
device figure is the human's to confirm with `ESP.getFreeHeap()` **and**
`ESP.getFreePsram()` around a load of a large bookmark file — see the correction
in §5 for why either alone is misleading.** Nothing here was run on hardware.
