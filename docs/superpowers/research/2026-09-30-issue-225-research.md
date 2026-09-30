# Issue #225 research — unify the Bible reference formatters

Branch `refactor/225-unify-reference-formatters`, based on `e3e5c94e` (release 1.28.1).
Everything below was read in this worktree on 2026-09-30; line numbers are for that tree.

## 1. Who builds a "Book C:V" string today

The issue lists five. There are **seven** builders once the status bar and tagged passages are
counted; the two extra ones are marked **(not in the issue)**.

| # | Builder | Book-name source | Chapter source | Output shape | Consumers |
|---|---|---|---|---|---|
| 1 | `bibleReference(bookName, chapter)` — `src/activities/reader/BibleReference.h:8-11` (header-only, `std::string`) | covering TOC entry: `epub->getTocItem(epub->getTocIndexForSpineIndex(spine)).title` | `currentBibleChapter()` (`EpubReaderActivity.cpp:1783-1785`) | `"Isaiah 40"`; book alone when chapter ≤ 0 | reader-menu title `readerMenuTitle()` (`EpubReaderActivity.cpp:1787-1794`) |
| 2 | same `bibleReference` **(not in the issue)** | same TOC title | same `currentBibleChapter()` | same | status-bar chapter title, `renderStatusBar()` (`EpubReaderActivity.cpp:1812-1819`), when `titleMode == CHAPTER_TITLE` — runs for **every** EPUB, not only Bibles; a non-Bible gets chapter −1 so the bare TOC title |
| 3 | `PlacesDoc::formatReference(book, unit, chapterOnly)` — `src/util/PlacesDoc.cpp:51-60` | covering TOC title, `EpubReaderActivity.cpp:1489-1490` | `unit.major` from `STUDY.placeAt` (`EpubReaderActivity.cpp:1481`, `StudyStore.cpp:88`, `PlacesDoc::placeUnit` `PlacesDoc.cpp:38-49`) | `"Revelation 21:4"` / `"Genesis 1"`; `"40:31"` with empty book; `utf8SafeSummary`-capped to `MAX_REFERENCE_BYTES = 48` (`PlacesDoc.h:26`) | **persisted** as `Place::reference` (`"r"` in `/.berean/places.json`, `PlacesDoc.cpp:122`), then shown verbatim on Home: "Continue at %s" (`LauncherActivity.cpp:148`) and the Recent places rows (`LauncherActivity.cpp:309`) |
| 4 | `PlacesDoc::formatChipLabel(abbrev, place, out, outBytes)` — `src/util/PlacesDoc.cpp:62-77` | nav abbreviation `BibleBookNameTable::abbreviationFor` (`EpubReaderActivity.cpp:900-906`); falls back to the stored `place.reference` when the table did not load | `place.unit.major` / `.minor` | `"Rev. 21:4"` / `"Gén. 1"`, into `RECENT_LABEL_BYTES = 49` (`ReaderMenuSheetLayout.h:15`, static_assert `EpubReaderActivity.cpp:932`) | reader-menu Recent chips |
| 5 | `formatTypedReference(out, outBytes, bookName, ref)` — `src/activities/reader/TypedReference.cpp:254-271` | `BibleBookNameTable::forBook` (`BibleSearchActivity.cpp:528`) | the parsed/resolved `TypedReference.chapter` | `"Juan 3:16-18"`, `"Génesis 1"`, `"40:31"` — the only builder with a verse **range** | "Go to %s" row (`STR_BIBLE_SEARCH_GO_TO`, `BibleSearchActivity.cpp:529`) |
| 6 | inline `snprintf` — `src/activities/reader/BibleSearchActivity.cpp:597-604` | `BibleBookNameTable::forBook` | `bible.idx` `VerseEntry.chapter` / `.verse` | `"Isaiah 40:31"`, `"40:31"` with empty name, into `Row::reference[64]` (`BibleSearchActivity.h:87,95`) | search hit rows |
| 7 | `PassageSelectActivity::verseReference` **(not in the issue)** — `src/activities/reader/PassageSelectActivity.cpp:196-239` | covering TOC title via `epub.getSpineItem(spine).tocIndex` (`:235-237`) | `VerseAnchors::format` of the anchor at the selection start (`:229`, `VerseAnchors.cpp:120-125`) | `toc.title + " " + "C:V"`, or bare `"C:V"` | **persisted** as `TaggedPassage::reference` (`StudyStore.cpp:293`), capped to 48 in `PassageDoc.cpp:150`; shown as the Highlights row label (`HighlightsActivity.cpp:113`) |

