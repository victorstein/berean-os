# Plan: `lib/Preaching` — the intro section and its question parser (berean-o-14)

Implements `docs/superpowers/specs/2026-10-15-berean-o-14-design.md` (cleared by
`docs/superpowers/reviews/berean-o-14-spec-review-0.md`). Read that spec first: its assumptions
A1–A16 are the behaviour this code implements.

FILES: lib/Preaching/, test/preaching/, test/CMakeLists.txt
FILES: src/PreachingBuildCheck.cpp

`src/PreachingBuildCheck.cpp` is created in Task 4 only to make PlatformIO compile the library
once, and is deleted in the same task. It must never be committed.

**Modelled on.** `lib/Epub/Epub/BibleNavScanner.{h,cpp}` and its suite
`test/bible_nav_scanner/` (shape); `lib/BibleSearch/BibleSearch/VerseTextScanner.cpp:44-62,191-203,307-336`
(`hasToken`, `attribute`, the entity default handler, `makeUniqueNoThrow` + `LOG_ERR`);
`test/bible_search_scanner/CMakeLists.txt` (a suite that links `htmlEntities.cpp`, `lib/Memory`
and the `test/stubs` logging stub).

**The code in this plan was compiled and run before it was written down.** It was built against
this repo's own expat, `htmlEntities.cpp` and `Memory.h` with the repo's `-Wall -Wextra -pedantic`
host flags: all 33 tests pass. It was formatted with the repo's clang-format 21. All 21 files of
the real section parse (`nwt_S.epub`): Q1–Q20 yield 114 `Verse` blocks, the largest is Q19 at 32
blocks, the most text is 1,902 bytes (Q2). Paste it exactly. If something does not compile or a
test fails, the plan is wrong: stop and report rather than improvise.

## Rules for whoever executes this

- Work only in `/Volumes/stein/.herdr/worktrees/berean-os/feature-preaching-intro-parser`, on
  branch `feature/preaching-intro-parser`. Never push, never open a PR, never run `hpipe`, `bd` or
  `bv`.
- Commit with `git add <paths>` then `git commit`, naming paths. Never `git add -A`, never
  `git commit -a`.
- **Code is self-documenting.** Use meaningful names. A comment exists only for a non-obvious
  *why* (a constraint, a gotcha, a workaround). Do not add comments that restate the code or
  narrate history. The code below already follows this; do not add to it.
- `CLAUDE.md` applies: `makeUniqueNoThrow` for fallible allocations with a `LOG_ERR`, no
  `std::function`, `constexpr` tables, `.reserve()` before push-back loops, no `Serial`.
- The repo is public. Test fixtures are the short excerpts below, never whole publisher files.
- `pio` is not on `PATH`: use `~/.platformio/penv/bin/pio`.
- Commit messages end with the line `Claude-Session: https://claude.ai/code/session_01G51hvdrPkE2Hm6TU4eW2CY` and carry no co-author trailer.

## Task 0 — the host test build directory (2 min)

**Step 0.1.** Check that the worktree is bootstrapped, then configure the host tests. The configure
fetches GoogleTest and ArduinoJson on the first run.

```bash
cd /Volumes/stein/.herdr/worktrees/berean-os/feature-preaching-intro-parser
test -x .venv/bin/clang-format || ./bin/bootstrap
cmake -S test -B build/test
```

Expected: `-- Build files have been written to: …/build/test`. `build/` is gitignored
(`.gitignore:13`). Nothing to commit.

## Task 1 — `IntroSection`: the basenames and the follow-up table (5 min)

**Step 1.1 — the failing test.** Create `test/preaching/IntroSectionTest.cpp`:

```cpp
#include <gtest/gtest.h>

#include <string>

#include "Preaching/IntroSection.h"

namespace {

using preaching::IntroSection::BASENAMES;
using preaching::IntroSection::nextQuestion;

TEST(IntroSection, IndexPageIsSlotZero) {
  EXPECT_STREQ(BASENAMES[preaching::IntroSection::INDEX_SLOT], "1001061250.xhtml");
}

TEST(IntroSection, QuestionNIsSlotN) {
  ASSERT_EQ(preaching::IntroSection::SLOT_COUNT, 21);
  for (int slot = 0; slot < preaching::IntroSection::SLOT_COUNT; slot++) {
    EXPECT_EQ(std::string(BASENAMES[slot]), "10010612" + std::to_string(50 + slot) + ".xhtml") << "slot " << slot;
  }
}

// The design's follow-up table, by question number; index 0 is unused.
constexpr int EXPECTED_FOLLOW_UP[] = {0, 2, 3, 4, 5, 6, 7, 10, 9, 10, 11, 12, 18, 14, 15, 16, 18, 18, 20, 20, 0};

constexpr bool followUpsMatchTheDesign() {
  for (int question = 1; question <= preaching::IntroSection::QUESTION_COUNT; question++) {
    if (nextQuestion(question) != EXPECTED_FOLLOW_UP[question]) return false;
  }
  return true;
}
static_assert(followUpsMatchTheDesign());

TEST(IntroSection, FollowUpTableMatchesTheDesign) {
  for (int question = 1; question <= preaching::IntroSection::QUESTION_COUNT; question++) {
    EXPECT_EQ(nextQuestion(question), EXPECTED_FOLLOW_UP[question]) << "question " << question;
  }
}

TEST(IntroSection, FollowUpOutOfRangeIsNone) {
  EXPECT_EQ(nextQuestion(0), 0);
  EXPECT_EQ(nextQuestion(-1), 0);
  EXPECT_EQ(nextQuestion(21), 0);
}

}  // namespace
```

**Step 1.2.** Create `test/preaching/CMakeLists.txt`:

```cmake
add_executable(PreachingTest
  IntroSectionTest.cpp
)

target_include_directories(PreachingTest PRIVATE
  ${REPO_ROOT}/lib/Preaching
)

target_link_libraries(PreachingTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(PreachingTest)
```

**Step 1.3.** Register the suite. In `test/CMakeLists.txt`, add one line directly after
`add_subdirectory(parked_book)` (currently the last line, `test/CMakeLists.txt:147`):

```cmake
add_subdirectory(preaching)
```

This T1 brief assigns that line to this task.

**Step 1.4 — run it, and see it fail.**

```bash
cmake -S test -B build/test && cmake --build build/test --target PreachingTest -j8
```

Expected: a compile error, `'Preaching/IntroSection.h' file not found`.

**Step 1.5 — the minimum to pass.** Create `lib/Preaching/Preaching/IntroSection.h`:

```cpp
#pragma once

#include <cstdint>
#include <string_view>

// The NWT's front-matter section of twenty preaching questions,
// "Introducción a la Palabra de Dios".
namespace preaching::IntroSection {

inline constexpr int QUESTION_COUNT = 20;
inline constexpr int SLOT_COUNT = QUESTION_COUNT + 1;
inline constexpr int INDEX_SLOT = 0;

// Slot 0 is the section's index page; slot n is question n. Found by filename
// rather than by title: the titles are translated, the filenames are the same
// in every edition checked.
inline constexpr const char* BASENAMES[SLOT_COUNT] = {
    "1001061250.xhtml", "1001061251.xhtml", "1001061252.xhtml", "1001061253.xhtml", "1001061254.xhtml",
    "1001061255.xhtml", "1001061256.xhtml", "1001061257.xhtml", "1001061258.xhtml", "1001061259.xhtml",
    "1001061260.xhtml", "1001061261.xhtml", "1001061262.xhtml", "1001061263.xhtml", "1001061264.xhtml",
    "1001061265.xhtml", "1001061266.xhtml", "1001061267.xhtml", "1001061268.xhtml", "1001061269.xhtml",
    "1001061270.xhtml",
};

// The question to offer on a return visit, by question number; index 0 is unused.
inline constexpr uint8_t NEXT_QUESTION[SLOT_COUNT] = {0,  2,  3,  4,  5,  6,  7,  10, 9,  10, 11,
                                                      12, 18, 14, 15, 16, 18, 18, 20, 20, 0};

// 0 when there is no follow-up or `question` is outside 1..QUESTION_COUNT.
constexpr int nextQuestion(const int question) {
  if (question < 1 || question > QUESTION_COUNT) return 0;
  return NEXT_QUESTION[question];
}

}  // namespace preaching::IntroSection
```

