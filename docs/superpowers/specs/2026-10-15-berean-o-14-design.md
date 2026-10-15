# Design: `lib/Preaching` — the intro section and its question parser (berean-o-14)

Task `t1` of run `berean-os-20261015-predicaci-n-193x`. Surface: `epub`. Tier: `standard`.
Parent spec: `docs/superpowers/specs/2026-10-14-preaching-tool-design.md` §1, §4 (follow-up
table), D2, D7. Research: `docs/superpowers/research/2026-10-15-berean-o-14-research.md`.

**Modelled on:** `lib/Epub/Epub/BibleNavScanner.{h,cpp}` for the shape (chunk-fed, expat behind an
opaque pointer, `feed`/`take`, nothing partial after a failure) and its suite
`test/bible_nav_scanner/`. Allocation, logging and entity handling come from
`lib/BibleSearch/BibleSearch/VerseTextScanner.cpp`, because `BibleNavScanner` uses a bare
`new (std::nothrow)` with no `LOG_ERR` (`BibleNavScanner.cpp:125`) and has no entity handler
(`:137-138`). The library layout mirrors `lib/StudyStore/StudyStore/`.

## Problem

T3's Predicación screens need two things from the installed NWT EPUB: where the twenty preaching
questions are, and each question's content as quotes, references, headings and prose. Nothing in
the repo knows about the intro section yet (research §1). Today it can only be reached as ordinary
spine items through the TOC.

## Goal

A host-testable library, `lib/Preaching/Preaching/`, included as `<Preaching/...>`, with no
Arduino and no `HalStorage`:

1. `IntroSection` — the 21 basenames as `constexpr` data, `nextQuestion(n)` and
   `stripTocPrefix(label)`.
2. `QuestionParser` — a chunk-fed expat parser over one question file. It produces a title and an
   ordered list of `Heading`, `Verse` and `Text` blocks. Past 64 blocks or 8 KB it refuses
   (returns false) and keeps nothing.
3. `test/preaching/`, wired into `test/CMakeLists.txt`.

## Non-goals

- No `src/` code, UI, strings or `tr()` (T3). This library shows nothing to the user.
- No reading from the EPUB. T3 streams with `SpineHtmlStream::stream`
  (`src/activities/reader/SpineHtmlStream.h:40`) and writes the sink adapter, as
  `src/study/UnitIndexCache.cpp:23-41` does for its scanners.
- No tag topics (T2), and nothing written to SD, so no store, budget or format version.
- No parsing of the index page (`1001061250.xhtml`, slot 0). It is a list of links in `<li>`, not a
  question; T3 lists the questions from the TOC (parent spec §3).
- No resolving of hrefs to spine indices or verses. The href is kept verbatim for T7's
  `navigateToHref`.
