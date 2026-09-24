# Bible verse search

**Date:** 2026-09-24
**Status:** Design approved by the user

## Goal

Find a verse by the words in it. Type `amor paciente` and get every verse containing both words,
each shown with its reference and the start of its text; tap one to read it.

## Scope change

This reverses a stated non-goal: on-device full-text search was out, and the founding design
(`docs/superpowers/specs/2026-09-13-berean-os-design.md`) limited search to the publication
catalog. The user has decided to add **Bible verse search only**. Searching other publications,
footnotes or study notes stays out. `AGENTS.md` (which `CLAUDE.md` links to) and the founding
design say so.

## Decisions

| Question | Decision |
|---|---|
| What is searched | Verse text only: no footnotes, introductions or appendices. Every result is a verse reference. |
| How words match | Every query word must appear in the verse, in any order. Case and accents are folded, and ñ folds to n, so `senor` finds `Señor`. The last word also matches as a prefix (`pacien` finds `paciente`). |
| Result rows | The reference plus the start of the verse text, with a total count. |
| Entry point | A search button on the Bible navigation screen (the book grid). |
| Index build | On first search, with a progress screen. There is one Bible on the device. |

## Measurements

These come from `nwt_S.epub`, measured offline:

- 3,937 xhtml documents: 19.6 MB of markup, 5.7 MB of visible text.
- About 1,036,000 words, 28,086 distinct words before folding.
- 31,078 verses in 1,189 verse-bearing documents (the other 2,748 spine documents carry no
  verses).

How a verse sits in the markup:

- A verse starts at an empty `<span id="chapterN_verseM">`, which `VerseAnchors` already parses.
- The verse number is in `<strong><sup>N</sup></strong>`.
- Footnote markers are `<a epub:type="noteref">*</a>`.
- The footnote bodies follow the verse text in the same document.

Every chapter is exactly one spine document (`1001061147-split2.xhtml` is the whole of John 2),
and no document starts mid-verse. The build still walks spine documents, which is the same thing
here.

The loop task is not subscribed to the task watchdog (see the comments in `BibleSearchStore.cpp`
and `BibleSearchIndexer.cpp`), so the real cost of a long step is a stalled UI, not a panic. The
build walks 1,189 documents; its duration is measured on the device.

## Design

### 1. Screens

**Entry.** A **Buscar versículos** button in a band below the book grid, on the book level only
(`BibleNavigationActivity`). It opens `BibleSearchActivity`, which opens `KeyboardEntryActivity`
once the index is ready. The keyboard picks the Spanish layout with ñ from the UI language
(`KeyboardEntryActivity.cpp:112-132`).

**1a. Preparing search.** The first time search is opened, or whenever the index is missing,
stale, incomplete, unreadable or in a newer format, the screen asks "¿Preparar búsqueda?". The
message says which: that this happens once and takes a few minutes, that the Bible changed, or
that the index could not be read. Whenever a checkpoint for this Bible exists, it says instead
that the interrupted preparation will continue where it stopped. Confirming shows a starting
frame, "Preparando…" or "Retomando búsqueda…", with Cancel available, while `begin()` runs, then
the full-screen progress view:

- **Title:** "Preparando búsqueda".
- **Progress:** a real percentage and a bar, via `BaseTheme::drawProgressBar`
  (`BaseTheme.cpp:115`). The total is the number of documents to index, known before the pass
  starts.
- **Status line:** the book being read and its current chapter, taken from the verse anchors,
  e.g. `Salmos · capítulo 119`.
- **Time left:** `Quedan ~2 min`. It appears only after about 5% is done, once a measured rate
  exists, and until then the space stays empty.
- **Cancel:** a Cancel button, plus the left-edge swipe (Back). Work done so far is kept, and
  the next search resumes from the checkpoint.