**Step 1.6 — run it again.**

```bash
cmake --build build/test --target PreachingTest -j8 && build/test/preaching/PreachingTest
```

Expected: `[  PASSED  ] 4 tests.`, no warnings from `test/preaching/` or `lib/Preaching/`.

**Step 1.7 — commit.**

```bash
git add lib/Preaching/Preaching/IntroSection.h test/preaching/IntroSectionTest.cpp test/preaching/CMakeLists.txt test/CMakeLists.txt
git commit -m "feat(preaching): add the intro section's files and follow-ups" -m "Claude-Session: https://claude.ai/code/session_01G51hvdrPkE2Hm6TU4eW2CY"
```

## Task 2 — `IntroSection::stripTocPrefix` (4 min)

**Step 2.1 — the failing test.** In `test/preaching/IntroSectionTest.cpp`:

- after the line `using preaching::IntroSection::nextQuestion;`, add:

```cpp
using preaching::IntroSection::stripTocPrefix;
```

- directly before the closing `}  // namespace`, add:

```cpp
TEST(IntroSection, StripsTheTocPrefix) {
  EXPECT_EQ(stripTocPrefix("Pregunta 8: ¿Tiene Dios la culpa del sufrimiento?"),
            "¿Tiene Dios la culpa del sufrimiento?");
  EXPECT_EQ(stripTocPrefix("Question 12: Can the Dead Live Again?"), "Can the Dead Live Again?");
  EXPECT_EQ(stripTocPrefix("Pregunta 1: ¿Quién es Dios?"), "¿Quién es Dios?");
}

TEST(IntroSection, ReturnsAnUnprefixedLabelWhole) {
  for (const char* label : {"Introducción a la Palabra de Dios", "", "Pregunta: x", "Pregunta 8 x", "Pregunta 8:x",
                            " 8: x", "Pregunta 8: "}) {
    EXPECT_EQ(stripTocPrefix(label), label) << '"' << label << '"';
  }
}
```

**Step 2.2 — run it, and see it fail.**

```bash
cmake --build build/test --target PreachingTest -j8
```

Expected: a compile error, `no member named 'stripTocPrefix' in namespace 'preaching::IntroSection'`.

**Step 2.3 — the minimum to pass.** In `lib/Preaching/Preaching/IntroSection.h`, directly before
the closing `}  // namespace preaching::IntroSection`, add:

```cpp
// "Pregunta 8: ¿Tiene Dios la culpa del sufrimiento?" -> the question alone.
// A label that does not have that shape, or would strip to nothing, comes back
// whole so a row is never blank. The result views into `label`.
std::string_view stripTocPrefix(std::string_view label);
```

Create `lib/Preaching/Preaching/IntroSection.cpp`:

```cpp
#include "Preaching/IntroSection.h"

namespace preaching::IntroSection {

std::string_view stripTocPrefix(const std::string_view label) {
  const size_t space = label.find(' ');
  if (space == 0 || space == std::string_view::npos) return label;
  const size_t digitsStart = space + 1;
  size_t cursor = digitsStart;
  while (cursor < label.size() && label[cursor] >= '0' && label[cursor] <= '9') cursor++;
  if (cursor == digitsStart) return label;
  const std::string_view rest = label.substr(cursor);
  constexpr std::string_view SEPARATOR = ": ";
  if (rest.substr(0, SEPARATOR.size()) != SEPARATOR || rest.size() == SEPARATOR.size()) return label;
  return rest.substr(SEPARATOR.size());
}

}  // namespace preaching::IntroSection
```

Replace the whole of `test/preaching/CMakeLists.txt` with:

```cmake
add_executable(PreachingTest
  IntroSectionTest.cpp
  ${REPO_ROOT}/lib/Preaching/Preaching/IntroSection.cpp
)

target_include_directories(PreachingTest PRIVATE
  ${REPO_ROOT}/lib/Preaching
)

target_link_libraries(PreachingTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(PreachingTest)
```

**Step 2.4 — run it again.**

```bash
cmake --build build/test --target PreachingTest -j8 && build/test/preaching/PreachingTest
```

Expected: `[  PASSED  ] 6 tests.` After this step the two IntroSection files equal the final
versions in Appendix A.

**Step 2.5 — commit.**

```bash
git add lib/Preaching/Preaching/IntroSection.h lib/Preaching/Preaching/IntroSection.cpp test/preaching/IntroSectionTest.cpp test/preaching/CMakeLists.txt
git commit -m "feat(preaching): strip the TOC prefix from a question label" -m "Claude-Session: https://claude.ai/code/session_01G51hvdrPkE2Hm6TU4eW2CY"
```

## Task 3 — `QuestionParser` (one red/green cycle, 10 min in five steps)

The parser is a single state machine: pairing, skipping, entities and bounds all run through the
same handlers. So its tests are written together first, all red, and the implementation then
turns them green together.

**Step 3.1 — the header.** Create `lib/Preaching/Preaching/QuestionParser.h`:

```cpp
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

// Reads one question file of the NWT intro section into blocks the Predicación
// cards lay out. Shaped like BibleNav::Scanner: chunk-fed, expat behind an
// opaque pointer, and free of Arduino and HalStorage so it stays host-testable.
namespace preaching {

// The largest real question is 33 blocks and under 2 KB of text; these bound
// memory against a malformed or unexpected file.
inline constexpr size_t MAX_BLOCKS = 64;
inline constexpr size_t MAX_TEXT_BYTES = 8 * 1024;

enum class BlockKind : uint8_t { Heading, Verse, Text };

struct Block {
  BlockKind kind = BlockKind::Text;
  // Heading and Text: the text. Verse: the quote, empty when the reference had none.
  std::string text;
  // Verse only: the reference as it reads, e.g. "Mateo 26:14, 15; 27:5".
  std::string reference;
  // Verse only: the first link's href, verbatim.
  std::string href;
};

struct Question {
  std::string title;
  std::vector<Block> blocks;
};

struct QuestionParserState;

class QuestionParser {
 public:
  QuestionParser();
  ~QuestionParser();
  QuestionParser(const QuestionParser&) = delete;
  QuestionParser& operator=(const QuestionParser&) = delete;

  bool valid() const { return parser_ != nullptr; }
  // The SpineHtmlStream::ChunkSink contract: false stops the pass, and every
  // later call returns false too. `isFinal` marks the last chunk, which may be
  // empty.
  bool feed(const char* chunk, size_t length, bool isFinal);
  // True when a failed feed ran out of memory rather than met bad markup or a bound.
  bool outOfMemory() const { return outOfMemory_; }
  // Moves the result into `out` once a final chunk was accepted, and only once.
  // Otherwise returns false and leaves `out` untouched: nothing partial.
  bool take(Question& out);

 private:
  void* parser_ = nullptr;  // XML_Parser; opaque here to keep expat out of the header
  std::unique_ptr<QuestionParserState> state_;
  bool failed_ = false;
  bool finished_ = false;
  bool taken_ = false;
  bool outOfMemory_ = false;
};

bool parseQuestion(const char* xhtml, size_t length, Question& out);

}  // namespace preaching
```

**Step 3.2 — the failing tests.** Create `test/preaching/QuestionParserTest.cpp`.

What it covers, in the spec's terms:

- the title and the `contextTtl`/`pageNum` skip;
- `<figure>`, and the `<figure>` and `pageNum` skips against content that would show if either
  skip were missing: a caption paragraph inside `<figure>`, and `pageNum` spans that carry text
  (the real ones are empty, so only this test fails when a skip is removed);
