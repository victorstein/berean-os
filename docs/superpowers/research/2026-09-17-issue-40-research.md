# Issue #40 — RecentBooksStore title and author are unbounded

Research note. Every claim below was read or run in this worktree at
`11d9497d` (`chore(main): release 1.9.10 (#67)`), which already contains #51's
change to the shared load path (`6156de32`).

---

## 1. Who owns the behaviour

| File | What it owns |
|---|---|
| `src/RecentBooksStore.h:8-15` | `RecentBook` — four bare `std::string`s: `path`, `title`, `author`, `coverBmpPath` |
| `src/RecentBooksStore.h:21` | `MAX_RECENT_BOOKS = 10` — the only bound that exists today |
| `src/RecentBooksStore.h:29-35` | The comment that explains *why* the budget is still the shared default, and `SAVE_BUDGET = persist::DEFAULT_SAVE_BUDGET` |
| `src/RecentBooksStore.cpp:42-65` | `addBook` — the entry point the issue names |
| `src/RecentBooksStore.cpp:67-80` | `updateBook` — a **second** writer of `title`/`author`, not mentioned in the issue |
| `src/RecentBooksStore.cpp:22-40` | `fromJson` — reads `title`/`author` back off the card with no bound |
| `src/RecentBooksStore.cpp:139-140` | `static_assert` pinning the budget to the default, with a message citing "the spec's A5" |

`RecentBooksStore.cpp:139` is a hard gate: tightening `SAVE_BUDGET` without
editing that `static_assert` fails the build. Good — it is the tripwire the
change is supposed to trip.

### Where the strings come from

Both `addBook` call sites pass EPUB metadata straight through, untrimmed:

- `src/activities/reader/ReaderActivity.cpp:60` — `addBook(bookPath, getBookTitle(), getBookAuthor(), getBookThumbBmpPath())`
- `src/activities/reader/EpubReaderActivity.cpp:435` — `addBook(epub->getPath(), epub->getTitle(), epub->getAuthor(), epub->getThumbBmpPath())`

and `src/activities/home/HomeActivity.cpp:75` calls `updateBook(book.path, book.title, book.author, "")`.

The ultimate source is `BookMetadataCache::BookMetadata` (`lib/Epub/Epub/BookMetadataCache.h:15-16`) —
`std::string title; std::string author;` — filled from the EPUB's OPF. Nothing
between the OPF and the JSON file bounds them.

`getDataFromBook` (`src/RecentBooksStore.cpp:119-137`) also builds an unbounded
`RecentBook`, but **it has no callers** (`grep -rn getDataFromBook src` returns
only its declaration and definition). Dead code; out of scope, worth a note.

---

## 2. Current control flow

```
ReaderActivity::onEnter (ReaderActivity.cpp:60)
  └─ RecentBooksStore::addBook (RecentBooksStore.cpp:42)
       ├─ pruneMissing()                      :45
       ├─ erase existing entry with same path :48-52
       ├─ insert {path, title, author, cover} :55   ← no truncation anywhere
       ├─ resize to MAX_RECENT_BOOKS          :58-60
       └─ saveToFileAtomic()                  :62
            └─ PersistableStore<T>::saveToFileAtomic (PersistableStore.h:165)
                 ├─ lock storeMutex
                 ├─ toJson(doc)
                 ├─ measureJson(doc)                          :170
                 ├─ persist::fitsBudget(serialised, 45000)    :171  ← refuse over budget
                 └─ writeDocToFileAtomic(...)                 :176
```