- **Refresh cadence:** FAST refreshes, at most one per whole percentage point and one every 2 s.
  A HALF_REFRESH (the codebase's clean refresh) on entry, on the progress view, on "Listo" and on
  a failure. Redraws go through the render task (`requestUpdate`), so indexing never waits on
  the panel.
- **Finish:** "Listo", then the keyboard opens straight away. "Listo" stays up while the new
  index opens and its term index loads.

**1b. Results.** A "Buscando…" frame is shown between the keyboard and the results. The results
are a list headed with the count, e.g. `Resultados: 42`, or `Resultados: 1000+` when capped.

- **Rows:** each row shows the reference (`Juan 3:16`) and the start of the verse text.
- **Book names:** the full TOC book name, found by the same TOC join
  `BibleNavigationActivity::loadBooks` uses (`BibleNav::findTargetByHref`), so references follow
  the publication's language.
- **Tap:** opens the reader at that verse through the existing navigation result
  (`ChapterResult{spine, "", offset}`, the same path the verse grid uses).
- **Paging:** swipe or a held button pages the list.
- **No results:** says so and offers to edit the query. An "Editar búsqueda" row always leads
  the results list.

Every string goes through `tr()`, and new keys go in `english.yaml`. Spanish is the user's
language, so `spanish.yaml` gets the new keys too.

### 2. Matching

- **Folding** (a pure function, host-tested):
  - lower-case the text;
  - strip Latin diacritics (á→a, é→e, í→i, ó→o, ú→u, ü→u, ñ→n, ç→c, à→a and the rest of the
    Latin-1 and Latin Extended-A letters);
  - leave other scripts untouched.
- **Tokens:** a token is a maximal run of ASCII letters/digits or of non-ASCII codepoints that
  are not punctuation, after folding. Tokens under 2 bytes are dropped; tokens over 32 bytes are
  cut on a UTF-8 boundary. Punctuation, and typographic quotes such as “ ” ¡ ¿, separate tokens.
- **Query:** the query is split and folded the same way. Each full word must match a word in
  the verse exactly; the last word may match as a prefix. A query with no usable tokens gives
  no results.
- **Common words:** none are excluded. A query of only common words is valid; it just returns
  many results.
- **Order:** results are in canonical order, Genesis 1:1 to Revelation 22:21.

### 3. Index file

The file is `/.berean/search/bible.idx`. It is derived data: losing it costs a rebuild and
loses nothing the user made. It still obeys every `AGENTS.md` storage rule:

- atomic write;
- a byte budget checked before writing;
- a format version, refused when newer;
- owned by the main task, with `storageMutex` held through HalStorage.

Layout, all little-endian:

| Section | Content |
|---|---|
| Header | magic `BSIX`; formatVersion u16; flags u16 (bit 0 = complete); fingerprint u64 (FNV-1a over the EPUB size, spine count, the verse-document hrefs and their count; no path or mtime, since device-written FAT timestamps are constant); verseCount; termCount; docsDone; four section offsets; fileSize (must equal the real size). 48 B. |
| Verse table | one entry per verse, in canonical order: book (u8), chapter (u8), verse (u8), spine (u16), VerseAnchors' visible-codepoint offset in the document (u32), 9 B total, packed. About 280 KB. |
| Term table | terms sorted by folded bytes: each is the offset of its term string, a postings offset (u32) and a posting count (u16). 23,568 entries for the Spanish NWT; 10 B each. |
| Term strings | folded UTF-8 terms, each ending in a NUL |
| Postings | per term, ascending global verse numbers, delta-encoded as LEB128 varints |

1,469,729 B for the Spanish NWT; the write budget is 8 MB. The verse table stores the book of
each verse, so no separate book map is needed.

Rules:

- A file whose fingerprint does not match the Bible on the card is stale; the prompt asks before
  rebuilding.
- A file with a newer `FORMAT_VERSION` is refused and left in place until the user confirms a
  rebuild.
- An unreadable file is never overwritten silently: the rebuild prompt says the index could
  not be read.

### 4. Build

- **Input:** `StudyStore::spineIndicesForBook` for books 1-66, in canonical order, from the unit
  index's book map.
- **Tokenising:** each document is streamed once through a scanner that combines the
  `VerseAnchors` rules with a text-only tokeniser. That scanner:
  - starts a new verse at each `chapterN_verseM` span;
  - skips `<sup>` inside `<strong>` (the verse number) and every `<a epub:type="noteref">`;
  - stops collecting verse text at the footnote section.

  It also skips the `w_navigation`, `w_ch`, `sw`, `ss` and `sd` classes, `<header>` and
  `<h1>`-`<h6>`, so superscriptions are not searchable. A skipped block still separates words.
- **Memory:** an open-addressing hash of term ids, with postings held LEB128-encoded in pooled
  blocks, drawn in 48-64 KB chunks from an injected PSRAM allocator. A verse number is appended
  once per term per verse. Running out of memory returns false and fails the build; it is never
  skipped as a bad document.
- **Stepping:** one document per `loop()` pass (`step()`); there is no task of its own.
- **Checkpoint:** every 300 documents and on cancel, the partial state is written atomically to
  `/.berean/search/bible.partial`: the documents done, the verse table so far, and the postings.
  A cancelled or interrupted build resumes from it. The checkpoint is the `bible.idx` format with
  the complete flag clear.
- **Finish:** terms are sorted, the file is written atomically, and the checkpoint is removed.
  Written to `<path>.tmp`, then the old file is removed and the temp renamed. A failed card write
  on the final file is retried once, then kept as a checkpoint.
- **Peak memory:** 2.88 MB of PSRAM measured, freed when the build ends. The internal SRAM
  footprint is the parser and a small read buffer.

### 5. Query

- **Term lookup:** each query word is found by binary search in the term table. When search
  opens, the term table and strings (~444 KB) are cached in PSRAM for the session; postings
  stream through a 4 KB page.
- **Prefix and intersection:** full words are intersected smallest first. The last word's prefix
  range then marks a verse bitset, which filters the intersection, so nothing is capped before
  it. `PREFIX_CAP` (5,000) applies only to a lone prefix word, where `RESULT_CAP` makes it
  redundant.
- **Results:** capped at 1,000 verses, held as u16 verse numbers in a `std::vector<uint16_t>`
  (≤ 2 KB).
- **Rows:** rows cache up to 16 entries; one streamed document serves every visible row it holds.

### 6. Components

- **`lib/BibleSearch/`**, pure and host-tested:
  - `Fold` (folding and tokenising);
  - `IndexFormat` (the on-disk layout);
  - `IndexBuilder` (build state, checkpoint, final write);
  - `IndexReader` (header, verse table, term lookup, postings);
  - `Query` (parse, intersect, prefix);
  - `Allocator` (the injected allocator the build and the reader draw from);
  - `VerseTextScanner` (per-verse visible text from one document, shared by the build and the
    result rows).
- **`src/study/BibleSearchStore`:** the files on the card, the fingerprint, and the reader
  opened for a session.
- **`src/study/BibleSearchIndexer`:** the build, one document per step.
- **`StudyStore::spineIndicesForBook`:** a book's verse documents from the unit index's book map.
- **`BibleSearchActivity`:** the prepare prompt, the progress view, the results list, and the
  hand-off to the reader.
- **`BibleNavigationActivity`:** gains the search entry point on the book level.
- **Docs:** `AGENTS.md` (which `CLAUDE.md` links to) and `2026-09-13-berean-os-design.md` change
  their scope wording, and
  `docs/file-formats.md` documents `bible.idx`.

### 7. Error handling

| Condition | Behaviour |
|---|---|
| Out of memory during build | Stop, keep the last checkpoint, tell the user the build could not finish |
| SD write fails | A failed checkpoint write is logged and the build continues. A failed final write is retried once, then kept as a checkpoint, and the user is told. |
| Bible replaced or changed | Fingerprint mismatch: prompt to rebuild |
| Index unreadable or newer format | Say so and prompt to rebuild; never overwrite without asking |
| A result's document fails to stream | That row shows the reference only |
| A document the scanner rejects as malformed | Skipped and logged; the build continues |

### 8. Testing

**Host tests:**

- folding and tokenising, including every Spanish accent and ñ;
- tokenising verbatim NWT markup: footnote markers and verse numbers are excluded, and text after
  the footnote section is ignored. The continuation API is tested on a synthetic document; no
  real document produces one. The fixtures are short verbatim excerpts, because the repository
  is public;
- a build → write → read → query round trip on a three-chapter fixture, with exact result sets;
- the prefix range and its cap;
- the result cap;
- a newer format version is refused;
- resuming from a checkpoint gives the same file as an uninterrupted build;
- a fingerprint mismatch is reported as stale.

**On the device:**

- total build time, and free heap before, during and after the build;
- the progress screen: its cadence, the time-left estimate, and that Cancel resumes;
- query latency;
- correct results for known verses (`Juan 3:16`, `amor paciente`, `senor`).

## Out of scope

- Search in publications other than the Bible.
- Footnote and study-note text.
- Exact-phrase queries.
- Highlighting the matched words inside the reader page.
- Search history.