- a quote with its reference;
- a `pageNum` inside a quote paragraph;
- multi-link references, Ecclesiastes and Matthew;
- Q6's headings, including the one with a `pageNum`;
- Q19's `<h2>`/`<h3>` prose (A3);
- whitespace collapsing (A8);
- a reference with no quote, a quote with no reference, and two quotes in a row;
- empty paragraphs (A10);
- prose with an inline link (A7);
- entities with a DOCTYPE, and refusal without one (A9);
- malformed markup;
- `take` only after the final chunk and only once (A14, A15);
- chunk boundaries, every split point plus sizes 1/2/3/7/64, with the one-shot result asserted
  first;
- the bounds: exactly 64 blocks and exactly 8,192 bytes accepted; 65 blocks and 8,193 bytes
  refused; title and href counted; a trailing unpaired quote that crosses the block limit refused
  (review MAJOR 2); an oversized paragraph refused before it closes (A12).

```cpp
#include <gtest/gtest.h>

#include <algorithm>
#include <string>

#include "Preaching/QuestionParser.h"

namespace {

using preaching::BlockKind;
using preaching::Question;
using preaching::QuestionParser;

// Short excerpts of the NWT intro section's markup, never whole files: the
// repository is public.
std::string page(const std::string& body) {
  return "<?xml version=\"1.0\" encoding=\"utf-8\" ?>\n"
         "<html xmlns=\"http://www.w3.org/1999/xhtml\" xml:lang=\"es\">\n"
         "<head><title>Pregunta</title></head>\n"
         "<body>" +
         body + "</body>\n</html>\n";
}

// Under XML_GE=0 expat hands an undeclared entity to the default handler only
// once the document declares an external subset.
std::string pageWithDoctype(const std::string& body) {
  return "<?xml version=\"1.0\" encoding=\"utf-8\" ?>\n"
         "<!DOCTYPE html PUBLIC \"-//W3C//DTD XHTML 1.1//EN\" \"xhtml11.dtd\">\n"
         "<html xmlns=\"http://www.w3.org/1999/xhtml\"><body>" +
         body + "</body></html>\n";
}

Question parsed(const std::string& xhtml) {
  Question question;
  EXPECT_TRUE(preaching::parseQuestion(xhtml.data(), xhtml.size(), question));
  return question;
}

bool refused(const std::string& xhtml) {
  Question question;
  question.title = "untouched";
  const bool ok = preaching::parseQuestion(xhtml.data(), xhtml.size(), question);
  EXPECT_EQ(question.title, "untouched");
  return !ok;
}

Question parsedInChunks(const std::string& xhtml, const size_t chunkBytes) {
  QuestionParser parser;
  EXPECT_TRUE(parser.valid());
  for (size_t offset = 0; offset < xhtml.size(); offset += chunkBytes) {
    const size_t length = std::min(chunkBytes, xhtml.size() - offset);
    EXPECT_TRUE(parser.feed(xhtml.data() + offset, length, offset + length == xhtml.size())) << "at " << offset;
  }
  Question question;
  EXPECT_TRUE(parser.take(question));
  return question;
}

void expectSame(const Question& actual, const Question& expected, const std::string& where) {
  EXPECT_EQ(actual.title, expected.title) << where;
  ASSERT_EQ(actual.blocks.size(), expected.blocks.size()) << where;
  for (size_t i = 0; i < expected.blocks.size(); i++) {
    EXPECT_EQ(actual.blocks[i].kind, expected.blocks[i].kind) << where << " block " << i;
    EXPECT_EQ(actual.blocks[i].text, expected.blocks[i].text) << where << " block " << i;
    EXPECT_EQ(actual.blocks[i].reference, expected.blocks[i].reference) << where << " block " << i;
    EXPECT_EQ(actual.blocks[i].href, expected.blocks[i].href) << where << " block " << i;
  }
}

const char* const Q1_HEADER =
    "<header>\n"
    "<p class=\"contextTtl\" id=\"p1\" data-pid=\"1\"><span id=\"page6\" class=\"pageNum\" aria-hidden=\"true\" "
    "data-no=\"6\" data-before-text=\"6\"></span><strong>PREGUNTA 1</strong></p>\n"
    "<h1 id=\"p2\" data-pid=\"2\"><strong>¿Quién es Dios?</strong></h1>\n"
    "</header>\n";

const char* const Q1_FIGURE =
    "<div id=\"f1\" class=\"east_right\">\n<figure>\n"
    "<img src=\"images/1001061251_univ_cnt_1.jpg\" alt=\"Un hombre mirando hacia el cielo\" class=\"east_right\" />\n"
    "</figure>\n</div>\n";

const char* const Q1_PAIR =
    "<p id=\"p3\" data-pid=\"3\" class=\"p3 sw\"><q class=\"scrp\">“Que la gente sepa que tu nombre es Jehová, que "
    "solo tú eres el Altísimo sobre toda la tierra”.</q></p>\n"
    "<p id=\"p4\" data-pid=\"4\" class=\"p4 sw\"><a href=\"1001061123-split83.xhtml#chapter83_verse18\">Salmo "
    "83:18</a></p>\n";

TEST(QuestionParser, TakesTheTitleAndSkipsTheQuestionLabel) {
  const Question question = parsed(page(Q1_HEADER));
  EXPECT_EQ(question.title, "¿Quién es Dios?");
  EXPECT_TRUE(question.blocks.empty());
}

TEST(QuestionParser, FigureAddsNothing) {
  const Question question = parsed(page(std::string(Q1_HEADER) + "<div class=\"bodyTxt\">\n" + Q1_FIGURE + "</div>\n"));
  EXPECT_TRUE(question.blocks.empty());
}

TEST(QuestionParser, SkipsFigureCaptionsAndPageNumbersThatCarryText) {
  const Question question =
      parsed(page("<h2><span class=\"pageNum\">15</span><strong>PROFECÍA</strong></h2>\n"
                  "<figure><img src=\"a.jpg\" alt=\"Un hombre\" /><p>Un pie de foto</p></figure>\n"
                  "<p><span class=\"pageNum\">7</span>Texto</p>\n"));
  ASSERT_EQ(question.blocks.size(), 2u);
  EXPECT_EQ(question.blocks[0].text, "PROFECÍA");
  EXPECT_EQ(question.blocks[1].text, "Texto");
}

TEST(QuestionParser, PairsAQuoteWithItsReference) {
  const Question question =
      parsed(page(std::string(Q1_HEADER) + "<div class=\"bodyTxt\">\n" + Q1_FIGURE + Q1_PAIR + "</div>\n"));
  ASSERT_EQ(question.blocks.size(), 1u);
  const auto& verse = question.blocks[0];
  EXPECT_EQ(verse.kind, BlockKind::Verse);
  EXPECT_EQ(verse.text,
            "“Que la gente sepa que tu nombre es Jehová, que solo tú eres el Altísimo sobre toda la tierra”.");
  EXPECT_EQ(verse.reference, "Salmo 83:18");
  EXPECT_EQ(verse.href, "1001061123-split83.xhtml#chapter83_verse18");
}

TEST(QuestionParser, PageNumberInsideAQuoteParagraphAddsNothing) {
  const Question question = parsed(page(
      "<p id=\"p13\" class=\"p13 sw ruleAbove\"><span id=\"page7\" class=\"pageNum\" aria-hidden=\"true\" "
      "data-no=\"7\" data-before-text=\"7\"></span><q class=\"scrp\">“Levanten la vista al cielo y vean”.</q></p>\n"
      "<p id=\"p14\" class=\"p14 sw\"><a href=\"1001061127-split40.xhtml#chapter40_verse26\">Isaías "
      "40:26</a></p>\n"));
  ASSERT_EQ(question.blocks.size(), 1u);
  EXPECT_EQ(question.blocks[0].text, "“Levanten la vista al cielo y vean”.");
  EXPECT_EQ(question.blocks[0].reference, "Isaías 40:26");
}

TEST(QuestionParser, JoinsTwoLinksIntoOneReference) {
  const Question question = parsed(
      page("<p id=\"p5\" class=\"p5 sw ruleAbove\"><q class=\"scrp\">“Porque los vivos saben que morirán, pero los "
           "muertos no saben nada”.</q></p>\n"
           "<p id=\"p6\" class=\"p6 sw\"><a href=\"1001061125-split9.xhtml#chapter9_verse5\">Eclesiastés "
           "9:5,</a><a href=\"1001061125-split9.xhtml#chapter9_verse10\"> 10</a></p>\n"));
  ASSERT_EQ(question.blocks.size(), 1u);
  EXPECT_EQ(question.blocks[0].reference, "Eclesiastés 9:5, 10");
  EXPECT_EQ(question.blocks[0].href, "1001061125-split9.xhtml#chapter9_verse5");
}

TEST(QuestionParser, KeepsTheFirstHrefOfAReferenceAcrossChapters) {
  const Question question =
      parsed(page("<p class=\"p31 sw\"><q class=\"scrp\">“Quedaron en darle 30 monedas de plata”.</q></p>\n"
                  "<p id=\"p32\" class=\"p32 sw\"><a href=\"1001061144-split26.xhtml#chapter26_verse14\">Mateo 26:14, "
                  "15;</a><a href=\"1001061144-split27.xhtml#chapter27_verse5\"> 27:5</a></p>\n"));
  ASSERT_EQ(question.blocks.size(), 1u);
  EXPECT_EQ(question.blocks[0].reference, "Mateo 26:14, 15; 27:5");
  EXPECT_EQ(question.blocks[0].href, "1001061144-split26.xhtml#chapter26_verse14");
}

TEST(QuestionParser, KeepsQuestionSixHeadingsInOrder) {
  const Question question = parsed(page(
      "<div class=\"bodyTxt\">\n"
      "<h2 class=\"sw ruleAbove\" id=\"p3\" data-pid=\"3\"><strong>PROFECÍA</strong></h2>\n"
      "<p id=\"p4\" class=\"p4 sw\"><q class=\"scrp\">“De ti, Belén Efrata, [...] de ti saldrá para mí el que será "
      "gobernante en Israel”.</q></p>\n"
      "<p id=\"p5\" class=\"p5 sw\"><a href=\"1001061137-split5.xhtml#chapter5_verse2\">Miqueas 5:2</a></p>\n"
      "<h2 id=\"p6\" data-pid=\"6\"><strong>CUMPLIMIENTO</strong></h2>\n"
      "<p id=\"p7\" class=\"p7 sw\"><q class=\"scrp\">“Después de nacer Jesús en Belén de Judea [...]”.</q></p>\n"
      "<p id=\"p8\" class=\"p8 sw\"><a href=\"1001061144-split2.xhtml#chapter2_verse1\">Mateo 2:1</a></p>\n"
      "<h2 class=\"sw ruleAbove\" id=\"p15\" data-pid=\"15\"><span id=\"page15\" class=\"pageNum\" "
      "aria-hidden=\"true\" data-no=\"15\" data-before-text=\"15\"></span><strong>PROFECÍA</strong></h2>\n"
      "</div>\n<p id=\"p33\" data-pid=\"33\" class=\"p33\"></p>\n"));
  ASSERT_EQ(question.blocks.size(), 5u);
  EXPECT_EQ(question.blocks[0].kind, BlockKind::Heading);
  EXPECT_EQ(question.blocks[0].text, "PROFECÍA");
  EXPECT_EQ(question.blocks[1].kind, BlockKind::Verse);
  EXPECT_EQ(question.blocks[1].text,
            "“De ti, Belén Efrata, [...] de ti saldrá para mí el que será gobernante en Israel”.");
  EXPECT_EQ(question.blocks[1].reference, "Miqueas 5:2");
  EXPECT_EQ(question.blocks[2].kind, BlockKind::Heading);
  EXPECT_EQ(question.blocks[2].text, "CUMPLIMIENTO");
  EXPECT_EQ(question.blocks[3].kind, BlockKind::Verse);
  EXPECT_EQ(question.blocks[3].reference, "Mateo 2:1");
  EXPECT_EQ(question.blocks[4].kind, BlockKind::Heading);
  EXPECT_EQ(question.blocks[4].text, "PROFECÍA");
}

TEST(QuestionParser, ReadsQuestionNineteenProse) {
  const Question question = parsed(
      page("<div class=\"bodyTxt\">\n"
           "<h2 class=\"ss ruleAbove\" id=\"p3\" data-pid=\"3\"><strong>LAS ESCRITURAS HEBREAS, TAMBIÉN CONOCIDAS "
           "COMO ANTIGUO TESTAMENTO</strong></h2>\n"
           "<h3 id=\"p4\" data-pid=\"4\"><strong>EL PENTATEUCO (5 LIBROS):</strong></h3>\n"
           "<p id=\"p5\" data-pid=\"5\" class=\"p5 sw\"><strong>Génesis, Éxodo, Levítico, Números y "
           "Deuteronomio</strong></p>\n"
           "<p id=\"p6\" data-pid=\"6\" class=\"p6 sw\">Desde la creación del mundo hasta el nacimiento de la nación "
           "de Israel</p>\n"
           "</div>\n"));
  ASSERT_EQ(question.blocks.size(), 4u);
  EXPECT_EQ(question.blocks[0].kind, BlockKind::Heading);
  EXPECT_EQ(question.blocks[0].text, "LAS ESCRITURAS HEBREAS, TAMBIÉN CONOCIDAS COMO ANTIGUO TESTAMENTO");
  EXPECT_EQ(question.blocks[1].kind, BlockKind::Heading);
  EXPECT_EQ(question.blocks[1].text, "EL PENTATEUCO (5 LIBROS):");
  EXPECT_EQ(question.blocks[2].kind, BlockKind::Text);
  EXPECT_EQ(question.blocks[2].text, "Génesis, Éxodo, Levítico, Números y Deuteronomio");
  EXPECT_EQ(question.blocks[3].kind, BlockKind::Text);
  EXPECT_EQ(question.blocks[3].text, "Desde la creación del mundo hasta el nacimiento de la nación de Israel");
}

TEST(QuestionParser, CollapsesWhitespaceRuns) {
  const Question question = parsed(page("<p>\n  ¿Qué me enseña\n\t sobre   Dios?  </p>"));
  ASSERT_EQ(question.blocks.size(), 1u);
  EXPECT_EQ(question.blocks[0].text, "¿Qué me enseña sobre Dios?");
}

TEST(QuestionParser, AReferenceWithNoQuoteIsAVerseWithAnEmptyQuote) {
  const Question question =
      parsed(page("<p class=\"p15 sw\"><a href=\"1001061153-split6.xhtml#chapter6_verse1\">Efesios 6:1-3</a></p>"));
  ASSERT_EQ(question.blocks.size(), 1u);
  EXPECT_EQ(question.blocks[0].kind, BlockKind::Verse);
  EXPECT_EQ(question.blocks[0].text, "");
  EXPECT_EQ(question.blocks[0].reference, "Efesios 6:1-3");
  EXPECT_EQ(question.blocks[0].href, "1001061153-split6.xhtml#chapter6_verse1");
}

TEST(QuestionParser, AQuoteWithNoReferenceIsText) {
  const Question beforeHeading =
      parsed(page("<p><q class=\"scrp\">“Uno”.</q></p><h2><strong>CUMPLIMIENTO</strong></h2>"));
  ASSERT_EQ(beforeHeading.blocks.size(), 2u);
  EXPECT_EQ(beforeHeading.blocks[0].kind, BlockKind::Text);
  EXPECT_EQ(beforeHeading.blocks[0].text, "“Uno”.");
  EXPECT_EQ(beforeHeading.blocks[1].kind, BlockKind::Heading);

  const Question atEnd = parsed(page("<p>Antes</p><p><q class=\"scrp\">“Uno”.</q></p>"));
  ASSERT_EQ(atEnd.blocks.size(), 2u);
  EXPECT_EQ(atEnd.blocks[1].kind, BlockKind::Text);
  EXPECT_EQ(atEnd.blocks[1].text, "“Uno”.");
}

TEST(QuestionParser, OnlyTheSecondOfTwoQuotesPairs) {
  const Question question =
      parsed(page("<p><q class=\"scrp\">“Uno”.</q></p><p><q class=\"scrp\">“Dos”.</q></p>"
                  "<p><a href=\"a.xhtml#chapter1_verse2\">Juan 1:2</a></p>"));
  ASSERT_EQ(question.blocks.size(), 2u);
  EXPECT_EQ(question.blocks[0].kind, BlockKind::Text);
  EXPECT_EQ(question.blocks[0].text, "“Uno”.");
  EXPECT_EQ(question.blocks[1].kind, BlockKind::Verse);
  EXPECT_EQ(question.blocks[1].text, "“Dos”.");
}

TEST(QuestionParser, EmptyParagraphsAddNothingAndKeepAPair) {
  const Question question =
      parsed(page("<p><q class=\"scrp\">“Uno”.</q></p><p class=\"p9\"></p>"
                  "<p><a href=\"a.xhtml#chapter1_verse2\">Juan 1:2</a></p><p class=\"p19\"> </p>"));
  ASSERT_EQ(question.blocks.size(), 1u);
  EXPECT_EQ(question.blocks[0].kind, BlockKind::Verse);
  EXPECT_EQ(question.blocks[0].text, "“Uno”.");
}

TEST(QuestionParser, ProseWithAnInlineLinkIsText) {
  const Question question = parsed(page("<p>Lea <a href=\"a.xhtml#chapter3_verse16\">Juan 3:16</a> hoy.</p>"));
  ASSERT_EQ(question.blocks.size(), 1u);
  EXPECT_EQ(question.blocks[0].kind, BlockKind::Text);
  EXPECT_EQ(question.blocks[0].text, "Lea Juan 3:16 hoy.");
  EXPECT_EQ(question.blocks[0].href, "");
}

TEST(QuestionParser, ResolvesEntitiesInADocumentWithADoctype) {
  const Question question = parsed(pageWithDoctype("<p>a&nbsp;b &amp; c &foo; d</p>"));
  ASSERT_EQ(question.blocks.size(), 1u);
  EXPECT_EQ(question.blocks[0].text,
            "a\xC2\xA0"
            "b & c &foo; d");
}

TEST(QuestionParser, RefusesAnEntityWithoutADoctype) { EXPECT_TRUE(refused(page("<p>a&nbsp;b</p>"))); }

TEST(QuestionParser, RefusesMalformedMarkup) {
  QuestionParser parser;
  const std::string xhtml = "<html><body><p>open";
  EXPECT_FALSE(parser.feed(xhtml.data(), xhtml.size(), true));
  Question question;
  EXPECT_FALSE(parser.take(question));
  EXPECT_FALSE(parser.feed("</p>", 4, true));
  EXPECT_FALSE(parser.outOfMemory());
}

TEST(QuestionParser, TakesOnlyAfterTheFinalChunkAndOnlyOnce) {
  QuestionParser parser;
  const std::string xhtml = page("<p>Uno</p>");
  ASSERT_TRUE(parser.feed(xhtml.data(), xhtml.size(), false));
  Question question;
  EXPECT_FALSE(parser.take(question));
  ASSERT_TRUE(parser.feed("", 0, true));
  ASSERT_TRUE(parser.take(question));
  EXPECT_EQ(question.blocks.size(), 1u);
  EXPECT_FALSE(parser.take(question));
  EXPECT_FALSE(parser.feed("", 0, true));
}

TEST(QuestionParser, ChunkBoundariesDoNotChangeTheResult) {
  const std::string xhtml = pageWithDoctype(std::string(Q1_HEADER) + "<div class=\"bodyTxt\">\n" + Q1_FIGURE + Q1_PAIR +
                                            "<p class=\"p5 sw\">Él&nbsp;nos hizo &amp; le pertenecemos</p>\n</div>\n");
  Question expected;
  ASSERT_TRUE(preaching::parseQuestion(xhtml.data(), xhtml.size(), expected));
  ASSERT_EQ(expected.title, "¿Quién es Dios?");
  ASSERT_EQ(expected.blocks.size(), 2u);
  ASSERT_EQ(expected.blocks[0].reference, "Salmo 83:18");
  ASSERT_EQ(expected.blocks[1].text, "Él\xC2\xA0nos hizo & le pertenecemos");

  for (const size_t chunkBytes : {1u, 2u, 3u, 7u, 64u}) {
    expectSame(parsedInChunks(xhtml, chunkBytes), expected, "chunk " + std::to_string(chunkBytes));
  }
  for (size_t split = 1; split < xhtml.size(); split++) {
    QuestionParser parser;
    ASSERT_TRUE(parser.feed(xhtml.data(), split, false)) << "split " << split;
    ASSERT_TRUE(parser.feed(xhtml.data() + split, xhtml.size() - split, true)) << "split " << split;
    Question question;
    ASSERT_TRUE(parser.take(question));
    expectSame(question, expected, "split " + std::to_string(split));
  }
}

std::string paragraphs(const size_t count) {
  std::string body;
  for (size_t i = 0; i < count; i++) body += "<p>x</p>";
  return body;
}

TEST(QuestionParser, AcceptsExactlyTheBlockLimit) {
  EXPECT_EQ(parsed(page(paragraphs(preaching::MAX_BLOCKS))).blocks.size(), preaching::MAX_BLOCKS);
}

TEST(QuestionParser, RefusesOneBlockPastTheLimit) { EXPECT_TRUE(refused(page(paragraphs(preaching::MAX_BLOCKS + 1)))); }

TEST(QuestionParser, RefusesWhenTheTrailingUnpairedQuoteCrossesTheBlockLimit) {
  const std::string xhtml = page(paragraphs(preaching::MAX_BLOCKS) + "<p><q class=\"scrp\">“Uno”.</q></p>");
  QuestionParser parser;
  EXPECT_FALSE(parser.feed(xhtml.data(), xhtml.size(), true));
  Question question;
  EXPECT_FALSE(parser.take(question));
}

TEST(QuestionParser, AcceptsExactlyTheTextLimit) {
  const Question question = parsed(page("<p>" + std::string(preaching::MAX_TEXT_BYTES, 'x') + "</p>"));
  ASSERT_EQ(question.blocks.size(), 1u);
  EXPECT_EQ(question.blocks[0].text.size(), preaching::MAX_TEXT_BYTES);
}

TEST(QuestionParser, RefusesOneByteOverTheTextLimit) {
  EXPECT_TRUE(refused(page("<p>" + std::string(preaching::MAX_TEXT_BYTES + 1, 'x') + "</p>")));
}

TEST(QuestionParser, CountsTitleAndHrefTowardsTheTextLimit) {
  const std::string href = "a.xhtml#chapter1_verse1";
  const size_t room = preaching::MAX_TEXT_BYTES - 5 - 4 - href.size();
  const std::string fits =
      "<h1>Title</h1><p>" + std::string(room, 'x') + "</p><p><a href=\"" + href + "\">Juan</a></p>";
  EXPECT_EQ(parsed(page(fits)).blocks.size(), 2u);
  const std::string over =
      "<h1>Title</h1><p>" + std::string(room + 1, 'x') + "</p><p><a href=\"" + href + "\">Juan</a></p>";
  EXPECT_TRUE(refused(page(over)));
}

TEST(QuestionParser, RefusesAnOversizedParagraphBeforeItCloses) {
  const std::string opening = "<html><body><p>" + std::string(9000, 'x') + "<";
  QuestionParser parser;
  EXPECT_FALSE(parser.feed(opening.data(), opening.size(), false));
}

}  // namespace
```

