# Bible verse search

**Date:** 2026-09-24
**Status:** Design approved by the user

## Goal

Find a verse by the words in it. Type `amor paciente` and get every verse containing both words,
each shown with its reference and the start of its text; tap one to read it.

## Scope change

This reverses a stated non-goal. `CLAUDE.md:8-9` and `:114` list "on-device full-text search"
as out, and so does `docs/superpowers/specs/2026-09-13-berean-os-design.md:84-85`, which gives
only "Search means the publication catalog, not content". The user has decided to add **Bible
verse search only**. Searching other publications, footnotes or study notes stays out.
`CLAUDE.md` and the founding design doc are amended in the same change.

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
- About 31,100 verses.

How a verse sits in the markup:

- A verse starts at an empty `<span id="chapterN_verseM">`, which `VerseAnchors` already parses.
- The verse number is in `<strong><sup>N</sup></strong>`.
- Footnote markers are `<a epub:type="noteref">*</a>`.
- The footnote bodies follow the verse text in the same document.

A chapter can span several spine documents, for example `1001061147-split2.xhtml`. The build
therefore walks spine documents, not chapters.

The existing unit index measured about 30 ms per document, so a full pass over the NWT takes
about two minutes. The 5 s task watchdog panics on any uninterrupted pass
(`src/study/UnitIndexCache.h:17-20`). A search build is a pass of the same shape plus
tokenising: estimated at 2–4 minutes, once.

## Design

### 1. Screens

**Entry.** A search control on the Bible navigation screen's book level. It opens
`KeyboardEntryActivity`, which already picks the Spanish layout with ñ from the UI language
(`KeyboardEntryActivity.cpp:112-132`).

**1a. Preparing search.** The first time search is opened, or whenever the index is missing,
stale or incomplete, the screen asks "Preparar búsqueda?". It says this happens once and takes a
few minutes. Confirming starts the build on a full-screen progress view:

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
- **Refresh cadence:** fast partial refreshes, at most one per percentage point *and* no more
  than one every 2 seconds, so about 100 redraws in all. A full refresh happens only on entry
  and on completion. Redraws go through the render task (`requestUpdate`), so indexing never
  waits on the panel.
- **Finish:** a brief "Listo", then the keyboard opens straight away.

**1b. Results.** A list headed with the count, e.g. `Resultados: 42`, or `Resultados: 1000+`
when capped.

- **Rows:** each row shows the reference (`Juan 3:16`) and the start of the verse text.
- **Book names:** the full TOC book name, found by the same TOC join
  `BibleNavigationActivity::loadBooks` uses (`BibleNav::findTargetByHref`), so references follow
  the publication's language.
- **Tap:** opens the reader at that verse through the existing navigation result
  (`ChapterResult{spine, "", offset}`, the same path the verse grid uses).
- **Paging:** swipe or a held button pages the list.
- **No results:** says so and offers to edit the query.

Every string goes through `tr()`, and new keys go in `english.yaml`. Spanish is the user's
language, so `spanish.yaml` gets the new keys too.

### 2. Matching

- **Folding** (a pure function, host-tested):
  - lower-case the text;
  - strip Latin diacritics (á→a, é→e, í→i, ó→o, ú→u, ü→u, ñ→n, ç→c, à→a and the rest of the
    Latin-1 and Latin Extended-A letters);
  - leave other scripts untouched.
- **Tokens:** a token is a maximal run of letters or digits after folding. One-letter tokens are
  dropped. Punctuation, and typographic quotes such as “ ” ¡ ¿, separate tokens.
- **Query:** the query is split and folded the same way. Each full word must match a word in
  the verse exactly; the last word may match as a prefix. A query with no usable tokens gives
  no results.
- **Common words:** none are excluded. A query of only common words is valid; it just returns
  many results.
- **Order:** results are in canonical order, Genesis 1:1 to Revelation 22:21.

### 3. Index file

The file is `/.berean/search/bible.idx`. It is derived data: losing it costs a rebuild and
loses nothing the user made. It still obeys every CLAUDE.md storage rule:

- atomic write;
- a byte budget checked before writing;
- a format version, refused when newer;
- owned by the main task, with `storageMutex` held through HalStorage.

Layout, all little-endian:

