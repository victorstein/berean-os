# Issue #206 research — typed Bible references in verse search

Branch `feature/206-typed-references`, base `67eaaf1e` (release 1.21.0). Every claim below cites a
line read on this branch or a command run on 2026-09-29.

## Installed tools and packages

| Tool | Version | Evidence |
|---|---|---|
| PlatformIO Core | 6.1.19 | `~/.platformio/penv/bin/pio --version` |
| ESP32 platform | pioarduino `55.03.37` | `platformio.ini:15` |
| CMake (host tests) | 4.4.2 | `cmake --version` |
| Host compiler | Apple clang 21.0.0 | `c++ --version` |
| GoogleTest | v1.17.0 | `test/CMakeLists.txt:16-17` |

No new package is needed: the parser is pure C++ over `std::string_view`, and folding reuses
`BibleSearch::foldAppend` (`lib/BibleSearch/BibleSearch/Fold.h:18-23`).

## Who owns the behaviour

| Concern | Owner |
|---|---|
| The search screen, its states and its result list | `src/activities/reader/BibleSearchActivity.{h,cpp}` |
| Full book names, canonical order | `BibleBookNameTable` (`src/activities/reader/BibleBookNameTable.h:17-43`) |
| Publication abbreviations (nav link text) | `BibleNav::Scanner(collectText=true)` → `BookNavPage::labels` (`lib/Epub/Epub/BibleNavScanner.h:33-39,46,62`) |
| Verse → (spine, offset) | the index's verse table, `IndexReader::verse(n, VerseEntry&)` (`lib/BibleSearch/BibleSearch/IndexReader.h:63`, `IndexReader.cpp:95-103`) |
| Case/accent folding | `BibleSearch::foldAppend` (`Fold.h:18-23`; ñ→n in `LATIN_FOLD`, `Fold.cpp:54-79`) |
| Opening the reader at a verse | `EpubReaderActivity` `SEARCH_BIBLE` case (`EpubReaderActivity.cpp:740-754`) |

Search is launched from exactly one place, the reader menu (`EpubReaderActivity.cpp:744`).

## Current control flow

1. `onEnter` clears the font cache and enters `Opening` (`BibleSearchActivity.cpp:100-114`).
2. `runBusyWork` in `Opening`: `bookNames.load(epub, renderer)` then `openIndex()`
   (`BibleSearchActivity.cpp:188-193`). `BibleBookNameTable::load` builds its scanner with the
   default `collectText=false` and only calls `take()` (`BibleBookNameTable.cpp:33-43`), so **the
   search screen never sees the abbreviations today**.
3. `openIndex()` opens the reader; only on success does it open the keyboard. On failure it shows
   the Prepare prompt (`BibleSearchActivity.cpp:219-231`). **No query can be typed until the index
   exists** — a reference jump inside this activity inherits that gate unless the design says
   otherwise.
4. The keyboard result goes to `onQueryEntered` (copies up to `MAX_QUERY_BYTES = 64`,
   `BibleSearchActivity.h:72`, `.cpp:494-504`) → `Searching` → `runSearch()` → `runQuery` →
   `results` and `State::Results` (`BibleSearchActivity.cpp:506-540`).
5. The list: `listCount() = 1 + results.size()` in `Results` (`:542-544`). Row 0 is "Edit search"
   (`buildResults`, `:888-890`); row N is result N-1. `activateIndex(0)` reopens the keyboard,
   anything else → `finishWithVerse(index - 1)` (`:668-676`). The selection starts on row 1
   (`nav.reset(results.empty() ? 0 : 1)`, `:532`).
6. `finishWithVerse` sets `ChapterResult{entry.spine, "", entry.offset}` and finishes
   (`:678-696`); the reader calls `navigateTo({spineIndex, offsetJump})` (`EpubReaderActivity.cpp:750-752`).
7. Empty results draw `STR_BIBLE_SEARCH_NO_RESULTS` above the list (`:864-870`).

A "Go to …" row therefore slots in as a new row between "Edit search" and the hits, which shifts
every `index - 1` / `result + 1` mapping (`:669-675`, `:885-899`, and the initial `nav.reset`).

## Resolving a reference to a place

- `VerseEntry` = `{book 1-66, chapter, verse, spine, offset}` (`IndexFormat.h:53-59`). `book` is
  canonical: `BibleSearchStore::resolveDocuments` tags each spine with book numbers 1..66 from
  `STUDY.spineIndicesForBook` (`BibleSearchStore.cpp:146-156`), the same numbering
  `bookNames.forBook(book)` reads (`BibleBookNameTable.h:38`).
- The verse table is written "in canonical order" (`IndexFormat.h:11`); `IndexBuilder::addVerse`
  documents that as a caller contract (`IndexBuilder.h:58-60`) but does **not** check it
  (`IndexBuilder.cpp:135-155`). A binary search of the verse table by (book, chapter, verse)
  depends on that contract; a lower-bound search also covers "verse missing → nearest following"
  and chapter-only references (first verse of the chapter).
- Each `verse(n)` is one 9-byte `readAt` served through the reader's 4 KB page
  (`IndexReader.h:36-38`, `.cpp:95-103`): ~15 probes for 31,100 verses, no new allocation.