**Step 3.3.** Replace the whole of `test/preaching/CMakeLists.txt` with its final version:

```cmake
# expat is compiled here from the repo's own sources with the same flags
# platformio.ini passes, so a question file is tokenised by the identical parser
# the firmware uses. xmltok_impl.c and xmltok_ns.c are #included by xmltok.c.
add_executable(PreachingTest
  IntroSectionTest.cpp
  QuestionParserTest.cpp
  ${REPO_ROOT}/lib/Preaching/Preaching/IntroSection.cpp
  ${REPO_ROOT}/lib/Preaching/Preaching/QuestionParser.cpp
  ${REPO_ROOT}/lib/Epub/Epub/htmlEntities.cpp
  ${REPO_ROOT}/lib/expat/xmlparse.c
  ${REPO_ROOT}/lib/expat/xmlrole.c
  ${REPO_ROOT}/lib/expat/xmltok.c
)

target_include_directories(PreachingTest PRIVATE
  ${REPO_ROOT}/lib/Preaching
  ${REPO_ROOT}/lib/Epub
  ${REPO_ROOT}/lib/Memory
  ${REPO_ROOT}/lib/expat
  ${REPO_ROOT}/test/stubs  # no-op Logging.h
)

target_compile_definitions(PreachingTest PRIVATE XML_GE=0 XML_CONTEXT_BYTES=1024)

target_link_libraries(PreachingTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(PreachingTest)
```