| Section | Content |
|---|---|
| Header | magic, `FORMAT_VERSION`, EPUB fingerprint (file size plus a hash of its path and modification time), verse count, term count, section offsets, `complete` flag |
| Verse table | one entry per verse, in canonical order: book (u8), chapter (u8), verse (u8), spine (u16), byte offset in document (u32), 9 B total, packed. About 280 KB. |
| Term table | terms sorted by folded bytes: each is the offset of its term string, a postings offset (u32) and a posting count (u16). About 28K entries. |
| Term strings | folded UTF-8 terms, each ending in a NUL |
| Postings | per term, ascending global verse numbers, delta-encoded as LEB128 varints |

Estimated at about 1 MB in total. The verse table stores the book of each verse, so no separate
book map is needed.

Rules:

- A file whose fingerprint does not match the Bible on the card is stale and is rebuilt.
- A file with a newer `FORMAT_VERSION` is refused and left in place until the user confirms a
  rebuild.
- An unreadable file is never overwritten silently: the rebuild prompt says the index could
  not be read.

### 4. Build

- **Input:** the build walks the spine documents from the first verse-bearing document to the
  last. It locates them with the existing Bible book map (`UnitIndexCache::bookFor`).
- **Tokenising:** each document is streamed once through a scanner that combines the
  `VerseAnchors` rules with a text-only tokeniser. That scanner:
  - starts a new verse at each `chapterN_verseM` span;
  - skips `<sup>` inside `<strong>` (the verse number) and every `<a epub:type="noteref">`;
  - stops collecting verse text at the footnote section.
- **Memory:** terms go into a PSRAM hash map, `term → growing postings vector`. A verse number
  is appended once per term per verse.
- **Watchdog:** after each document the build yields and reports progress, so the watchdog is
  never tripped.
- **Checkpoint:** every 100 documents the partial state is written atomically to
  `/.berean/search/bible.partial`: the documents done, the verse table so far, and the postings.
  A cancelled or interrupted build resumes from it.
- **Finish:** terms are sorted, the file is written atomically, and the checkpoint is removed.
- **Peak memory:** about 2–3 MB of PSRAM for the hash map and postings, freed when the build
  ends. The internal SRAM footprint is the parser and a small read buffer.

### 5. Query

- **Term lookup:** each query word is found by binary search in the term table. The table is
  read from SD in small pages; the whole file is never loaded.
- **Prefix:** the last word matches a range of the sorted term table. Its postings are merged,
  capped at 5,000 verses so that a one-letter prefix cannot run away.
- **Intersection:** the word lists are intersected smallest first.
- **Results:** capped at 1,000 verses, held as u16 verse numbers in a PSRAM buffer.
- **Rows:** the verse text for the rows on screen is read from those rows' documents only (about
  8 per page, each a single streamed document). It is cached for the current page and never
  kept past it.

### 6. Components

- **`lib/BibleSearch/`**, pure and host-tested:
  - `Fold` (folding and tokenising);
  - `IndexWriter` (build state, checkpoint, final write);
  - `IndexReader` (header, verse table, term lookup, postings);
  - `Query` (parse, intersect, prefix).
- **Verse-text scanner:** extracts per-verse visible text from one document. Shared by the build
  and the result rows.
- **`BibleSearchActivity`:** the prepare prompt, the progress view, the results list, and the
  hand-off to the reader.
- **`BibleNavigationActivity`:** gains the search entry point on the book level.
- **Docs:** `CLAUDE.md` and `2026-09-13-berean-os-design.md` change their scope wording, and
  `docs/file-formats.md` documents `bible.idx`.

### 7. Error handling

| Condition | Behaviour |
|---|---|
| Out of memory during build | Stop, keep the last checkpoint, tell the user the build could not finish |
| SD write fails | The same; the atomic write leaves the previous file intact |
| Bible replaced or changed | Fingerprint mismatch: prompt to rebuild |
| Index unreadable or newer format | Say so and prompt to rebuild; never overwrite without asking |
| A result's document fails to stream | That row shows the reference only |

### 8. Testing

**Host tests:**

- folding and tokenising, including every Spanish accent and ñ;
- tokenising verbatim NWT markup: footnote markers and verse numbers are excluded, a verse
  split across documents is handled, and text after the footnote section is ignored;
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
