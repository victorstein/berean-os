# Bible Verse Search Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development to implement this plan task by task. Steps use checkbox (`- [ ]`) syntax.

**Goal:** Search Bible verse text by words, with a one-time on-device index, from the Bible navigation screen.

**Architecture:**
- A pure library, `lib/BibleSearch/`, holds the folding, the verse-text scanning, the index format, and the query logic, all host-tested.
- A firmware-side indexer (`src/study/BibleSearchIndexer`) streams spine documents straight from the zip into that library. It yields per document and checkpoints as it goes.
- `BibleSearchActivity` owns the prepare/progress/results UI, and `BibleNavigationActivity` opens it.

**Tech stack:**
- C++20
- ESP32-S3 / PlatformIO, 8 MB PSRAM
- expat
- FreeInkUI (`UiListActivity`, `KeyboardEntryActivity`)
- GoogleTest via CMake

**Spec:** `docs/superpowers/specs/2026-09-24-bible-verse-search-design.md`. Read it in full before any task.

---

## Ground rules for every task

- **Branch:** `feature/bible-verse-search`, in the worktree the orchestrator names. In a fresh worktree run `./bin/bootstrap` once. `pio` is at `/Volumes/stein/.platformio/penv/bin/pio`.
- **Serialise every `pio run` / `pio check`** with the lock:

  ```sh
  L=/tmp/berean-pio.lock
  until mkdir "$L" 2>/dev/null; do sleep 10; done
  <cmd>; rc=$?
  rmdir "$L"
  exit $rc
  ```

- **Host tests:**
  - Run `cmake -S test -B build/test && cmake --build build/test -j && ctest --test-dir build/test --output-on-failure`.
  - The first build on a fresh configure can race gtest; re-run once before believing a failure.
  - Wire new suites with their own `test/<suite>/CMakeLists.txt` and one `add_subdirectory` line in `test/CMakeLists.txt`.
- **`./bin/clang-format-fix` only formats git-tracked files.** `git add` new files first, then run it over the whole tree, then `git diff --exit-code`.
- **CLAUDE.md is binding**, in particular:
  - `makeUniqueNoThrow`, never a bare `new`;
  - stack locals under 256 B;
  - no `std::string` or `std::function` in render hot paths;
  - `tr()` for UI text;
  - storage discipline (atomic writes, byte budget, format version refused when newer);
  - `LOG_ERR` before every error return;
  - comments only for a non-obvious *why*.
- **PSRAM:** allocations over 4096 B land in PSRAM automatically on this build. Large build-time buffers should still use explicit `heap_caps_malloc(..., MALLOC_CAP_SPIRAM)`, or a `std::vector` with a PSRAM allocator, where the code must not depend on that threshold. Justify every allocation over 4 KB in a comment only if the reason is non-obvious.
- **Commits:**
  - Use conventional commit messages.
  - End each message with the line `Claude-Session: https://claude.ai/code/session_01YWFtxUFE3M7RQ1A8C8M6cm`.
  - Add no co-author trailer.
- **Real markup:** `/Volumes/stein/Downloads/nwt_S.epub` is a real Spanish NWT. Extract fixtures from it (`unzip -p … OEBPS/<file>`) rather than inventing markup.

## File map

| File | Task | Responsibility |
|---|---|---|
| `lib/BibleSearch/BibleSearch/Fold.{h,cpp}` | 1 | Fold UTF-8 text and split it into search tokens |
| `lib/BibleSearch/BibleSearch/VerseTextScanner.{h,cpp}` | 2 | Chunk-fed expat scan: per-verse visible text, skipping verse numbers and footnote markers |
| `lib/BibleSearch/BibleSearch/IndexFormat.h` | 3 | On-disk layout constants and structs |
| `lib/BibleSearch/BibleSearch/IndexBuilder.{h,cpp}` | 3 | Accumulate terms and postings, checkpoint, serialise |
| `lib/BibleSearch/BibleSearch/IndexReader.{h,cpp}` | 3 | Header, verse table, term lookup and postings over a byte-source interface |
| `lib/BibleSearch/BibleSearch/Query.{h,cpp}` | 3 | Parse, intersect, prefix, caps |
| `lib/BibleSearch/library.json` | 1 | PlatformIO library manifest, mirroring the existing `lib/*/library.json` |
| `test/bible_search_fold/`, `test/bible_search_scanner/`, `test/bible_search_index/` | 1–3 | Host suites |
| `src/study/BibleSearchIndexer.{h,cpp}` | 4 | Firmware build driver: fingerprint, spine walk, zip streaming, progress, checkpoint, atomic write |
| `src/study/BibleSearchStore.{h,cpp}` | 4 | Opens `bible.idx` through HalStorage, implements the reader's byte source, and states whether the index is ready, stale, unreadable or too new |
| `src/activities/reader/BibleSearchActivity.{h,cpp}` | 5 | Prepare prompt, progress view, keyboard hand-off, results list, jump |
| `src/activities/reader/BibleNavigationActivity.{h,cpp}` | 5 | Search entry on the book level |
| `lib/I18n/translations/english.yaml`, `spanish.yaml` | 5 | New `STR_*` keys |
| `CLAUDE.md` (symlink to `AGENTS.md`), `docs/superpowers/specs/2026-09-13-berean-os-design.md`, `docs/file-formats.md` | 6 | Scope amendment and the index format |