- The non-index path exists in `BibleNavigationActivity` (chapter grid → `finishWith(spine, offset)`,
  `BibleNavigationActivity.cpp:236-244`) but it walks chapter-nav pages and `VerseAnchors`; it is
  not a function the search screen can call.

## The names and abbreviations actually in the publication

- Abbreviations carry a trailing period and can contain a space and a digit prefix: `Gén.`, `Éx.`,
  `1 Crón.`, `Mat.`, `Jud.`, `Apoc.` (`test/bible_nav_scanner/BibleNavLabelsTest.cpp:20-27,51-55`,
  verbatim structure of `nwt_S.epub`).
- Full names come from TOC titles, e.g. "El Cantar de los Cantares" (25 B), up to ~42 B in
  Cyrillic/Greek (`BibleBookNameTable.h:22-25`). English names used in tests: "Genesis", "Isaiah",
  "John", "1 John" (`test/bible_book_join/BibleBookJoinTest.cpp:36-99`).
- The issue's own example `Juan 3:16` is a full name, not an abbreviation. `Isa` is a prefix of
  the English abbreviation (presumably `Isa.`) and of "Isaiah"; the Spanish grid shows why plain
  prefix matching is ambiguous: `Jue.`/`Jud.`/`Juan` (memory note on PR #90; `Jud.` confirmed in
  the fixture above). No English `biblebooknav.xhtml` fixture exists in `test/`
  (`grep -rln "Isa\." test` finds only name lists), so the English abbreviation set is unverified.
- Book-grid abbreviations are held as `char bookAbbrev[66][16]` in `BibleNavigationActivity`
  (`BibleNavigationActivity.h:43,64`), filled from `page.labels` with the full name as fallback
  (`BibleNavigationActivity.cpp:89-96`). That is the load to mirror: `BibleNavigationActivity.cpp:56-110`.

## Keyboard

The search keyboard is the SDK builtin layout (`KeyboardEntryActivity` with no URL mode,
`BibleSearchActivity.cpp:477-478`). Digits are on its number row and `:` is on the symbol layer
(`freeink-sdk/libs/ui/FreeInkUI/src/FreeInkUI.cpp:132,169`), so `Isa 40:31` is typeable; `Gén`
needs the Spanish layout or the fold (é→e) to meet `gen`.

## Memory

- New state for abbreviations: 66 × 16 B = 1,056 B if copied like `bookAbbrev`, alongside the
  existing `BibleBookNameTable` (66 × 48 B = 3,168 B, `BibleBookNameTable.h:21,25,41`). Both sit
  inside the heap-allocated activity; the transient `BookNavPage` vectors are freed at the end of
  the load.
- Parsing a ≤ 64-byte query is stack-sized work; folding appends into a `std::string` of at most
  the query length (`Fold.h:21-22`). Not a hot path — once per entered query.

## Nearest existing examples

- **Pure, host-tested helper next to the activity:** `src/activities/reader/BibleReference.h`
  ("Free of Arduino and the Epub library so test/ui_layout can exercise it", `:5-7`), tested by
  `test/ui_layout/BibleReferenceTest.cpp`, registered in `test/ui_layout/CMakeLists.txt:16-29`.
  `NumberGridLayout.h` and `BookGridLayout.h` follow the same shape. A new test executable there
  touches `test/ui_layout/CMakeLists.txt`, not the shared top-level `test/CMakeLists.txt`
  (which only `add_subdirectory(ui_layout)`s it, `test/CMakeLists.txt:102`).
- **Folding tests against real Spanish/English words:** `test/bible_search_fold/FoldTest.cpp`,
  built from `Fold.cpp` directly (`test/bible_search_fold/CMakeLists.txt`). A parser that calls
  `foldAppend` needs `Fold.cpp` in its test target the same way.
- **Abbreviation load:** `BibleNavigationActivity::loadBooks` (`BibleNavigationActivity.cpp:56-110`).
- **Returning a verse:** `BibleSearchActivity::finishWithVerse` (`:678-696`).

## Strings

Existing keys live in `lib/I18n/translations/english.yaml:23-50` (`STR_BIBLE_SEARCH_*`). The row
needs one new key (e.g. `"Go to %s"`); translation YAMLs are shared files — hand the lines off in
the PR body.

## Scope and tier

Everything lands in `src/activities/reader/` (ui surface) plus a `test/ui_layout/` test. No
on-disk format, store, or contract changes: the index is read, never written, and `ChapterResult`
is unchanged. Tier `standard` stands.

## Open questions for the spec

1. Reference jump only once the index is ready (today's gate), or also before it exists? The
   verse table is the only (spine, offset) source this screen owns.
2. Matching rule: exact folded full name or abbreviation (period optional), versus unique prefix.
   `Isa` in the issue implies prefix; `Jud`/`Jue`/`Juan` show prefixes can be ambiguous.
3. A reference to a verse or chapter that does not exist (`Gen 51`, `Jud 2`): no row, or the
   nearest place.
4. Ranges (`Juan 3:16-18`): parse and open at the first verse.