**Step 3.4 — run it, and see it fail.**

```bash
cmake --build build/test --target PreachingTest -j8
```

Expected: the build stops with a CMake or compile error because
`lib/Preaching/Preaching/QuestionParser.cpp` does not exist. That is the red state; nothing is
implemented yet.

**Step 3.5 — the implementation.** Create `lib/Preaching/Preaching/QuestionParser.cpp`:

```cpp
#include "Preaching/QuestionParser.h"

#include <Logging.h>
#include <Memory.h>
#include <expat.h>

#include <cstring>
#include <utility>

#include "Epub/htmlEntities.h"

namespace preaching {
namespace {

bool isAsciiSpace(const char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }

bool hasToken(const char* list, const char* token) {
  const size_t tokenLength = strlen(token);
  const char* at = list;
  while (*at) {
    while (*at == ' ') at++;
    const char* end = at;
    while (*end && *end != ' ') end++;
    if (static_cast<size_t>(end - at) == tokenLength && strncmp(at, token, tokenLength) == 0) return true;
    at = end;
  }
  return false;
}

const char* attribute(const XML_Char** atts, const char* name) {
  for (int i = 0; atts && atts[i]; i += 2) {
    if (strcmp(atts[i], name) == 0) return atts[i + 1];
  }
  return nullptr;
}

bool hasClass(const XML_Char** atts, const char* token) {
  const char* classes = attribute(atts, "class");
  return classes && hasToken(classes, token);
}

bool isHeading(const XML_Char* name) { return name[0] == 'h' && name[1] >= '1' && name[1] <= '6' && name[2] == '\0'; }

// The "PREGUNTA N" label, images, and printed page markers, which are empty
// spans carrying their number in an attribute.
bool isSkipped(const XML_Char* name, const XML_Char** atts) {
  if (strcmp(name, "figure") == 0) return true;
  if (strcmp(name, "p") == 0) return hasClass(atts, "contextTtl");
  if (strcmp(name, "span") == 0) return hasClass(atts, "pageNum");
  return false;
}

}  // namespace

struct QuestionParserState {
  enum class Collecting : uint8_t { None, Title, Heading, Paragraph };

  XML_Parser parser = nullptr;
  int skipDepth = 0;
  Collecting collecting = Collecting::None;
  // Depth below the collecting element, so text inside <strong>, <q> or <a> is kept.
  int collectDepth = 0;
  std::string buffer;
  bool pendingSpace = false;
  bool titleSeen = false;

  int linkDepth = 0;
  bool paragraphHasLink = false;
  bool paragraphHasQuote = false;
  bool paragraphHasNonLinkText = false;
  std::string paragraphHref;

  // A quote paragraph waiting for the reference paragraph after it.
  bool hasPendingQuote = false;
  std::string pendingQuote;

  std::string title;
  std::vector<Block> blocks;
  size_t committedBytes = 0;
  bool overLimit = false;

  size_t bytesInUse() const { return committedBytes + (hasPendingQuote ? pendingQuote.size() : 0) + buffer.size(); }

  bool admit(const size_t extraBytes) {
    if (bytesInUse() + extraBytes <= MAX_TEXT_BYTES) return true;
    refuse();
    return false;
  }

  void refuse() {
    overLimit = true;
    XML_StopParser(parser, XML_FALSE);
  }

  void open(const Collecting kind) {
    collecting = kind;
    collectDepth = 0;
    buffer.clear();
    pendingSpace = false;
    linkDepth = 0;
    paragraphHasLink = false;
    paragraphHasQuote = false;
    paragraphHasNonLinkText = false;
    paragraphHref.clear();
  }

  // Collapses ASCII whitespace runs to one space, with none leading or
  // trailing, so the buffer's size while growing is its final size.
  void append(const char* text, const int length) {
    for (int i = 0; i < length; i++) {
      const char c = text[i];
      if (isAsciiSpace(c)) {
        if (!buffer.empty()) pendingSpace = true;
        continue;
      }
      if (collecting == Collecting::Paragraph && linkDepth == 0) paragraphHasNonLinkText = true;
      if (!admit(pendingSpace ? 2 : 1)) return;
      if (pendingSpace) {
        buffer.push_back(' ');
        pendingSpace = false;
      }
      buffer.push_back(c);
    }
  }

  bool commit(const BlockKind kind, std::string text, std::string reference = {}, std::string href = {}) {
    if (blocks.size() >= MAX_BLOCKS) {
      refuse();
      return false;
    }
    committedBytes += text.size() + reference.size() + href.size();
    blocks.push_back(Block{kind, std::move(text), std::move(reference), std::move(href)});
    return true;
  }

  // A quote whose reference never came reads as text where it stood.
  void flushPendingQuote() {
    if (!hasPendingQuote) return;
    hasPendingQuote = false;
    std::string quote = std::move(pendingQuote);
    pendingQuote.clear();
    commit(BlockKind::Text, std::move(quote));
  }

  void closeParagraph() {
    const bool isReference = paragraphHasLink && !paragraphHasNonLinkText;
    if (isReference) {
      if (!admit(paragraphHref.size())) return;
      std::string quote;
      if (hasPendingQuote) {
        hasPendingQuote = false;
        quote = std::move(pendingQuote);
        pendingQuote.clear();
      }
      commit(BlockKind::Verse, std::move(quote), buffer, paragraphHref);
    } else if (paragraphHasQuote) {
      flushPendingQuote();
      pendingQuote = buffer;
      hasPendingQuote = true;
    } else {
      flushPendingQuote();
      commit(BlockKind::Text, buffer);
    }
  }

  void close() {
    const Collecting closing = collecting;
    collecting = Collecting::None;
    // An empty heading or paragraph (the publisher ends most files with one)
    // adds nothing and leaves a quote still waiting for its reference.
    if (buffer.empty()) return;
    if (closing == Collecting::Title) {
      flushPendingQuote();
      committedBytes += buffer.size();
      title = buffer;
    } else if (closing == Collecting::Heading) {
      flushPendingQuote();
      commit(BlockKind::Heading, buffer);
    } else if (closing == Collecting::Paragraph) {
      closeParagraph();
    }
    buffer.clear();
  }

  void clear() {
    title.clear();
    blocks.clear();
    pendingQuote.clear();
    hasPendingQuote = false;
    buffer.clear();
  }
};

namespace {

using Collecting = QuestionParserState::Collecting;

void XMLCALL onStart(void* userData, const XML_Char* name, const XML_Char** atts) {
  auto& state = *static_cast<QuestionParserState*>(userData);
  if (state.overLimit) return;
  if (state.skipDepth > 0) {
    state.skipDepth++;
    return;
  }
  if (isSkipped(name, atts)) {
    state.skipDepth = 1;
    return;
  }
  if (state.collecting != Collecting::None) {
    state.collectDepth++;
    if (state.collecting != Collecting::Paragraph) return;
    if (strcmp(name, "q") == 0 && hasClass(atts, "scrp")) state.paragraphHasQuote = true;
    if (strcmp(name, "a") == 0) {
      state.linkDepth++;
      const char* href = attribute(atts, "href");
      if (href && *href && !state.paragraphHasLink) {
        state.paragraphHasLink = true;
        state.paragraphHref = href;
      }
    }
    return;
  }
  if (strcmp(name, "h1") == 0 && !state.titleSeen) {
    state.titleSeen = true;
    state.open(Collecting::Title);
  } else if (isHeading(name)) {
    state.open(Collecting::Heading);
  } else if (strcmp(name, "p") == 0) {
    state.open(Collecting::Paragraph);
  }
}

void XMLCALL onEnd(void* userData, const XML_Char* name) {
  auto& state = *static_cast<QuestionParserState*>(userData);
  if (state.overLimit) return;
  if (state.skipDepth > 0) {
    state.skipDepth--;
    return;
  }
  if (state.collecting == Collecting::None) return;
  if (state.collectDepth > 0) {
    state.collectDepth--;
    if (strcmp(name, "a") == 0 && state.linkDepth > 0) state.linkDepth--;
    return;
  }
  state.close();
}

void XMLCALL onText(void* userData, const XML_Char* text, const int length) {
  auto& state = *static_cast<QuestionParserState*>(userData);
  if (state.overLimit || state.skipDepth > 0 || state.collecting == Collecting::None) return;
  state.append(text, length);
}

// Mirrors VerseAnchors' default handler. Under XML_GE=0 expat reports an
// undeclared entity here only once the document declares an external subset;
// without one the entity is a parse error and the question is refused.
void XMLCALL onDefault(void* userData, const XML_Char* s, const int length) {
  if (length >= 3 && s[0] == '&' && s[length - 1] == ';') {
    const char* value = lookupHtmlEntity(s, static_cast<size_t>(length));
    if (value != nullptr) {
      onText(userData, value, static_cast<int>(strlen(value)));
      return;
    }
    onText(userData, s, length);
  }
}

}  // namespace

QuestionParser::QuestionParser() {
  auto state = makeUniqueNoThrow<QuestionParserState>();
  if (!state) {
    LOG_ERR("PREACH", "OOM: question parser state");
    return;
  }
  state->blocks.reserve(MAX_BLOCKS);

  XML_Parser parser = XML_ParserCreate(nullptr);
  if (!parser) {
    LOG_ERR("PREACH", "OOM: question parser");
    return;
  }
  state->parser = parser;
  XML_SetUserData(parser, state.get());
  XML_SetElementHandler(parser, onStart, onEnd);
  XML_SetCharacterDataHandler(parser, onText);
  XML_SetDefaultHandlerExpand(parser, onDefault);

  parser_ = parser;
  state_ = std::move(state);
}

QuestionParser::~QuestionParser() {
  if (parser_) XML_ParserFree(static_cast<XML_Parser>(parser_));
}

bool QuestionParser::feed(const char* chunk, const size_t length, const bool isFinal) {
  if (!parser_ || failed_ || finished_) return false;
  auto parser = static_cast<XML_Parser>(parser_);
  const XML_Status status = XML_Parse(parser, chunk, static_cast<int>(length), isFinal ? 1 : 0);
  if (status != XML_STATUS_ERROR && isFinal) state_->flushPendingQuote();
  if (state_->overLimit) {
    LOG_ERR("PREACH", "Question refused: over %zu blocks or %zu bytes of text", MAX_BLOCKS, MAX_TEXT_BYTES);
    failed_ = true;
    state_->clear();
    return false;
  }
  if (status == XML_STATUS_ERROR) {
    const XML_Error error = XML_GetErrorCode(parser);
    outOfMemory_ = error == XML_ERROR_NO_MEMORY;
    LOG_ERR("PREACH", "Question parse failed: %s", XML_ErrorString(error));
    failed_ = true;
    state_->clear();
    return false;
  }
  if (isFinal) finished_ = true;
  return true;
}

bool QuestionParser::take(Question& out) {
  if (!state_ || !finished_ || failed_ || taken_) return false;
  out.title = std::move(state_->title);
  out.blocks = std::move(state_->blocks);
  taken_ = true;
  return true;
}

bool parseQuestion(const char* xhtml, const size_t length, Question& out) {
  QuestionParser parser;
  if (!parser.valid()) return false;
  if (!parser.feed(xhtml, length, /*isFinal=*/true)) return false;
  return parser.take(out);
}

}  // namespace preaching
```