---

### Task 1: Folding and tokenising

**Interface** (`namespace BibleSearch`):

```cpp
// Lower-cases and strips Latin diacritics (Latin-1 Supplement and Latin
// Extended-A: á→a … ñ→n, ç→c, ü→u, ß→ss, æ→ae, œ→oe). Other scripts pass
// through unchanged. Output is valid UTF-8.
void foldAppend(std::string& out, std::string_view utf8);

// Splits already-folded text into tokens: maximal runs of letters or digits
// (any non-ASCII codepoint that survives folding counts as a letter).
// Tokens shorter than MIN_TOKEN_BYTES (2) or longer than MAX_TOKEN_BYTES
// (32, cut on a UTF-8 boundary) are handled as documented; `emit` is a
// function pointer + ctx, not std::function.
using TokenSink = void (*)(void* ctx, std::string_view token);
void tokenize(std::string_view folded, TokenSink sink, void* ctx);

// Convenience for queries: fold + tokenize into a small vector.
std::vector<std::string> queryTokens(std::string_view rawQuery);
```

Implement `foldAppend` with a `constexpr` table indexed by codepoint for U+00C0..U+017F, so it lives in flash; there is no runtime map. Punctuation, including `“ ” ‘ ’ ¡ ¿ « » — …` and the ASCII punctuation, separates tokens.

- [ ] Write `test/bible_search_fold/FoldTest.cpp` with at least these cases:
  - `Señor`→`senor`, `corazón`→`corazon`, `PINGÜINO`→`pinguino`, `Ça`→`ca`;
  - mixed case;
  - an already-folded string is idempotent;
  - Greek and Cyrillic text passes through lower-cased where ASCII-simple, or untouched otherwise. State the choice in the test;
  - `“¡Quiten todo esto de aquí!”` tokenizes to `quiten todo esto de aqui`;
  - one-letter tokens (`y`, `a`) are dropped;
  - digits are kept (`46`);
  - a 40-byte word is cut to ≤32 bytes on a boundary;
  - `queryTokens("  amor   PACIENTE ")` gives `{"amor","paciente"}`.
- [ ] Run it and see it fail. Implement. Run it and see it pass.
- [ ] Commit: `feat: fold and tokenize text for Bible search`.

### Task 2: Verse-text scanner

**Interface:**

```cpp
struct VerseText {
  uint16_t chapter;
  uint16_t verse;
  uint32_t anchorOffset;  // VerseAnchors' visible-codepoint offset of the verse marker
  std::string text;       // visible verse text, raw (unfolded), whitespace-collapsed
};

class VerseTextScanner {
 public:
  VerseTextScanner();
  ~VerseTextScanner();
  bool valid() const;
  bool feed(const char* chunk, size_t length, bool isFinal);
  // Verses completed so far, in document order; moved out. Empty after a failed feed.
  std::vector<VerseText> take();
};
```