Load side (changed by #51, re-read at `lib/Serialization/PersistableStore.cpp:63-106`):
`loadFromFile` → `readDocFromFileAdopting` → `readDocFromFileChecked`, which
calls `Storage.readFile` (`PersistableStore.cpp:50`). `readFile` hard-caps at
`constexpr size_t maxSize = 50000` (`freeink-sdk/libs/hardware/SDCardManager/src/SDCardManager.cpp:202,204`)
and returns the truncated string with no error. That is the whole reason the
budget exists (`lib/Serialization/SaveBudget.h:7-19`).

`readDocFromFileAdopting` is new since #51 and **does not change anything this
issue touches**: it only adds a `.tmp` promotion when the primary path is
`Missing`. `saveToFileAtomic`'s measure-then-refuse path is untouched.

---

## 3. What the recents list can actually display

The issue says to pick the cap against what can be rendered, not against budget
arithmetic. Four surfaces render a recent book's title today:

| Surface | Call | Effective cap |
|---|---|---|
| `RecentBooksActivity` list row | `item.label = book.title.c_str()` (`src/activities/home/RecentBooksActivity.cpp:39-40`), single-line label (`:174-179`, comment: *"No maxLines=2 here"*) | Whatever fits the row width, then ellipsised |
| `LauncherActivity` resume tile | `utf8SafeSummary(recents[0].title, 48)` (`src/activities/launcher/LauncherActivity.cpp:83`) | **48 bytes** |
| `LauncherActivity` Bible / Meetings subtitles | `utf8SafeSummary(found->title, 40)` (`:103`), `utf8SafeSummary(opened->title, 30)` (`:132`) | **40 / 30 bytes** |
| `PublicationsActivity` entry label | `utf8SafeSummary(opened->title, 48)` (`src/activities/catalog/PublicationsActivity.cpp:56`) | **48 bytes** |

The single-line list label goes through
`FreeInkUIGfxRenderer.h:177-179` → `renderer.truncatedText(fontId, text, rect.width, ...)`,
which appends U+2026 and drops characters until it fits
(`lib/GfxRenderer/GfxRenderer.cpp:1777-1788`). So the list row is the widest
surface, and everything past its width is already invisible today.

Row width, from the theme metrics: screen 800 px, `contentSidePadding = 20`
(`src/components/themes/BaseTheme.h:142`, and identically in
`lyra/LyraTheme.h:17` and `roundedraff/RoundedRaffTheme.h:20`), minus a 32 px
icon (`RecentBooksActivity.cpp:41`) and its gap — on the order of 700 px of
text in the small font. **The px → bytes conversion is not established here**;
it needs either a font measurement or a deliberate design call, and that is a
spec-phase decision.

`HomeActivity` and the theme `drawRecents` paths (`BaseTheme.cpp:708-716`,
`lyra/LyraTheme.cpp:469-484`, `lyra/Lyra3CoversTheme.cpp:91`) wrap the title to
**3 lines** and are the most generous renderers in the tree — but
`HomeActivity` is never instantiated (`grep -rn "HomeActivity" src` finds only
the `#include` at `src/activities/ActivityManager.cpp:16` and the unrelated
`isHomeActivity()` virtual; the only launcher is `LauncherActivity`,
`ActivityManager.cpp:211` constructs `RecentBooksActivity`). Those 3-line paths
are unreachable and must not drive the cap.

---

## 4. Worst-case serialised size, measured

The JSON shape is `{"books":[{"path":…,"title":…,"author":…,"coverBmpPath":…},…]}`
(`src/RecentBooksStore.cpp:11-20`).

Measured with ArduinoJson **7.4.2** — the version both `platformio.ini:151` and
`test/CMakeLists.txt:28-32` pin — by a scratch program calling `measureJson` on
exactly that shape:

| Document | `measureJson` |
|---|---|
| empty (`{"books":[]}`) | 12 |
| 1 entry, all four strings empty | 64 |
| 10 entries, all four strings empty | 541 |
| 10 × (path 255, cover 255, title 0, author 0) | 5,641 |
| 10 × (path 255, cover 255, title 96, author 64) | 7,241 |
| 10 × (path 255, cover 255, title 128, author 96) | 7,881 |
| 10 × (path 512, cover 512, title 128, author 96) | 13,021 |

Two facts that matter for the arithmetic:

1. **Escaping can double a string.** A byte cap on `title` does not bound its
   serialised length by itself. Measured against 7.4.2: 128 bytes of `"` or
   `\` serialise to 256; 128 bytes of 4-byte UTF-8 serialise to 128; 128 bytes
   of `0x01` serialise to 128 — this ArduinoJson does **not** `\u`-escape
   control characters. So the worst-case multiplier is **2×**, from `"` and `\`.
2. **`path` and `coverBmpPath` are also unbounded, and they dominate.** At 255
   bytes each they already account for 5,641 of the 7,881 above. Bounding only
   `title` and `author`, as the issue asks, still leaves the worst case
   theoretically unbounded — the table's "255" and "512" are assumptions, not
   limits found in this repo. `grep -rn "MAX_PATH|PATH_MAX|maxPathLen"` over
   `src`, `lib` and `freeink-sdk` returns nothing; there is no path-length
   constant anywhere in the tree, and `coverBmpPath` is built by string
   concatenation (`lib/Epub/Epub.cpp:653-654`). **This is the one thing the
   issue does not account for and the spec has to resolve.**

---

## 5. Nearest existing example of this change

`study::PassageDoc::add` (`lib/StudyStore/StudyStore/PassageDoc.cpp:22-38`) is
the closest match — it is literally "truncate the strings at the add path, then
check the budget":

```cpp
passage.snippet   = utf8SafeSummary(std::move(passage.snippet),   MAX_SNIPPET_BYTES);
passage.reference = utf8SafeSummary(std::move(passage.reference), MAX_REFERENCE_BYTES);
```

with the caps declared as `static constexpr size_t MAX_SNIPPET_BYTES = 120;` /
`MAX_REFERENCE_BYTES = 48;` in `PassageDoc.h:27-28` and the reasoning in a
comment at `PassageDoc.cpp:24-26` (*"utf8SafeSummary, never resize()"*).
`lib/Epub/Epub/HighlightDoc.cpp:62-63,159-160` does the same on both the add
and the load path.

`src/util/BookmarkDoc.h` is the precedent for the **budget** half: it declares
`MAX_SUMMARY_BYTES = 72` (`:33`) and `MAX_RECORD_BYTES = 420` (`:35-42`) with a
comment recording the *measured* worst case (386 bytes) and the headroom, and
re-bounds on load because *"a file on an SD card is not a trusted input"*
(`:31-32`).

### The helper

`utf8SafeSummary(std::string, size_t maxBytes = 72)` — `lib/Utf8/Utf8.cpp:185-201`,
declared `lib/Utf8/Utf8.h:32`. It collapses whitespace runs, strips `\n`, trims,
and only then truncates, guarded by `if (passage.size() > maxBytes)` before
calling `utf8SafeTruncateBuffer(passage.data(), maxBytes)`.

`utf8SafeTruncateBuffer` (`lib/Utf8/Utf8.cpp:148-165`) indexes `buf[len - 1]`
after only checking `len <= 0`. It does **not** check that the buffer is `len`
bytes long — `test/utf8_summary/Utf8SummaryTest.cpp:33-36` exists precisely to
pin that `utf8SafeSummary` clamps first. Calling it directly requires clamping
`len` to the string's size at the call site; going through `utf8SafeSummary`
gets that clamp for free.

Note the behaviour difference `utf8SafeSummary` brings: it *normalises*
whitespace, so `"Book   Title"` becomes `"Book Title"` even under the cap. For a
display string that is the desired behaviour and matches what
`LauncherActivity.cpp:83` already does to the same field.

---

## 6. How this gets tested

`RecentBooksStore.cpp` is **not host-buildable**. It reaches `<Arduino.h>`
transitively through `PersistableStore.h:3`, and also includes `<Epub.h>` and
`<HalStorage.h>`. The repo has met this exact wall twice and settled on one
answer both times — extract the pure rule into an Arduino-free header and test
that:

- `src/util/HighlightFileAction.h:10-15` spells the reasoning out in full
  (*"a genuinely ESP32-specific header … with no host stub anywhere in this
  repo and too costly to fake convincingly"*), tested by
  `test/highlight_file/` (`CMakeLists.txt:1-4` repeats it).
- `test/bookmark_save_action/CMakeLists.txt:1-2` is the same pattern for
  `BookmarkSaveAction.h`.
- `test/passage_doc/CMakeLists.txt` shows the other shape: when the rules live
  in an Arduino-free `lib/` file, the test compiles that `.cpp` plus
  `lib/Utf8/Utf8.cpp` directly.

`test/stubs/` holds only `Arduino.h`, `HalDisplay.h`, `HalStorage.h`,
`Logging.h` — and the `Arduino.h` stub is not wired into the store tests.

`test/CMakeLists.txt` is a **shared append point**; per `.claude/agents/data-dev.md`
the exact `add_subdirectory(...)` line goes in the PR description for the
orchestrator to apply, not into the file directly.

---

## 7. Tooling actually installed

Run in this worktree on 2026-09-17:

```
$ ~/.platformio/penv/bin/pio --version   → PlatformIO Core, version 6.1.19   (pio is NOT on PATH)
$ cmake --version                        → cmake version 4.4.2
$ c++ --version                          → Apple clang version 21.0.0 (clang-2100.0.123.102)
$ python3 --version                      → Python 3.14.7
$ <main checkout>/.venv/bin/clang-format --version → clang-format version 21.1.8
```

`git submodule update --init --recursive` produced no output — the submodules
are already populated (`freeink-sdk/` has `docs libs LICENSE NOTICE …`).
`./bin/clang-format-fix` needs the **main checkout's** `.venv/bin` on `PATH`
(issue #61, still open).

Host suite baseline, green before any change:

```
$ cmake -S test -B <scratch>/tb                 → Configuring done (5.5s)
$ cmake --build <scratch>/tb --target Utf8SummaryTest BookmarkSaveActionTest SaveBudgetTest -j8
$ <scratch>/tb/utf8_summary/Utf8SummaryTest     → [  PASSED  ] 9 tests.
$ <scratch>/tb/save_budget/SaveBudgetTest       → [  PASSED  ] 5 tests.
```

`git status --short` is clean.

---

## 8. Carried into the spec

1. **`path` and `coverBmpPath` are unbounded too**, and at any plausible length
   they dominate the serialised size (§4). "Bound title and author → the worst
   case is computable" does not hold on its own. The spec must either bound
   them as well, or justify a budget against an explicitly stated path-length
   assumption. This is the decision most likely to need surfacing.
2. **`updateBook` (`RecentBooksStore.cpp:67-80`) is a second write path** for
   `title`/`author`. The issue names only `addBook`.
3. **`fromJson` (`:31-34`) is a third**, reading untrusted card data. `BookmarkDoc.h:31-32`
   and `HighlightDoc.cpp:159-160` both re-bound on load for exactly this reason.
4. **The 2× escaping multiplier** (§4) must appear in the budget arithmetic.
5. **`RecentBooksStore.cpp:139-140`'s `static_assert`** and the
   `RecentBooksStore.h:29-35` comment both have to be rewritten in the same
   change, or the build fails / the file lies.
6. The cap has to be picked against the **list row**, not the launcher's 48 and
   not the unreachable 3-line home card (§3).
7. `getDataFromBook` is dead code (§1). Out of scope; flag only.