How it maps to the spec, so a reviewer can check it:

- `admit`/`refuse` are the A12 bounds. `bytesInUse` counts the committed bytes, the pending quote
  and the open paragraph, so an oversized paragraph is refused while it grows.
- `commit` refuses a 65th block.
- `feed` flushes a trailing quote **before** it checks `overLimit` (review MAJOR 2). Its two
  `LOG_ERR` calls each have a literal format (review MAJOR 3).
- `close` drops empty blocks without touching a pending quote (A10).
- `closeParagraph` is the A7/A11 classification.
- `onDefault` is A9.
- `Collecting` is nested in the state struct rather than in the anonymous namespace. A struct with
  external linkage that holds an anonymous-namespace type draws GCC's `-Wsubobject-linkage` once it
  moves to a header.

**Step 3.6 — run it again.**

```bash
cmake --build build/test --target PreachingTest -j8 2>&1 | grep -E "warning|error" | grep -E "lib/Preaching|test/preaching"; build/test/preaching/PreachingTest
```

Expected: the `grep` prints nothing (GoogleTest's own `gtest-printers.h` warning under clang 21 is
pre-existing and not ours), and the run ends `[  PASSED  ] 33 tests.`

**Step 3.7 — commit.**

```bash
git add lib/Preaching/Preaching/QuestionParser.h lib/Preaching/Preaching/QuestionParser.cpp test/preaching/QuestionParserTest.cpp test/preaching/CMakeLists.txt
git commit -m "feat(preaching): parse a question into verse, heading and text blocks" -m "Claude-Session: https://claude.ai/code/session_01G51hvdrPkE2Hm6TU4eW2CY"
```

