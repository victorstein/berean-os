Tier: standard

# PR review 0: berean-o-14, `lib/Preaching` intro section and question parser

Range: `origin/batch/20261015-predicaci-n-193x...origin/feature/preaching-intro-parser` at
`f407486b`. Code: `lib/Preaching/Preaching/{IntroSection,QuestionParser}.{h,cpp}`,
`test/preaching/`, and one line in `test/CMakeLists.txt`. Everything else in the diff is pipeline
documentation (research, spec, plan, and two review files). There are no worker reply commits after
the code commits.

What I ran:

- `cmake -S test -B build/test && cmake --build build/test --target PreachingTest && build/test/preaching/PreachingTest`
  gives 33/33 passing, with no compiler warnings under `crosspoint_test_common`'s
  `-Wall -Wextra -pedantic`.
- A scratch harness, kept out of the tree, fed all 20 real question files
  (`1001061251`–`1001061270.xhtml`, extracted from `~/Downloads/nwt_S.epub`) through
  `QuestionParser::feed` in 2,048-byte chunks plus an empty final chunk. This is how
  `SpineHtmlStream` will feed it. Results:
  - All 20 succeed, with the correct `<h1>` titles.
  - The largest is Q19 at 32 blocks; the most text is Q2 at 1,902 bytes.
  - There are 104 `Verse` blocks, which equals the 104 `<q class="scrp">` in the section. None has
    an empty quote, and no `Text` block is an orphaned quote.
  - Q6 alternates PROFECÍA/CUMPLIMIENTO headings, Q17 has three headings, and Q19's
    `<h2>`/`<h3>` headings and prose come out in document order.
  - Ten multi-link references join correctly, including "Mateo 26:14, 15; 27:5".
  - **Nothing visible is lost.** For every file, the non-whitespace character count of the
    parser's title, text and reference matches an independent count. That count used Python's
    `html.parser` over the `<body>`, with the same three skips (`contextTtl`, `<figure>`,
    `pageNum`).
- `build/pio-x4pro-check.log` is from the implement phase's temporary-include build. It shows
  `Compiling .../Preaching/Preaching/IntroSection.cpp.o` and `QuestionParser.cpp.o` (lines
  4209–4210), no warnings naming `lib/Preaching`, and `SUCCESS`. The final `build/pio-x4pro.log`
  is `SUCCESS`, and `git status` is clean, so the temporary `src/PreachingBuildCheck.cpp` did not
  survive.
- I did not run `./bin/clang-format-fix`, because it rewrites in place and this review may only
  write its own file. CI's format job gates it.

## Intent

**Acceptance criteria, against the brief (`hpipe brief --task t1`):**

- **`IntroSection`.**
  - The 21 basenames are `constexpr const char*` at `IntroSection.h:17-23`, with slot 0 the index
    and slot n question n.
  - `nextQuestion` is `constexpr` over a flash table (`IntroSection.h:26-33`). I checked it row
    by row against parent spec §4 on `main` (7→10, 12→18, 16→18, 17→18, 18→20, 20→0, otherwise
    n+1). Out-of-range input gives 0.
  - `stripTocPrefix` (`IntroSection.cpp:5-16`) implements the grammar with `string_view` in and
    out. It returns the label whole when the remainder would be empty (spec A2), so the row is
    never blank.
- **`QuestionParser`.**
  - It is chunk-fed through expat behind `void*` and has no Arduino or HalStorage dependency. Its
    `feed(chunk, length, isFinal)` matches the `ChunkSink` signature at `SpineHtmlStream.h:19`.
  - It emits a title, then `Heading`/`Verse`/`Text` blocks.
  - It skips `contextTtl`, `<figure>` and `pageNum` wherever they occur
    (`QuestionParser.cpp:46-51`, applied before the collecting check at `:201-208`). Entities and
    `[...]` pass through.
  - It applies both pairing rules: a lone reference becomes a `Verse` with an empty quote
    (`:147-155`), and a lone quote becomes `Text` where it stood (`:136-143`, flushed before
    headings, titles, text and end of document).
- **Bounds.** The block cap is enforced in `commit` (`:127-130`) and the byte cap in `admit`
  (`:84-88`), which runs before every byte is stored, so a single oversized paragraph is stopped
  mid-growth. `feed` flushes a trailing quote before checking `overLimit` (`:302-303`), so an
  end-of-document overflow is refused too. Every failure clears the state (`:305-306`, `:313-314`).
  `take` writes only after an accepted final chunk, and only once (`:321-327`): nothing partial
  escapes.