- No rich text. Emphasis (`<strong>` in Q19's prose, research §4.4) is flattened to plain text.

## Architecture

```
lib/Preaching/Preaching/
  IntroSection.h      constexpr basenames, nextQuestion; stripTocPrefix declared
  IntroSection.cpp    stripTocPrefix
  QuestionParser.h    Block, Question, QuestionParser — no expat in the header
  QuestionParser.cpp  expat handlers, the State, pairing and the bounds
test/preaching/
  CMakeLists.txt
  IntroSectionTest.cpp
  QuestionParserTest.cpp
test/CMakeLists.txt   + add_subdirectory(preaching)
```

There is no `library.json`. `lib/StudyStore/` has none either and builds from `src/` includes such as
`src/Place.h:3` `#include <StudyStore/Unit.h>`. Everything lives in `namespace preaching`, lower
case like `study` (`lib/StudyStore/StudyStore/Unit.h:21`).

Dependencies, all present in the repo:

- `<expat.h>` (vendored 2.7.3).
- `"Epub/htmlEntities.h"` for `lookupHtmlEntity` (`lib/Epub/Epub/htmlEntities.h`), included the way
  `lib/StudyStore/StudyStore/UnitText.cpp:11` does.
- `<Memory.h>` (`makeUniqueNoThrow`).
- `<Logging.h>`, stubbed to a no-op on the host by `test/stubs/Logging.h`.

### `IntroSection.h`

```cpp
namespace preaching::IntroSection {

inline constexpr int QUESTION_COUNT = 20;
inline constexpr int SLOT_COUNT = QUESTION_COUNT + 1;
inline constexpr int INDEX_SLOT = 0;

// Slot 0 is the section's index page; slot n (1..20) is question n.
inline constexpr const char* BASENAMES[SLOT_COUNT] = {
    "1001061250.xhtml", "1001061251.xhtml", /* … */ "1001061270.xhtml"};

// The question to offer for a return visit after `question`; 0 for none or out of range.
constexpr int nextQuestion(int question);

// "Pregunta 8: ¿…?" -> "¿…?"; anything else whole.
std::string_view stripTocPrefix(std::string_view label);

}
```

`nextQuestion` is `constexpr`. It is defined in the header over a `constexpr` table of 21 `uint8_t`
(index 0 unused), so it costs no DRAM and can be checked with `static_assert` in the test.

| From | 1–6 | 7 | 8–11 | 12 | 13–15 | 16 | 17 | 18 | 19 | 20 |
|---|---|---|---|---|---|---|---|---|---|---|
| To | n+1 | 10 | n+1 | 18 | n+1 | 18 | 18 | 20 | 20 | 0 |

This is parent spec §4 written out for every row.

### `QuestionParser.h`

```cpp
namespace preaching {

inline constexpr size_t MAX_BLOCKS = 64;
inline constexpr size_t MAX_TEXT_BYTES = 8 * 1024;

enum class BlockKind : uint8_t { Heading, Verse, Text };

struct Block {
  BlockKind kind = BlockKind::Text;
  std::string text;       // Heading/Text: the text. Verse: the quote, empty when none.
  std::string reference;  // Verse only: the reference paragraph's text, e.g. "Mateo 26:14, 15; 27:5".
  std::string href;       // Verse only: the first link's href, verbatim.
};

struct Question {
  std::string title;
  std::vector<Block> blocks;
};

class QuestionParser {
 public:
  QuestionParser();
  ~QuestionParser();
  QuestionParser(const QuestionParser&) = delete;
  QuestionParser& operator=(const QuestionParser&) = delete;

  bool valid() const;
  // The SpineHtmlStream::ChunkSink contract: false stops the pass, and every
  // later call returns false too. `isFinal` marks the last chunk, which may be empty.
  bool feed(const char* chunk, size_t length, bool isFinal);
  // True when a failed feed ran out of memory rather than met bad markup or a bound.
  bool outOfMemory() const;
  // Moves the result into `out` only after a final chunk was accepted; otherwise
  // returns false and leaves `out` untouched.
  bool take(Question& out);

 private:
  void* parser_ = nullptr;  // XML_Parser; opaque to keep expat out of the header
  std::unique_ptr<QuestionParserState> state_;
  bool failed_ = false;
  bool finished_ = false;
  bool outOfMemory_ = false;
};

// One-shot convenience for whole buffers, as BibleNav::scan is (BibleNavScanner.h:70).
bool parseQuestion(const char* xhtml, size_t length, Question& out);

}
```

The `void* parser_` and `std::unique_ptr<…State>` members match `VerseTextScanner.h:70-71`.
`outOfMemory()` matches `VerseTextScanner.h:58`.

## Data and control flow

T3 drives the parser one chunk at a time. `SpineHtmlStream` sends chunks of up to 2,048 B
(`SpineHtmlStream.cpp:14`) and may finish with an empty final call (`:66`):

```
SpineHtmlStream::stream ─chunk→ feedQuestion(ctx,…) ─→ QuestionParser::feed ─→ XML_Parse
                                                                   │ handlers mutate State
                                       after isFinal & no failure: take(Question&) → T3 lays out
```

### Constructor

As in `VerseTextScanner.cpp:307-336`:

- `makeUniqueNoThrow<QuestionParserState>()`; on null, `LOG_ERR("PREACH", "OOM: question parser state")`
  and return with `valid() == false`.
- `state->blocks.reserve(MAX_BLOCKS)`.
- `XML_ParserCreate(nullptr)`; on null, `LOG_ERR` and return with `valid() == false`.
- Set the element and character-data handlers, plus
  `XML_SetDefaultHandlerExpand(parser, onDefault)` for entities.

The destructor frees the parser with `XML_ParserFree`, as `BibleNavScanner.cpp:145` does.

### State, per document

| Field | Meaning |
|---|---|
| `skipDepth` | >0 while inside a skipped subtree; every start increments it and every end decrements it |
| `collect` | which block is collecting text: none, title, heading, or paragraph |
| `collectDepth` | element depth inside the collecting element, so text in `<strong>`/`<a>`/`<q>` is kept |
| `buffer` | the collapsed text of the element being collected |
| `pendingSpace` | a whitespace run was seen since the last character stored |
| `paragraphHasQuote`, `paragraphHasNonLinkText`, `paragraphLinkDepth`, `paragraphHref` | classification inputs for the open `<p>` |
| `pendingQuote`, `hasPendingQuote` | a quote paragraph waiting for its reference |
| `title`, `blocks` | the result |
| `committedBytes` | bytes stored so far in `title` and in every block's `text`, `reference` and `href`, counting `pendingQuote` once it is committed |
| `overLimit` | latched when a bound is exceeded |

### Start element

1. If `skipDepth > 0`, increment it and return.
2. If the element is skipped (A4), set `skipDepth = 1` and return.
3. If a block is collecting, increment `collectDepth`. Inside a paragraph:
   - `<q>` whose `class` has the token `scrp` sets `paragraphHasQuote`.
   - `<a>` enters link depth. The first `<a>` with an `href` in the paragraph sets `paragraphHref`
     verbatim.
   - Then return.
4. Otherwise a block may open:
   - The first `<h1>` opens the title. A later `<h1>` opens a heading (A5).
   - `<h2>`–`<h6>` open a heading (A3).
   - `<p>` opens a paragraph.
   - Anything else does nothing, and its text is not collected (A6).

Class tokens are matched with a file-local `hasToken`/`attribute` pair. The same helpers exist as
file-local copies in `VerseTextScanner.cpp:44,57` and `UnitText.cpp:37,45`.

### Character data

Character data is dropped unless a block is collecting and `skipDepth == 0`. When expat routes an
entity to `onDefault`, it reaches the same path exactly as `VerseAnchors.cpp:46-60` handles it: a
known entity becomes its UTF-8 value, and an unknown one is kept literally. Expat only routes it
there when the document declares an external subset; without a DOCTYPE an undeclared entity is a
hard parse error and the question is refused (A9).

Whitespace collapses (A8):

- An ASCII space, tab, CR or LF sets `pendingSpace` only when `buffer` is non-empty.
- Any other byte first writes one `' '` if `pendingSpace` is set, then itself.

So `buffer` never has leading or trailing whitespace, and its size while growing equals its final
size. Inside a paragraph, a non-whitespace byte outside any `<a>` sets `paragraphHasNonLinkText`.
Before each store, `committedBytes + pendingQuoteBytes + buffer.size() + n > MAX_TEXT_BYTES` latches
`overLimit` and calls `XML_StopParser(parser, XML_FALSE)`. A single oversized paragraph is therefore
refused before it finishes growing (A12).

### End element

1. If `skipDepth > 0`, decrement it and return.
2. If collecting and `collectDepth > 0`, decrement it (and the link depth on `</a>`), then return.
3. Otherwise the collecting element closes, and the block is classified.

**Title.** It becomes `title`. It goes through pairing first (see below).

**Heading.** If the text is non-empty, pairing runs first, then a `Heading` block is appended. An
empty heading is dropped (A10).

**Paragraph.** Classified in this order:

1. **Empty** (`buffer` empty after collapsing). Dropped, and pairing does not move (A10).
2. **Reference**: at least one `<a href>`, and no non-whitespace text outside links (A7).
   - If a quote is pending, append `Verse{pendingQuote, buffer, paragraphHref}` and clear it.
   - If none is pending, append `Verse{"", buffer, paragraphHref}`.
3. **Quote**: `paragraphHasQuote`. A quote that is already pending is flushed as `Text`. The new
   quote's text (the whole paragraph, A11) becomes the pending quote.
4. **Text**: anything else. Pairing runs first, then a `Text` block is appended.

**Pairing.** Before any title, heading or text is stored, and at the end of the document, a quote
that is still pending is appended as `Text`. That keeps document order: a quote with no reference
after it reads as text where it stood.

**Bounds.** An append that would make `blocks.size()` exceed `MAX_BLOCKS` latches `overLimit` and
stops the parser. The title is not a block. `href` bytes count towards `MAX_TEXT_BYTES` when the
`Verse` is committed.

### `feed`

```
if (!parser_ || failed_ || finished_) return false;
status = XML_Parse(parser, chunk, length, isFinal);
if (status != XML_STATUS_ERROR && isFinal) flush the pending quote as Text;  // bounds apply
if (state->overLimit) {
  LOG_ERR("PREACH", "Question refused: over %zu blocks or %zu bytes", MAX_BLOCKS, MAX_TEXT_BYTES);
  fail();
  return false;
}
if (status == XML_STATUS_ERROR) {
  outOfMemory_ = XML_GetErrorCode(parser) == XML_ERROR_NO_MEMORY;
  LOG_ERR("PREACH", "Question parse failed: %s", XML_ErrorString(XML_GetErrorCode(parser)));
  fail();  // failed_ = true; clear the state's title, blocks and pendingQuote
  return false;
}
if (isFinal) finished_ = true;
return true;
```

The end-of-document flush runs **before** the bounds check, so a trailing unpaired quote that
crosses a bound is refused like any other overflow. Each `LOG_ERR` has its own literal format:
the device macro pastes `format "\n"` and is printf-checked (`lib/Logging/Logging.h:42,46`), so a
computed format would not compile there, while the host stub expands to nothing
(`test/stubs/Logging.h:6`) and would not catch it.

A non-resumable `XML_StopParser` makes `XML_Parse` return `XML_STATUS_ERROR` with
`XML_ERROR_ABORTED` (`lib/expat/expat.h:763`). `feed` checks `overLimit` as well, so the refusal
does not depend on that. The error log uses `XML_ErrorString`, as
`lib/ProgressMapper/ChapterXPathResolver.cpp:104` does.

### `take`

Succeeds only when `finished_` is set and `failed_` is not. It moves `title` and `blocks` into
`out`, then disarms itself, so a second call returns false. A failed or unfinished parse never
writes to `out`: this is the "nothing partial" contract.

## Assumptions (each is a decision the review can attack)

| | Assumption | Why |
|---|---|---|
| A1 | `IntroSection::BASENAMES` is `constexpr const char*[21]`, not `std::string` | `constexpr` cannot hold `std::string`. T3 builds the `std::string[21]` that `Epub::resolveFilenamesToSpineIndices(const std::string*, int*, int)` takes (`lib/Epub/Epub.h:93`) |
| A2 | `stripTocPrefix`: one or more bytes other than U+0020; one U+0020; one or more ASCII digits; `": "`; then **a non-empty remainder**. Otherwise the label is returned whole. The result is a view into the input | The parent grammar (§1). A label like "Pregunta 8: " would otherwise strip to an empty row. The real TOC uses plain ASCII spaces (`toc.xhtml`: `50 7265 6775 6e74 6120 383a 20` = "Pregunta 8: ") |
| A3 | `<h2>` through `<h6>` are all `Heading` | Q19's ten headings are 2 `<h2>` and 8 `<h3>` (research §4.1). The parent spec counts all ten as headings; handling only `<h2>` would drop eight silently. `h4`–`h6` do not occur; they are included so nothing is lost |
| A4 | Skipped subtrees, with nothing inside collected: `<p>` with class token `contextTtl`; `<figure>`; `<span>` with class token `pageNum` | Brief and parent §1. `pageNum` occurs inside `contextTtl`, inside a quote paragraph and inside an `<h2>` (research §3), so it is skipped wherever it appears |
| A5 | The first `<h1>` is the title; a later `<h1>`, if any, is a `Heading`. No `<h1>` means an empty title, which is still a success | Every question has exactly one `<h1>` (21 in 21 files, research §3); this keeps text rather than dropping it |
| A6 | Only text inside `<h1>`–`<h6>` and `<p>` is collected. Text directly in `<div>`, `<li>`, `<header>` or `<title>` is ignored | Every visible run in Q1–Q20 sits in one of those (research §3 survey). It keeps `<title>` and `alt` out without special cases |
| A7 | A **reference paragraph** has at least one `<a>` with an `href`, and no non-whitespace character data outside its `<a>` elements. Its `reference` is the collapsed text of the whole paragraph, so the link texts are joined with whatever whitespace stands between them. `href` is the first `<a href>`, verbatim | The texts carry their own separators: "Eclesiastés 9:5," + " 10" → "Eclesiastés 9:5, 10" (research §3). A prose paragraph with an inline link stays `Text`, and its href is dropped |
| A8 | Whitespace: runs of ASCII space/tab/CR/LF collapse to one space, with none leading or trailing. U+00A0, U+202F and other non-ASCII spaces are kept as they are | The same rule and the same four bytes as `BibleNavScanner.cpp:36-58` (`:40`). The non-breaking spaces are meaningful typography in this publisher's markup |
| A9 | In a document with an external-subset DOCTYPE, known HTML entities become UTF-8 through `lookupHtmlEntity` and unknown ones are kept literally, `&` through `;`. Without a DOCTYPE an undeclared entity is a parse error and the question is refused, with nothing partial | Under `XML_GE=0` (`platformio.ini:38`) expat reports an undeclared entity to the default handler only when the document declared an external subset; otherwise it returns `XML_ERROR_UNDEFINED_ENTITY` (`lib/expat/xmlparse.c:3037-3039`). The repo documents and tests both sides (`test/verse_anchors/VerseAnchorsTest.cpp:34-53`), and `VerseAnchors.cpp:46-60` and `VerseTextScanner.cpp:191-203` behave the same way. The real question files have no DOCTYPE and no entities (research §3), so today neither path is taken. A future edition that adds an entity without a DOCTYPE would make that question fail to open rather than show wrong text |
| A10 | Empty paragraphs and empty headings are dropped and do not break a pending quote/reference pair | 13 of 21 files end in an empty `<p>` (research §4.2) |
| A11 | A quote's text is the whole paragraph's collapsed text, including any text outside `<q>` | Nothing visible is lost. In the real files the quote paragraph holds only the `<q>`, with its curly quotes inside it |
| A12 | `MAX_TEXT_BYTES` (8,192) counts the stored bytes of `title` plus every block's `text`, `reference` and `href`. A store that would exceed it is refused. `MAX_BLOCKS` (64) counts `blocks` only. Exactly 64 blocks and exactly 8,192 bytes are accepted | It bounds what the result holds in memory. The largest real file is 33 blocks and ~1.7 KB (research §3) |
| A13 | `blocks.reserve(MAX_BLOCKS)` at construction | `CLAUDE.md`'s reserve rule. The cap is known, so the vector never regrows. On the ESP32-S3, `std::string` is 24 B, so a `Block` is about 76 B and the reservation about 4.9 KB |
| A14 | `take(Question&)` returns `bool` and writes only on success, unlike `BibleNav::Scanner::take()` (`BibleNavScanner.h:58`), which returns a possibly empty value | An empty `Question` is a legitimate result, so a failure needs its own signal |
| A15 | The parser is single-use; feeding after `isFinal` returns false | One question per parser, as each `UnitIndexCache` pass makes a new scanner (`UnitIndexCache.cpp:23-41`) |
| A16 | The `<q>` test is the class token `scrp`, not any `<q>` | The brief and parent spec name `<q class="scrp">`. All 104 `<q>` elements in the section carry it (research §3) |

## Error handling

| Condition | `feed` | `take` | `outOfMemory()` | Log |
|---|---|---|---|---|
| State or parser allocation fails | false (`valid()` is false) | false | — | `LOG_ERR` OOM |
| Malformed XHTML | false, latched | false | false | `LOG_ERR` with `XML_ErrorString` |
| expat OOM | false, latched | false | true | `LOG_ERR` |
| Over 64 blocks or 8 KB | false, latched | false | false | `LOG_ERR` refusal |
| Feed after failure, or after the final chunk | false | unchanged | unchanged | none |
| Success | true | true once | false | none |

Every failure clears the collected state, so nothing partial survives.

String growth uses the default allocator, like every existing scanner's `std::string`
(`BibleNavScanner.cpp:42-44`). Under `-fno-exceptions` a failed growth aborts. The bounds limit
growth to 8 KB of text, and that limit is the mitigation; no other scanner in the repo does more.

## Memory

Each parser allocates:

- one `State`, about 5 KB, mostly the reserved `blocks`;
- one expat parser, whose buffer is bounded by `XML_CONTEXT_BYTES=1024` and the 2 KB chunk;
- at most 8 KB of strings.

`take` moves the 64-slot `blocks` vector (about 4.9 KB) and its strings into the result
`Question`, so they live as long as T3's screen does. The parser frees only the expat parser and
the rest of its `State`. At over 4,096 B the reservation is routed to PSRAM by this build's malloc
threshold (`CLAUDE.md`, "PSRAM is a licence to budget deliberately"), not the internal heap that
parent spec §9 assumes for a question. It is touched only when the cards are laid out, never from an
ISR. None of it is static, and there is nothing in
IRAM, ISRs or PSRAM-specific code. The `constexpr` tables sit in flash.

## Testing strategy

TDD. Each case is written failing first. Suite `test/preaching/`, shaped like
`test/bible_nav_scanner/CMakeLists.txt:4-25`, plus what `test/bible_search_scanner/CMakeLists.txt:4-21`
adds:

- sources: `IntroSection.cpp`, `QuestionParser.cpp`, `lib/Epub/Epub/htmlEntities.cpp`, and expat's
  `xmlparse.c`, `xmlrole.c`, `xmltok.c`;
- include directories: `lib/Preaching`, `lib/Epub`, `lib/Memory`, `lib/expat`, `test/stubs`;
- definitions: `XML_GE=0 XML_CONTEXT_BYTES=1024`;
- links: `crosspoint_test_common` (`-Wall -Wextra -pedantic`, `test/CMakeLists.txt:42-46`) and
  `GTest::gtest_main`.

Fixtures are inline strings: short excerpts of a few lines of real markup wrapped in a minimal
`<html><body>`. No whole file is copied, because the repo is public.

**`IntroSectionTest.cpp`**

- `BASENAMES[0]` is `1001061250.xhtml`, `BASENAMES[20]` is `1001061270.xhtml`, and every slot
  equals `10010612` followed by `50 + n` and `.xhtml`.
- `nextQuestion` for every n in 1–20 against the A-table, as `static_assert`s and as runtime
  `EXPECT`s. Also 0, -1 and 21 → 0.
- `stripTocPrefix`, matching: `"Pregunta 8: ¿Tiene Dios la culpa del sufrimiento?"`,
  `"Question 12: Can the Dead Live Again?"`, and a two-digit number.
- `stripTocPrefix`, returned whole:
  - `"Introducción a la Palabra de Dios"`;
  - an empty string;
  - `"Pregunta: x"` (no digits);
  - `"Pregunta 8 x"` (no colon);
  - `"Pregunta 8:x"` (no space after the colon);
  - `" 8: x"` (no leading run);
  - `"Pregunta 8: "` (empty remainder, A2).

**`QuestionParserTest.cpp`**

- Title from `<h1><strong>…</strong></h1>`, with the `contextTtl` paragraph and its `pageNum` absent.
- A quote with its reference (Q1 excerpt). The `Verse` holds the quote with its curly quotes, the
  label `Salmo 83:18`, and the href `1001061123-split83.xhtml#chapter83_verse18`.
- Multi-link references:
  - Eclesiastés: label `Eclesiastés 9:5, 10`, href `…#chapter9_verse5`;
  - Mateo: label `Mateo 26:14, 15; 27:5`, first href `1001061144-split26.xhtml#chapter26_verse14`.
- Q6's headings: `PROFECÍA`/`CUMPLIMIENTO` `<h2>`s alternating with verses, in order, including the
  `<h2>` that carries a `pageNum` span.
- Q19 prose: an `<h2>`, an `<h3>` (A3), and `<p><strong>…</strong></p>` + `<p>…</p>` → Heading,
  Heading, Text, Text.
- A reference with no quote → `Verse` with empty `text`.
- A quote with no reference: followed by a heading, and at the end of the document → `Text`.
- Two quotes in a row → the first becomes `Text`, the second pairs.
- An empty `<p>` between a quote and its reference does not break the pair (A10). A trailing empty
  `<p>` adds no block.
- `<figure>` with an `<img alt>` adds no text.
- `[...]` passes through.
- Entities, in a fixture opening with
  `<!DOCTYPE html PUBLIC "-//W3C//DTD XHTML 1.1//EN" "xhtml11.dtd">` as
  `VerseAnchorsTest.cpp:40-42` does: `&nbsp;` → U+00A0, `&amp;` → `&`, and an unknown `&foo;` is
  kept literally.
- The same `&nbsp;` with no DOCTYPE → `feed` false, `take` false (A9).
- A prose paragraph with an inline `<a>` → `Text` including the link text (A7).
- Chunk boundaries: one fixture fed at every split point and at chunk sizes 1, 2, 3, 7 and 64. The
  fixture has the DOCTYPE, and a tag name, an attribute value, an entity and a multi-byte UTF-8
  character crossing chunk edges. The test first asserts that one-shot `parseQuestion` succeeds and
  yields the expected blocks, so the comparison cannot pass by both sides failing. Each chunked
  result equals it. An empty final chunk (`feed("", 0, true)`) is accepted.
- Bounds:
  - exactly 64 `Text` blocks → success;
  - 65 → `feed` false, `take` false, `out` untouched;
  - text totalling exactly 8,192 bytes → success;
  - 8,193 → refused;
  - one 9,000-byte paragraph → refused before the paragraph closes (`feed` false on the chunk that
    carries the overflow);
  - a document whose only overflow is a trailing unpaired quote, flushed at the end → the final
    `feed` returns false, and `take` returns false.
- Malformed (`<p>` never closed, then the final chunk) → false, `take` false, and later feeds false.
- `take` before the final chunk → false; a second `take` after success → false.

`static_assert` and generated bulk fixtures (65 × `<p>x</p>`) keep the bound tests tiny and free of
publisher text.

**Device build.** `pio run -e x4pro` must stay green with no new warnings. Until T3 includes a
`<Preaching/…>` header from `src/`, PlatformIO's default library finder is unlikely to compile
`lib/Preaching` (research §4.5; `lib/StudyStore` landed the same way in `121e1e2f`). The implement
phase records from the build log whether `Preaching` was built. The host suite's
`-Wall -Wextra -pedantic` does not see the device `LOG_ERR` format checks, because the host stub is
empty. So the implement phase also compiles the library once on the device: a temporary,
**uncommitted** `#include <Preaching/QuestionParser.h>` and `<Preaching/IntroSection.h>` in one
`src/` file, `pio run -e x4pro`, the log checked for warnings from `lib/Preaching`, then reverted
with the tree confirmed clean. The final, committed build is run without it.

**Format.** `./bin/clang-format-fix` (whole tree) after `git add`.

## Hand-offs to later tasks

- T3 writes the sink adapter (`bool feedQuestion(void* ctx, …) { return …->feed(…); }`, like
  `UnitIndexCache.cpp:39-41`). It owns one `QuestionParser` per open, and calls `take` after
  `stream` returns true.
- T3/T7: `Block::href` is verbatim and unbounded except by `MAX_TEXT_BYTES`. The 64-byte
  `ReaderEntryIntent::href` limit (parent §6) is enforced where the intent is built; the longest real
  href is 45 B (research §3).

## Changes after spec review pass 0

`docs/superpowers/reviews/berean-o-14-spec-review-0.md` (CLEAR, 3 MAJOR, 1 MINOR); all accepted.

- **MAJOR 1, entities.** A9 claimed that undeclared entities always reach the default handler.
  They reach it only after an external-subset DOCTYPE (`xmlparse.c:3037-3039`). A9, the character
  data section and the tests are rewritten: entity fixtures carry the DOCTYPE, a no-DOCTYPE case
  asserts a clean refusal, and the chunk test asserts that the one-shot parse succeeds before
  comparing.
- **MAJOR 2, flush after the bounds check.** `feed` used to check the bounds before flushing a
  trailing quote, so an overflow there returned true. The flush now runs first, and a test covers it.
- **MAJOR 3, device warnings unchecked.** The ternary `LOG_ERR` format is now two literal-format
  calls (the device macro would not compile it). The implement phase compiles the library on the
  device through a temporary, uncommitted `src/` include.
- **MINOR 1, memory.** The block vector outlives the parser inside `Question`, and it is
  PSRAM-routed. The memory section now says so.