## Task 4 — compile the library once with the device toolchain (5–15 min, mostly build time)

The host stub makes `LOG_ERR` a no-op (`test/stubs/Logging.h:6`). The device macro is
printf-checked and pastes `format "\n"` (`lib/Logging/Logging.h:42,46`). Nothing in `src/`
includes `<Preaching/…>` yet, so PlatformIO would not otherwise compile the library (spec,
"Device build"). This task forces one device compile and then removes every trace of it.

**Step 4.1.** Create `src/PreachingBuildCheck.cpp`, temporary and never committed:

```cpp
#include <Preaching/IntroSection.h>
#include <Preaching/QuestionParser.h>
```

**Step 4.2.** Build, keeping the log in the gitignored `build/`:

```bash
~/.platformio/penv/bin/pio run -e x4pro 2>&1 | tee build/pio-x4pro-check.log | tail -5
find .pio/build/x4pro -path '*Preaching*' -name '*.o' | head
grep -nE "warning|error" build/pio-x4pro-check.log | grep -E "lib/Preaching|PreachingBuildCheck" || echo "no Preaching warnings"
```

Expected:

- the build ends `[SUCCESS]`;
- `find` lists `QuestionParser.cpp.o` and `IntroSection.cpp.o` under a `Preaching` library
  directory, which proves the library was compiled;
- the last command prints `no Preaching warnings`.

If any warning names `lib/Preaching`, fix it in the library, rerun Task 3's step 3.6, rerun this
step, and fold the fix into a new commit
`fix(preaching): clear a device-compiler warning` (with the trailer).

**Step 4.3 — remove the check and prove the tree is clean.**

```bash
rm src/PreachingBuildCheck.cpp
git status --short
```

Expected: `git status --short` prints nothing (or only files a fix in step 4.2 changed, which you
commit as described there). `src/PreachingBuildCheck.cpp` must not appear in any commit.

## Task 5 — final verification (5–15 min)

**Step 5.1 — the committed tree builds for the device.**

```bash
~/.platformio/penv/bin/pio run -e x4pro 2>&1 | tee build/pio-x4pro.log | tail -5
```

Expected: `[SUCCESS]`. This proves only that the committed tree, without the temporary include,
still builds. Nothing in `src/` includes the library now, so PlatformIO does not compile it here
and this log cannot show its warnings. Step 4.2 is the warning check for `lib/Preaching`; do not
grep this log for it.