Each builder does its own empty-name fallback. Only #3 drops the separator when the book is empty.
That is the #226 fix referred to in the issue (`git log -1 -- src/util/PlacesDoc.cpp` →
`222019b0`). #5 and #6 branch on an empty name explicitly. #1, #2 and #7 avoid it only because
their callers return early: `readerMenuTitle` returns the publication title when `book.empty()`
(`EpubReaderActivity.cpp:1792`), and `verseReference` checks `toc.title.empty()`
(`PassageSelectActivity.cpp:238`). The status bar (#2) has no guard, so an empty TOC title with a
known chapter would render `" 5"` through `bibleReference`.

## 2. The two book-name sources, and where they can disagree

- **Covering TOC entry** (#1, #2, #3, #7). `SpineEntry.tocIndex` is set in
  `BookMetadataCache.cpp:236-316`. It is the **first** TOC entry whose target is that spine item
  (`:239-245`). A spine item with no TOC entry of its own inherits the previous spine item's
  (`:311-316`). An NWT chapter file has no TOC entry of its own (one TOC entry per book,
  `BibleChapterNumber.h:7-9`), so it inherits whatever TOC entry last matched an earlier spine
  item.
- **`BibleBookNameTable`** (#4 abbreviations, #5, #6 names; `BibleBookNameTable.h:20-58`,
  `.cpp:28-83`). Names are the TOC entry whose href matches each `biblebooknav.xhtml` link target,
  first match wins (`BibleBookNameTable.cpp:59-66`). Abbreviations are the nav link text
  (`:51-52`, capped to `ABBREV_BYTES = 16`). Loading streams the nav spine item and can inflate
  it. The reader menu passes `WhenMissing::Fail` (`EpubReaderActivity.cpp:900`). The table is
  ~4.2 KB and heap-allocated per use (`EpubReaderActivity.cpp:897-899`); it is not resident in
  the reader.

Both sources end in a TOC title, but through different joins: spine inheritance versus
nav-href match. They agree when the TOC entry that covers a book's chapters is also the entry
the nav link points at. The Spanish NWT's publisher TOC interleaves 66 "Contenido de …" outline
entries with the book entries (project memory `jw-nwt-epub-toc-doubling`, confirmed on hardware
2026-09-08). I have **not verified** which of the two a chapter file inherits there, and no
fixture in `test/` has a doubled TOC. This is the "one place labelled two ways" risk the issue
names. The spec needs either a device check on the Spanish NWT or a fixture before it claims
either source is safe.

The abbreviation decision (project memory `book-abbreviation-source-decision`; issue #206 body,
"Book names and abbreviations are available from the publication itself: `BibleBookNameTable`
and the nav abbreviations") says to use the publication's own nav text, never truncation and
never a hardcoded table. `BibleBookNameTable` is the only source of abbreviations. The TOC
spine-inheritance join cannot supply them.

## 3. The two chapter sources

- `currentBibleChapter()` (#1, #2). `resolveBibleChapterNumber()` (`EpubReaderActivity.cpp:1763-1781`,
  called from the render path at `:1296`) streams the spine HTML with `WhenMissing::Fail` and
  stops at the first `chapter<N>_verse<M>` marker (`BibleChapterNumber.h`, `feedChapterNumberReader`
  `EpubReaderActivity.cpp:73-76`). The result is −1 when the HTML is not cached or the book is not
  a Bible.
- `unit.major` (#3, #4) comes from the study unit index. `Unit.h:17-20` says a document is `Verse`
  exactly when it carries `chapter<N>_verse<M>` markers, and `major` is that chapter. It is only
  available once the unit index exists (`STUDY.placeAt` returns nullopt otherwise,
  `EpubReaderActivity.cpp:1482-1485`).
- #5, #6 use `bible.idx` (`VerseEntry.chapter`), built by `lib/BibleSearch/BibleSearch/VerseTextScanner.cpp`,
  which also includes `VerseAnchors`. #7 uses `VerseAnchors::format` directly.

All four chapter numbers come from the same marker grammar, parsed once in `VerseAnchors.cpp:30-41`.
What differs is **availability**. `currentBibleChapter()` is −1 on a finished layout cache with no
HTML cache (comment at `:1771-1776`). `unit.major` needs the unit index. A single derivation would
have to choose which availability gap it accepts. The reader title today shows the bare book name
in that gap (`BibleReference.h:9`).

## 4. Existing tests of these builders

- `test/ui_layout/BibleReferenceTest.cpp` covers `bibleReference`: chapter appended, chapter ≤ 0,
  UTF-8. Target at `test/ui_layout/CMakeLists.txt:17-29`, header-only, links only
  `crosspoint_test_common`.
- `test/places_doc/PlacesDocTest.cpp:286-312` covers `formatReference` (including the empty-book
  case at `:296-297`) and `formatChipLabel` (abbreviation, UTF-8 abbreviation, fallback). Target at
  `test/places_doc/CMakeLists.txt`.
- `test/ui_layout/TypedReferenceTest.cpp:205-246` covers `formatTypedReference`. Target at
  `test/ui_layout/CMakeLists.txt:31-49`.
- Search hit rows (#6) and `verseReference` (#7) have **no** host test. Both are inline in
  activities that pull Arduino/HAL.
- No test compares builders against each other, which is the acceptance criterion's "same place
  formats the same way" test.

`test/CMakeLists.txt` is a shared file (`.claude/agents/ui-dev.md`, "Shared files"). Adding
sources to an existing subdirectory's `CMakeLists.txt` (`test/ui_layout/`, `test/places_doc/`)
is not that file. A new `add_subdirectory` would need a hand-off line.

## 5. Installed tools

```
$ cmake --version            → cmake version 4.4.2
$ c++ --version              → Apple clang version 21.0.0 (clang-2100.0.123.102)
$ ~/.platformio/penv/bin/pio --version → PlatformIO Core, version 6.1.19
```

`pio` is not on PATH (project memory `fresh-worktree-build-bootstrap`: run `./bin/bootstrap`
first). The C++ standard is gnu++2a (`platformio.ini:40`). ArduinoJson is 7.4.2
(`PlacesDoc.h:34` comment). No new package is needed. All the builders use `snprintf`,
`std::string` and `utf8SafeSummary` / `copyUtf8Truncated` from `lib/Utf8`.

## 6. Nearest existing examples to mirror

- **Pure, host-testable formatter in `src/`:** `BibleReference.h` (header-only, "Free of Arduino and
  the Epub library so test/ui_layout can exercise it", `:5-7`) and `PlacesDoc` (namespace of pure
  functions free of `<Arduino.h>`, `PlacesDoc.h:14-17`). Either is the shape for a single
  formatter that all seven sites can include from `src/activities` and `src/util` alike.
- **Fixed-buffer formatting with a UTF-8-safe cap:** `formatChipLabel` (`PlacesDoc.cpp:62-77`) and
  `formatTypedReference` (`TypedReference.cpp:254-271`). CLAUDE.md's string policy puts
  `snprintf` into a fixed `char[]` in hot paths. The search rows (#6) fill up to `ROW_CACHE` rows
  per scroll, so that is the hot one. `std::string` is used at the persisted and title sites.
- **Unifying diverged copies behind one helper:** #127 (`c46f87a2`, "keep an unusable .tmp on
  every study-data load"). It went through the same research → spec → plan pipeline, replaced
  five per-store copies with `lib/Serialization/TempAdoption.h`, and pinned the shared behaviour
  with one test suite (`test/storage_io/`).

## 7. What this means for scope and tier

- Nothing here changes an on-disk format. `Place::reference` and `TaggedPassage::reference` are
  display strings the device writes once and reads back verbatim (`PlacesDoc.cpp:152`,
  `LauncherActivity.cpp:148,309`, `HighlightsActivity.cpp:113`). Changing the formatter changes
  what **new** entries store. It does not reinterpret old ones, so no format-version bump and no
  migration is needed. Stored entries keep their old labels, which can differ from a fresh label
  for the same place until they are re-recorded. The spec should state this.
- `src/util/PlacesDoc.*` and `src/study/StudyStore.cpp` border the data-dev surface. The change
  there is to the display string only, so it is not a store or contract change.
- The open design choices are: (a) whether #2 and #7, which are not in the issue, are in scope;
  (b) which book-name join is the one source, given the unverified doubled-TOC divergence in §2;
  (c) which chapter availability gap to accept (§3); (d) `std::string` or fixed `char[]` output,
  given the hot search-row path. The spec settles these.
- Tier stays **standard**. The work is within the `ui` surface plus two pure `src/util` helpers.
  It adds no contract, dependency or migration.
