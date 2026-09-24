# Bible verse search: implementation notes

Findings from the real markup (`nwt_S.epub`, Spanish NWT) and deviations from
`docs/superpowers/plans/2026-09-24-bible-verse-search.md`, recorded as each task landed.

## Task 1: folding and tokenising

- **Other scripts are left untouched**, including their case. The plan allowed lower-casing
  "where ASCII-simple"; a partial Greek or Cyrillic case table would fold some letters and not
  others, so the choice is to fold none (`FoldTest.cpp`, `LeavesOtherScriptsUntouched`).
- **Combining marks U+0300..U+036F are dropped**, beyond the plan, so decomposed (NFD) text folds
  like precomposed text. The NWT is precomposed; this only guards other sources.
- **Malformed UTF-8 becomes a space.** The plan requires valid UTF-8 output without saying how;
  a space also separates tokens, so a bad byte never glues two words together.
- **`MIN_TOKEN_BYTES` counts bytes, as the plan says**, not letters as the spec's "one-letter
  tokens are dropped" reads. The two agree for every folded Latin letter; a lone unfolded
  non-Latin letter (2 bytes) is kept.
- **`lib/BibleSearch/library.json` carries only `name` and `version`.** The existing manifests
  (`lib/expat`, `lib/miniz`) exist to set a source filter or `srcDir`; this library needs neither,
  and `lib/StudyStore` has the same layout with no manifest at all.
- **`test/CMakeLists.txt` gains one `add_subdirectory` line per suite.** The epub-dev agent
  definition reserves that file for the orchestrator; the plan's ground rules ask the task to add
  the line, and this worktree has no parallel agent to collide with.

## Task 2: verse-text scanner

### What the real markup does

Measured over every spine document of `nwt_S.epub` that carries a verse marker:

- **1,189 documents, one chapter each, 31,078 verses.** No chapter spans two documents, so no
  document starts mid-verse. The plan's example, `1001061147-split2.xhtml`, is the whole of
  John 2. The one document that does not open at verse 1 is John 8 (`1001061147-split8.xhtml`),
  which opens at 8:12 because the NWT omits 7:53-8:11.
- **Text before the first marker is never verse text.** Every document opens with a navigation
  line, `<p class="w_navigation w_biblebookname">` ("Juan 2 : 1 - 25"). The first chapter of
  each book adds its title in `<header><h1>`. Psalms add a superscription,
  `<p class="pN sw">` ("Salmo de David."), and Psalm 119 an acrostic heading.
- **The footnote section opens with `<div class="groupFootnote">`**, holding one
  `<aside epub:type="footnote">` per note. Malachi 4 opens with an *empty*
  `groupFootnote` div before the one that holds its notes.
- **Verse numbers:** `<strong><sup>N</sup></strong>`, followed by U+202F. Verse 1 has no `<sup>`;
  its number is the chapter number, in `<span class="w_ch"><strong>2</strong> </span>`.
- **Footnote markers:** always `<a epub:type="noteref">*</a>`. No other `<a>` appears in verse
  text, and there are no entity references.
- **Unicode spaces:** U+202F after each verse number and U+00A0 between many words.
- **Headings inside a chapter:** `<p class="pN ss">` holds the Hebrew acrostic letters (Psalms 9,
  10, 25, 34, 37, 111, 112, 119 and 145, Proverbs 31, Lamentations 1-4), sometimes in the middle
  of a verse (Psalm 111:1). It also holds the Psalms book divisions ("(Salmos 42-72)") and the
  musical note closing Habakkuk 3. Malachi 4 ends with an
  editorial note, `<p class="pN sd">` ("Aquí termina la traducción de las Escrituras
  Hebreoarameas…"), after 4:6.
- **16 verses are empty**, for example Mark 9:44 and 9:46. The NWT omits them and keeps only the
  number and a footnote marker.
- **The longest verse is 485 bytes** (Esther 8:9).

### Deviations

- **Continuation text is kept as an API, but no real document produces any.** Following the
  plan literally would have attached each navigation line, book title and superscription to the
  previous chapter's last verse. The scanner skips these subtrees, so `continuation()` is empty
  for every real document. It is tested with a synthetic document instead, and the real fixtures
  assert that it stays empty.
- **More subtrees are skipped than the plan lists:** `w_navigation`, `w_ch`, `sw`, `ss` and `sd`
  classes, `<header>` and `<h1>`-`<h6>`, besides `<sup>` and `noteref` links. As a result,
  superscriptions ("Canción de las subidas") are not searchable. The spec scopes search to verse
  text, and these sit outside every verse.
- **The footnote section is detected by `class="groupFootnote"` or `epub:type="footnote"`**,
  whichever comes first.
- **Unicode spaces (U+00A0, U+2000-U+200A, U+202F, U+205F, U+3000) collapse into ASCII spaces**,
  as ASCII whitespace does. Block element boundaries also count as whitespace, so the poetry
  lines in Psalm 23:2 (`…reposar;</p><p …>me lleva…`) do not run together.
- **Empty verses are kept** with empty text, so the pairing with `VerseAnchors` holds and the
  verse table has no gaps.
- **Fixtures are whole documents copied verbatim**, not trimmed: John 2 (8.7 KB), John 8,
  Genesis 1, Psalms 23, 111 and 119, and Malachi 4, 117 KB in total. Trimming by hand risks
  breaking the XML the tests depend on.
- **Cross-check:** an independent Python extraction (`html.parser`) and the C++ scanner gave
  identical text for all 31,078 verses.