**Step 5.2 — formatting, as CI runs it.** The new files are committed, so `-g` would miss them.
Run the whole-tree form:

```bash
./bin/clang-format-fix
git status --short
```

Expected: exit 0 and no changes. If it reformatted anything under `lib/Preaching/` or
`test/preaching/`, rerun step 3.6, then commit those paths only:

```bash
git add lib/Preaching test/preaching
git commit -m "style(preaching): apply clang-format" -m "Claude-Session: https://claude.ai/code/session_01G51hvdrPkE2Hm6TU4eW2CY"
```

If it changed files outside this task's paths, do not commit them: restore them with
`git checkout -- <path>` and report it.

**Step 5.3 — the suite, once more, from a clean configure.**

```bash
cmake -S test -B build/test && cmake --build build/test --target PreachingTest -j8 && ctest --test-dir build/test -R '^(IntroSection|QuestionParser)\.' --output-on-failure
```

Expected: `100% tests passed, 0 tests failed out of 33`.

**Step 5.4 — report.** Report back:

- the commit list, from `git log --oneline origin/feature/preaching-intro-parser..HEAD`;
- the test count;
- whether step 4.2's `find` showed the `Preaching` objects, and step 4.2's warning grep result
  (the only device-compiler warning check this library gets before T3 includes it);
- the `[SUCCESS]` lines from both device builds;
- that `git status --short` is clean.

Do not push.

## Appendix A — final contents for reference

After Task 2, `lib/Preaching/Preaching/IntroSection.h` reads:

```cpp
#pragma once

#include <cstdint>
#include <string_view>

// The NWT's front-matter section of twenty preaching questions,
// "Introducción a la Palabra de Dios".
namespace preaching::IntroSection {

inline constexpr int QUESTION_COUNT = 20;
inline constexpr int SLOT_COUNT = QUESTION_COUNT + 1;
inline constexpr int INDEX_SLOT = 0;

// Slot 0 is the section's index page; slot n is question n. Found by filename
// rather than by title: the titles are translated, the filenames are the same
// in every edition checked.
inline constexpr const char* BASENAMES[SLOT_COUNT] = {
    "1001061250.xhtml", "1001061251.xhtml", "1001061252.xhtml", "1001061253.xhtml", "1001061254.xhtml",
    "1001061255.xhtml", "1001061256.xhtml", "1001061257.xhtml", "1001061258.xhtml", "1001061259.xhtml",
    "1001061260.xhtml", "1001061261.xhtml", "1001061262.xhtml", "1001061263.xhtml", "1001061264.xhtml",
    "1001061265.xhtml", "1001061266.xhtml", "1001061267.xhtml", "1001061268.xhtml", "1001061269.xhtml",
    "1001061270.xhtml",
};

// The question to offer on a return visit, by question number; index 0 is unused.
inline constexpr uint8_t NEXT_QUESTION[SLOT_COUNT] = {0,  2,  3,  4,  5,  6,  7,  10, 9,  10, 11,
                                                      12, 18, 14, 15, 16, 18, 18, 20, 20, 0};

// 0 when there is no follow-up or `question` is outside 1..QUESTION_COUNT.
constexpr int nextQuestion(const int question) {
  if (question < 1 || question > QUESTION_COUNT) return 0;
  return NEXT_QUESTION[question];
}

// "Pregunta 8: ¿Tiene Dios la culpa del sufrimiento?" -> the question alone.
// A label that does not have that shape, or would strip to nothing, comes back
// whole so a row is never blank. The result views into `label`.
std::string_view stripTocPrefix(std::string_view label);

}  // namespace preaching::IntroSection
```

After Task 2, `test/preaching/IntroSectionTest.cpp` reads:

```cpp
#include <gtest/gtest.h>

#include <string>

#include "Preaching/IntroSection.h"

namespace {

using preaching::IntroSection::BASENAMES;
using preaching::IntroSection::nextQuestion;
using preaching::IntroSection::stripTocPrefix;

TEST(IntroSection, IndexPageIsSlotZero) {
  EXPECT_STREQ(BASENAMES[preaching::IntroSection::INDEX_SLOT], "1001061250.xhtml");
}

TEST(IntroSection, QuestionNIsSlotN) {
  ASSERT_EQ(preaching::IntroSection::SLOT_COUNT, 21);
  for (int slot = 0; slot < preaching::IntroSection::SLOT_COUNT; slot++) {
    EXPECT_EQ(std::string(BASENAMES[slot]), "10010612" + std::to_string(50 + slot) + ".xhtml") << "slot " << slot;
  }
}

// The design's follow-up table, by question number; index 0 is unused.
constexpr int EXPECTED_FOLLOW_UP[] = {0, 2, 3, 4, 5, 6, 7, 10, 9, 10, 11, 12, 18, 14, 15, 16, 18, 18, 20, 20, 0};

constexpr bool followUpsMatchTheDesign() {
  for (int question = 1; question <= preaching::IntroSection::QUESTION_COUNT; question++) {
    if (nextQuestion(question) != EXPECTED_FOLLOW_UP[question]) return false;
  }
  return true;
}
static_assert(followUpsMatchTheDesign());

TEST(IntroSection, FollowUpTableMatchesTheDesign) {
  for (int question = 1; question <= preaching::IntroSection::QUESTION_COUNT; question++) {
    EXPECT_EQ(nextQuestion(question), EXPECTED_FOLLOW_UP[question]) << "question " << question;
  }
}

TEST(IntroSection, FollowUpOutOfRangeIsNone) {
  EXPECT_EQ(nextQuestion(0), 0);
  EXPECT_EQ(nextQuestion(-1), 0);
  EXPECT_EQ(nextQuestion(21), 0);
}

TEST(IntroSection, StripsTheTocPrefix) {
  EXPECT_EQ(stripTocPrefix("Pregunta 8: ¿Tiene Dios la culpa del sufrimiento?"),
            "¿Tiene Dios la culpa del sufrimiento?");
  EXPECT_EQ(stripTocPrefix("Question 12: Can the Dead Live Again?"), "Can the Dead Live Again?");
  EXPECT_EQ(stripTocPrefix("Pregunta 1: ¿Quién es Dios?"), "¿Quién es Dios?");
}

TEST(IntroSection, ReturnsAnUnprefixedLabelWhole) {
  for (const char* label : {"Introducción a la Palabra de Dios", "", "Pregunta: x", "Pregunta 8 x", "Pregunta 8:x",
                            " 8: x", "Pregunta 8: "}) {
    EXPECT_EQ(stripTocPrefix(label), label) << '"' << label << '"';
  }
}

}  // namespace
```

## Changes after plan review pass 0

`docs/superpowers/reviews/berean-o-14-plan-review-0.md` (CLEAR, 4 MINOR); all accepted. The
changed code was rebuilt and rerun against the repo's sources (33 tests pass). Each skip was
removed in turn: only the new test failed, so it guards both.

- **The skips were untested.** Removing the `<figure>` or `pageNum` skip left every test green,
  because the fixtures' figure held only an `<img>` and their `pageNum` spans were empty. The new
  test `SkipsFigureCaptionsAndPageNumbersThatCarryText` fails under either removal.
- **Step 5.1's warning check was hollow.** It ran after the temporary include was gone, so the
  library was not compiled. Step 5.1 now checks only `[SUCCESS]`, and step 5.4 reports step 4.2's
  warning result.
- **`static_assert` for every follow-up.** The three single `static_assert`s are replaced by one
  `static_assert(followUpsMatchTheDesign())` over all 20 questions. A shared `EXPECTED_FOLLOW_UP`
  table also drives the runtime test.
- **An allocation the spec did not budget.** `buffer.reserve(1024)` and `PARAGRAPH_RESERVE_BYTES`
  are gone. The paragraph buffer is cleared, not freed, between blocks, so it grows to the longest
  paragraph once and is reused. The only reservation left is the spec's `blocks.reserve(MAX_BLOCKS)`.