Rules, all taken from the real markup (see the spec's "Measurements"):
- **A verse starts** at an element with `id="chapter<N>_verse<M>"`. Reuse or mirror the id grammar in `VerseAnchors.cpp:30-41`.
- **`anchorOffset` must equal the offset `VerseAnchors::Scanner` reports** for the same marker. The simplest correct approach is to feed the same chunks to an embedded `VerseAnchors::Scanner` and pair its anchors with the verses in order. Do not re-implement `VisibleOffsetCounter`; that is exactly the entity-counting trap `VerseAnchors.h:12-16` warns about.
- **Excluded from verse text:**
  - the verse number: `<sup>` inside the `<strong>` right after the marker. Exclude any `<sup>`;
  - `<a epub:type="noteref">` and any `<a>` whose text is a lone `*` or `+`;
  - everything after the footnote section starts. Find the element the NWT uses to open footnotes (inspect a real chapter such as `OEBPS/1001061147-split2.xhtml`), and stop collecting there.
- **Where a verse ends:** at the next verse marker, at the footnote section, or at the end of the document. A verse whose start is in a previous document (a split chapter) is not the scanner's concern. Text before the first marker in a document belongs to the previous document's last verse. Collect it as a leading "continuation" string that `take()` exposes separately as `std::string continuation()`, so the build can append it.
- **Text cap:** each verse's text is capped at 4 KB, cut on a UTF-8 boundary, so a malformed page cannot grow memory without bound.

- [ ] Create `test/bible_search_scanner/` with fixtures extracted verbatim from `nwt_S.epub`:
  - John 2's document, `1001061147-split2.xhtml`, trimmed to a few verses plus its footnote section;
  - a document that starts mid-verse (find one whose first text precedes its first marker);
  - Psalm 119, for size.
- [ ] Test cases:
  - Verse 16's text is exactly `Y a los que vendían palomas les dijo: “¡Quiten todo esto de aquí! ¡Dejen de convertir la casa de mi Padre en un mercado!”.`, with no `16` and no `*`.
  - `anchorOffset` equals `VerseAnchors::scan(...)` for every verse.
  - Footnote text never appears.
  - The continuation text is captured.
  - A malformed document gives an empty result.
  - 1-byte chunk feeding gives the same output as whole-document feeding.
- [ ] Run and see it fail. Implement. Run and see it pass. Commit: `feat: scan Bible verse text for search`.

### Task 3: Index format, builder, reader, query

**On-disk layout** (`IndexFormat.h`, all little-endian and packed, as specified in the spec §3):

- **Header:**
  - `magic "BSIX"`, `formatVersion` (u16, starts at 1), `flags` (bit 0 = complete);
  - `fingerprint` (u64), `verseCount` (u32), `termCount` (u32);
  - `docsDone` (u32, for checkpoints);
  - the offsets of the verse table, term table, term strings and postings.
- **Verse entry:** 9 B (`book` u8, `chapter` u8, `verse` u8, `spine` u16, `offset` u32). The chapter must fit in u8: the maximum is 150.
- **Term entry:** `stringOffset` u32, `postingsOffset` u32, `postingCount` u16. Terms are sorted by folded bytes (`memcmp` order).
- **Postings:** ascending verse numbers, as LEB128 deltas from the previous one. The first delta is taken from 0.

**Builder** (pure; it knows nothing of HalStorage):

```cpp
class IndexBuilder {
 public:
  // Records a verse and returns its global number. Verses must arrive in canonical order.
  uint32_t addVerse(uint8_t book, uint8_t chapter, uint8_t verse, uint16_t spine, uint32_t offset);
  // Tokenizes (fold + tokenize) and records each distinct term once for this verse.
  void addVerseText(uint32_t verseNumber, std::string_view rawText);
  // Appends continuation text to the most recently added verse.
  void appendToLastVerse(std::string_view rawText);
  // Serialises the complete index. The byte sink is a function pointer + ctx.
  bool write(ByteSink sink, void* ctx, uint64_t fingerprint) const;
  // Checkpoint: the same format with the complete flag clear and docsDone set.
  bool writeCheckpoint(ByteSink, void*, uint64_t fingerprint, uint32_t docsDone) const;
  // Restores a checkpoint written by writeCheckpoint.
  bool loadCheckpoint(ByteSource&, uint64_t expectedFingerprint, uint32_t& docsDoneOut);
  size_t estimatedBytes() const;
};
```

- **Storage:** an `std::unordered_map<std::string, std::vector<uint16_t>>` on a PSRAM allocator is fine. Verse numbers fit in u16, since there are about 31,100 verses. Document this assumption in a `static_assert`, or fail loudly past 65,535.
- **Per-verse de-duplication:** record the last verse number for each term, so a term repeated inside one verse is recorded once.
- **Continuation:** `appendToLastVerse` must de-duplicate against terms already recorded for that verse.

**Reader** (pure, over an abstract byte source so the host tests use memory and the firmware uses HalFile):

```cpp
struct ByteSource {
  void* ctx;
  bool (*readAt)(void* ctx, uint32_t offset, void* dst, uint32_t len);
  uint32_t size;
};
class IndexReader {
 public:
  enum class Status { Ok, Missing, Unreadable, TooNew, Stale, Incomplete };
  Status open(const ByteSource&, uint64_t expectedFingerprint);
  uint32_t verseCount() const;
  bool verse(uint32_t n, VerseEntry& out) const;
  // Binary search over the term table, reading small pages; never loads the whole table.
  bool findExact(std::string_view foldedTerm, TermEntry& out) const;
  // [first, last) range of terms that start with `foldedPrefix`.
  bool findPrefixRange(std::string_view foldedPrefix, uint32_t& first, uint32_t& last) const;
  bool term(uint32_t index, TermEntry& out) const;
  // Decodes one term's postings, appending to `out`, stopping at `cap`.
  bool postings(const TermEntry&, std::vector<uint16_t>& out, size_t cap) const;
};
```

**Query:**

```cpp
struct QueryResult { std::vector<uint16_t> verses; bool truncated; };
// Tokenize the query. Every full token must match exactly; the LAST token
// matches by prefix. The prefix union is capped at PREFIX_CAP (5000) verses,
// and results at RESULT_CAP (1000), with truncated set. Intersection runs
// smallest-first. Output is in ascending (canonical) order.
QueryResult runQuery(const IndexReader&, std::string_view rawQuery);
```

- [ ] Create `test/bible_search_index/` covering:
  - a round trip on a synthetic three-chapter corpus built through `IndexBuilder`, with exact result sets for single words, two words, a prefix, and a word with and without accents;
  - a term that appears twice in one verse is recorded once;
  - a continuation append;
  - the prefix cap and the result cap, with `truncated` set;
  - `TooNew` for version+1;
  - `Stale` for a fingerprint mismatch;
  - `Incomplete` for a checkpoint;
  - checkpoint then resume gives a byte-identical final file to an uninterrupted build;
  - `Unreadable` for a truncated file.
- [ ] Add one realistic test: build from the Task 2 fixtures through `VerseTextScanner`, then query `palomas` and `quiten mercado`.
- [ ] Run and see the tests fail, then implement and see them pass.
- [ ] Commit: `feat: build and query the Bible search index`.

### Task 4: Firmware indexer and store

- **`BibleSearchStore`** (main task only, all I/O through HalStorage):
  - Paths are `/.berean/search/bible.idx` and `/.berean/search/bible.partial`.
  - The store computes the fingerprint: `epub->getPath()` plus the file size and modification time from HalStorage, hashed to u64 with FNV-1a.
  - `IndexReader::Status status()` is cheap: it reads the header only.
  - `open()` returns a ready `IndexReader` whose ByteSource wraps a member `HalFile` (closed in `close()`).
- **`BibleSearchIndexer`** is a step machine. It does not use a task; `BibleSearchActivity::loop()` calls it once per loop pass.
  - `begin()` finds the verse-bearing spine range. Use `UnitIndexCache` or `STUDY`'s book map (`bookFor`); read `src/study/UnitIndexCache.h` and `StudyStore.h` for the accessor. It sets `totalDocs` and loads the checkpoint if its fingerprint matches.
  - `step()` indexes exactly one spine document:
    - It streams the document with `epub->readItemContentsToStream(href, printSink, 4096)` straight into a `VerseTextScanner`. Do not use `SpineHtmlStream`: it goes through `Section` and would write about 20 MB of HTML cache to SD.
    - It adds each verse to the builder and appends the continuation.
    - Then it returns.
  - Every 100 documents it writes the checkpoint atomically.
  - `finish()` writes `bible.idx` through a temp file and a rename (mirror `writeDocToFileAtomic`'s pattern for a binary file, or add a small binary equivalent in `lib/Serialization` if none exists). It checks a byte budget of 8 MB before writing and removes the checkpoint afterwards.
  - It exposes `docsDone`, `totalDocs`, `currentBook` (a canonical number), `currentChapter`, and `failed` with a reason.
  - One `step()` must stay well under the 5 s watchdog. The largest document is about 69 KB, so one inflate and parse is well inside it. Log `millis()` per step at `LOG_DBG`.
  - **Memory:** `LOG_INF` the free internal heap and free PSRAM before, during (every 500 documents) and after the build.
- Add `BibleSearchIndexer` only to the firmware build. It is not host-testable because it uses HalStorage and Epub; the pure parts are covered by Task 3.
- **Pre-commit gate:** `pio run` and `pio check` must be clean before committing.
- [ ] Commit: `feat: index the Bible for search on the device`.

### Task 5: UI

**`BibleSearchActivity`**, launched from `BibleNavigationActivity` with the `epub`. It returns a `ChapterResult{spine, "", offset}` exactly as the verse grid does (`BibleNavigationActivity::finishWith`). `BibleNavigationActivity` forwards that result to the reader by finishing with it too.

States:
1. **Prepare prompt.** Shown when `status()` is not `Ok`. The message varies by status:
   - missing: "not prepared yet";
   - stale: "the Bible changed";
   - unreadable or too new: "could not be read".
   Confirm and Cancel.
2. **Progress view** (the spec §1a, in full):
   - a title and one explanatory line;
   - `GUI.drawProgressBar` with a real percentage;
   - `Book · chapter N` (book name from the TOC join, as `BibleNavigationActivity::loadBooks` does);
   - `~N min left`, shown only after 5% and computed from the measured rate;
   - a Cancel control, and Back (left-edge swipe) cancels too.
   Refresh rules:
   - at most one render per whole percentage point, and no more than one every 2000 ms;
   - FAST refresh while in progress, and a FULL refresh on entry and on completion;
   - the indexer steps in `loop()` and `requestUpdate()` is throttled, so rendering never blocks the build.
   Cancel stops the build and writes the checkpoint. It then returns to the navigation screen.
3. **Done.** "Ready" is shown briefly, then the keyboard opens automatically.
4. **Query.** `KeyboardEntryActivity(title=tr(STR_SEARCH_BIBLE), initial=lastQuery, max 64)`. A cancelled keyboard returns to the navigation screen.
5. **Results.** A `UiListActivity` list:
   - The header is `Results: N`, or `N+` when truncated.
   - Rows use `label` = reference (`Juan 3:16`) and a subtitle or second line = the start of the verse text.
   - Verse text for the visible rows only: stream each row's spine through `VerseTextScanner` once per page, grouping rows by spine. Cache the page's strings in fixed `char[]` buffers sized for about 120 bytes per row. Prewarm fallback glyphs, as the list screens do.
   - With no results, an empty-state message plus an "Edit search" row that reopens the keyboard.
   - Tapping a row finishes with `ChapterResult`.
   - A long query or result list must not allocate per repaint.

`BibleNavigationActivity`: add the search entry to the book level in the way that fits its existing chrome. A header right-label, or a footer hint action, are both acceptable; pick the one the ui-dev agent definition and the theme support without new widgets. The entry must be reachable by touch.

i18n: add keys to `english.yaml` and `spanish.yaml` (Spanish, because it is the user's language), beside related keys. The keys cover:
- the prepare message variants;
- the progress title, explanation, status line format, time-left format and Ready;
- the search title;
- the results header format;
- no results;
- Edit search;
- the search entry label.

Format strings take `%d`/`%s` only.

- [ ] Pre-commit gate: `pio run`, `pio check`, the host suite and the format check must all be clean before committing.
- [ ] Commit: `feat: search Bible verses from the navigation screen`.

### Task 6: Docs, verification, PR

- **`CLAUDE.md`** (edit `AGENTS.md`; `CLAUDE.md` is a symlink to it):
  - Change "no on-device full-text search" (lines 8-9 and 114) to allow Bible verse search only. Other publications and footnotes stay out.
- **The founding design** `2026-09-13-berean-os-design.md:84-85`:
  - Amend the non-goal to "full-text search beyond Bible verses".
  - Link the new spec.
- **`docs/file-formats.md`:** add a `bible.idx` section with the layout from Task 3.
- **Full gates:**
  - host suite;
  - locked `pio run` and `pio check`;
  - whole-tree format.
- **PR body:** file:line references, the verification counts, the deviations, and a numbered device checklist from the spec §8. End with `https://claude.ai/code/session_01YWFtxUFE3M7RQ1A8C8M6cm`.
- The orchestrator pushes, opens the PR and merges it.