- **Tests.**
  - `test/preaching/` is wired into `test/CMakeLists.txt` and runs.
  - Every brief case is present: a quote with its reference; a multi-link reference, in both the
    two-link Eclesiastés form and the cross-chapter Mateo form; Q6's headings, including the one
    carrying a `pageNum`; Q19 prose with `<h2>`, `<h3>` and `<strong>`; a lone reference; a lone
    quote, both before a heading and at the end; and chunk boundaries at every split point plus
    sizes 1, 2, 3, 7 and 64. The chunk-boundary fixture covers tag, attribute, entity and
    multi-byte UTF-8 crossings, and asserts the one-shot result before comparing. The 64/65-block
    and 8,192/8,193-byte bounds, the title and href share of the byte budget, the in-paragraph
    overflow, and the trailing-quote overflow are also covered.
  - The fixtures are short excerpts. The bulk fixtures are generated `x` text, not publisher text.
- **Device build.** The library was compiled by the device toolchain with no warnings (see above).

**Spec departures from the brief are deliberate, documented, and correct.**

- **Headings.** The spec takes `<h3>`–`<h6>` as `Heading`, not just `<h2>` (A3). Without that,
  Q19's eight `<h3>` labels would be silently dropped, because only heading and `<p>` text is
  collected (A6). The real-file run confirms they appear.
- **Byte budget.** The title and hrefs count towards the 8 KB budget (A12). That is stricter than
  "block text" and has no practical effect at about 1.9 KB maximum.

Neither touches an owner decision (D1–D9).

**Scope.** There is no `src/` code, no UI, no EPUB reading and no tag topics. The only shared-file
edit is the one `add_subdirectory(preaching)` line the brief assigns.

**Plan.** `QuestionParser.cpp` is identical to the plan's Task 3 listing: I diffed the extracted
plan block against the file and found no difference. Task 4, the temporary device include, and
Task 5 were carried out as written.

## Quality

The change follows existing patterns:

- **Shape.** It copies `BibleNav::Scanner`: an opaque `void*` parser, chunk feeding and a final
  `take`. Its allocation and logging copy `VerseTextScanner`: `makeUniqueNoThrow` for the state
  with a `LOG_ERR` on null, `outOfMemory()`, and `XML_ErrorString` in the log.
- **Entities.** The default-handler entity path mirrors `VerseAnchors`.
- **Layout.** The library layout and `namespace preaching` follow `lib/StudyStore`.
- **Test suite.** The suite's CMake follows `test/bible_nav_scanner/` and
  `test/bible_search_scanner/`.

CLAUDE.md rules:

- `constexpr` tables are used.
- `blocks.reserve(MAX_BLOCKS)` comes before the `push_back`s.
- There is no `std::function` and no bare `new`.
- The two `LOG_ERR` calls use literal formats, and `%zu` is correct for `size_t` on the S3.
- Locals are small.

The state machine is compact:

- Byte accounting is kept consistent by construction. `bytesInUse()` counts committed bytes, the
  pending quote and the live buffer, and `admit` is never called between a commit and the buffer
  clear that follows it.
- `skipDepth` is checked before `collectDepth`, so a skipped span inside a paragraph cannot
  unbalance it.
- `linkDepth` cannot underflow.

The comments explain a reason rather than restate code. For example, the empty-block comment at
`QuestionParser.cpp:169-170` and the DOCTYPE comment at `:255-257` both say why.

The tests check behaviour rather than mirror the code. The figure and `pageNum` skips each have a
test that fails if the skip is removed (`SkipsFigureCaptionsAndPageNumbersThatCarryText`, added
after plan review). The bounds tests probe both sides of each limit.

Two weaker spots, both minor:

- **The follow-up test copies its expectation.** It is a verbatim copy of the implementation's
  table rather than the brief's rule.
- **Three helpers are a fourth verbatim copy.** `hasToken`, `attribute` and `isAsciiSpace` repeat
  file-local helpers that already exist in `VerseTextScanner.cpp` and `UnitText.cpp`. The spec
  acknowledges this, and it is the repo's current convention, so it is not a defect in this change.

## Findings

1. **MINOR: the follow-up table test restates the implementation instead of the rule.**
   `test/preaching/IntroSectionTest.cpp:25` (`EXPECTED_FOLLOW_UP`) is the same 21 numbers as
   `lib/Preaching/Preaching/IntroSection.h:26-27` (`NEXT_QUESTION`). A transcription error made in
   both, which is likely since the tables were written together, would pass both the
   `static_assert` and the runtime test. I checked the values by hand against parent spec §4 and
   they are correct today. A stronger test would state the brief's form: `n + 1` except an
   explicit override list `{7→10, 12→18, 16→18, 17→18, 18→20, 20→0}`. Optional; fix inline if
   convenient.

2. **MINOR: a fourth verbatim copy of the expat attribute and class-token helpers.**
   `lib/Preaching/Preaching/QuestionParser.cpp:15-35` (`isAsciiSpace`, `hasToken`, `attribute`)
   is byte-for-byte the same as `lib/BibleSearch/BibleSearch/VerseTextScanner.cpp:44-63`.
   `lib/StudyStore/StudyStore/UnitText.cpp:37,45` carries a near-copy. The spec names the
   duplication, and extracting a shared header would reach into other surfaces, so do not change
   it here. If the duplication should end, file it as an `hpipe discover` follow-up.

VERDICT: CLEAR
