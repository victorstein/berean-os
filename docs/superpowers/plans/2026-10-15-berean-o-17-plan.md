# berean-o-17 (t4) — Show mode: implementation plan

Spec: `docs/superpowers/specs/2026-10-15-berean-o-17-design.md` (cleared review 0). Assumption
numbers (A1–A18) below are the spec's. Branch: `feature/preaching-show-mode`. Never push, open a
PR or run `hpipe submit` from this plan.

FILES: src/activities/preaching/PreachingShowLayout.h, src/activities/preaching/PreachingShowLayout.cpp
FILES: src/activities/preaching/PreachingShowActivity.h, src/activities/preaching/PreachingShowActivity.cpp
FILES: src/activities/preaching/PreachingTopicActivity.h, src/activities/preaching/PreachingTopicActivity.cpp
FILES: test/ui_layout/CMakeLists.txt, test/ui_layout/PreachingShowLayoutTest.cpp

## Rules for every step

- **Code is self-documenting.** Meaningful names. A comment only for a non-obvious *why*: a
  constraint, a gotcha, a workaround. No comments that restate the code or narrate history. The
  code below already follows this; keep it that way.
- `CLAUDE.md` holds: `makeUniqueNoThrow` + `LOG_ERR` for fallible allocations, no new
  `std::function`, `constexpr` tables, `tr()` for user-facing text, locals under 256 B,
  `.reserve()` before growth.
- Re-read every file before you edit it — a sibling task may have changed it since this plan was
  written. If `PreachingTopicActivity.{h,cpp}` no longer match the "old" text quoted in Task 6,
  adapt the edit to the current text without undoing the sibling's work.
- `pio` is not on PATH: use `~/.platformio/penv/bin/pio`.
- Every commit message ends with a blank line and then
  `Claude-Session: https://claude.ai/code/session_01HGfEft5zNU6U3sPjFTUYvP`.
  Commit only the files the step names (`git add <paths>`; never `git add -A`).

Host test commands used throughout (from the worktree root):

```bash
cmake --build build/test --target PreachingCardsTest
./build/test/ui_layout/PreachingCardsTest --gtest_filter='PreachingShow*'
```

---

## Task 0 — Bootstrap and baseline (no commit)

**0.1** Make the worktree buildable and configure the host tests:

```bash
./bin/bootstrap
cmake -S test -B build/test
cmake --build build/test --target PreachingCardsTest
./build/test/ui_layout/PreachingCardsTest
```

Expected: all existing `PreachingCardLayout*` / `PreachingCardNav*` tests pass. If the first
`cmake --build` fails while fetching or building googletest, run it once more (a known first-build
race in a fresh worktree).

**0.2** Record the firmware warning baseline (takes several minutes):

```bash
~/.platformio/penv/bin/pio run -e x4pro > .pio/baseline-build.log 2>&1; echo "exit $?"
grep -c "warning:" .pio/baseline-build.log
```

Expected: `exit 0`. Note the warning count; Task 7 compares against it. (`.pio/` is gitignored;
the log is never committed.)

---

## Task 1 — The deck view (A1)

**1.1** Create `src/activities/preaching/PreachingShowLayout.h` — the whole header, used unchanged
by every later task:

```cpp
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "PreachingCardLayout.h"

// Show mode's layout: one Verse card, as large as it fits -- the first of three rungs (the screen's
// Noto Serif 18, 14, 12) at which the quote and its reference fit one page with no word split --
// and paged at the last rung past that. Nothing is cut. Free of Arduino and the renderer -- widths
// come through MeasureFn -- so test/ui_layout runs it on the host (modelled on PreachingCardLayout.h
// and PreachingCardNav.h).
namespace preaching::show {

inline constexpr uint8_t RUNG_COUNT = 3;
inline constexpr uint8_t PAGING_RUNG = RUNG_COUNT - 1;

// A topic's cards, viewed: whatever owns them outlives every use of the Deck.
struct Deck {
  const CardInput* cards = nullptr;
  size_t count = 0;
};

int verseCount(const Deck& deck);
// The Verse card with this ordinal among the deck's Verse cards; nullptr out of range.
const CardInput* verseCard(const Deck& deck, int ordinal);

// Pixel width of `text` at `rung`, set in Face::Quote or Face::Reference.
using MeasureFn = int (*)(const void* ctx, uint8_t rung, Face face, const char* text);

struct Metrics {
  int width = 0;         // the text column, px
  int height = 0;        // the box the quote and its reference share, px
  int referenceGap = 0;  // above the reference, on the page that also holds a quote line
  int lineHeight[RUNG_COUNT] = {};
};

// The quote's lines, then the reference's, numbered as one column from 0.
struct Layout {
  uint8_t rung = 0;
  std::vector<Span> quote;              // into the card's body
  std::vector<Span> reference;          // into the card's reference
  std::vector<uint16_t> pageFirstLine;  // page p holds lines [pageFirstLine[p], pageEnd(p))

  int lineCount() const { return static_cast<int>(quote.size() + reference.size()); }
  int pageCount() const { return static_cast<int>(pageFirstLine.size()); }
  int pageEnd(int page) const;
  bool isReferenceLine(const int line) const { return line >= static_cast<int>(quote.size()); }
  // The page's lines, plus the gap when it holds both quote and reference lines.
  int pageHeight(int page, const Metrics& metrics) const;
};

// Clears and refills `out`'s vectors rather than reassigning `out`, so capacity reserved by the
// caller survives every verse change. Always at least one page.
void layOut(const CardInput& card, const Metrics& metrics, MeasureFn measure, const void* ctx, Layout& out);

struct Cursor {
  int verse = 0;
  int page = 0;
};

// direction > 0: the next page, else the next verse at its first page. direction < 0: the previous
// page, else the previous verse at its first page. False, with `cursor` unchanged, at either end.
bool step(Cursor& cursor, int pagesInVerse, int verses, int direction);

}  // namespace preaching::show
```

**1.2** Create `test/ui_layout/PreachingShowLayoutTest.cpp`:

```cpp
#include <gtest/gtest.h>

#include <cstring>
#include <string>
#include <string_view>
#include <vector>

#include "activities/preaching/PreachingShowLayout.h"

using preaching::CardInput;
using preaching::CardKind;
using preaching::Face;
using preaching::Span;
namespace show = preaching::show;

namespace {

std::vector<CardInput> mixedQuestionCards() {
  return {
      {CardKind::Title, "Title", ""},       {CardKind::Verse, "first", "Ref 1"},
      {CardKind::Heading, "Heading", ""},   {CardKind::Verse, "second", "Ref 2"},
      {CardKind::Text, "Text", ""},         {CardKind::NextVisit, "Next", "Pregunta 2"},
  };
}

}  // namespace

TEST(PreachingShowDeck, CountsOnlyVerseCards) {
  const auto cards = mixedQuestionCards();
  EXPECT_EQ(show::verseCount({cards.data(), cards.size()}), 2);
}

TEST(PreachingShowDeck, FindsTheNthVerseCardPastOtherKinds) {
  const auto cards = mixedQuestionCards();
  const show::Deck deck{cards.data(), cards.size()};
  ASSERT_NE(show::verseCard(deck, 0), nullptr);
  EXPECT_EQ(show::verseCard(deck, 0)->body, "first");
  ASSERT_NE(show::verseCard(deck, 1), nullptr);
  EXPECT_EQ(show::verseCard(deck, 1)->body, "second");
}

TEST(PreachingShowDeck, OutOfRangeIsNull) {
  const auto cards = mixedQuestionCards();
  const show::Deck deck{cards.data(), cards.size()};
  EXPECT_EQ(show::verseCard(deck, -1), nullptr);
  EXPECT_EQ(show::verseCard(deck, 2), nullptr);
  EXPECT_EQ(show::verseCount({}), 0);
  EXPECT_EQ(show::verseCard({}, 0), nullptr);
}

TEST(PreachingShowDeck, AllVerseDeckMapsOrdinalToIndex) {
  const std::vector<CardInput> cards = {
      {CardKind::Verse, "a", "Ref a"}, {CardKind::Verse, "b", "Ref b"}, {CardKind::Verse, "c", "Ref c"}};
  const show::Deck deck{cards.data(), cards.size()};
  EXPECT_EQ(show::verseCount(deck), 3);
  for (int ordinal = 0; ordinal < 3; ++ordinal) EXPECT_EQ(show::verseCard(deck, ordinal), &cards[ordinal]);
}
```

**1.3** Add the new sources to the `PreachingCardsTest` target in `test/ui_layout/CMakeLists.txt`.
Replace:

```cmake
add_executable(PreachingCardsTest
  PreachingCardLayoutTest.cpp
  PreachingCardNavTest.cpp
  ${REPO_ROOT}/src/activities/preaching/PreachingCardLayout.cpp
)
```

with:

```cmake
add_executable(PreachingCardsTest
  PreachingCardLayoutTest.cpp
  PreachingCardNavTest.cpp
  PreachingShowLayoutTest.cpp
  ${REPO_ROOT}/src/activities/preaching/PreachingCardLayout.cpp
  ${REPO_ROOT}/src/activities/preaching/PreachingShowLayout.cpp
)
```

**1.4** Create `src/activities/preaching/PreachingShowLayout.cpp` with only the include and the
namespace (no functions yet), so the test fails to link:

```cpp
#include "PreachingShowLayout.h"

namespace preaching::show {}  // namespace preaching::show
```

Run the host commands. Expected: the link fails with undefined `preaching::show::verseCount` /
`verseCard`.

**1.5** Replace `src/activities/preaching/PreachingShowLayout.cpp` with:

```cpp
#include "PreachingShowLayout.h"

namespace preaching::show {

int verseCount(const Deck& deck) {
  int verses = 0;
  for (size_t i = 0; i < deck.count; ++i) {
    if (deck.cards[i].kind == CardKind::Verse) ++verses;
  }
  return verses;
}

const CardInput* verseCard(const Deck& deck, const int ordinal) {
  if (ordinal < 0) return nullptr;
  int seen = 0;
  for (size_t i = 0; i < deck.count; ++i) {
    if (deck.cards[i].kind != CardKind::Verse) continue;
    if (seen == ordinal) return &deck.cards[i];
    ++seen;
  }
  return nullptr;
}

}  // namespace preaching::show
```

Run the host commands. Expected: 4 `PreachingShowDeck` tests pass. Also run
`./build/test/ui_layout/PreachingCardsTest` unfiltered: everything passes.

**1.6** Commit:

```bash
git add src/activities/preaching/PreachingShowLayout.h src/activities/preaching/PreachingShowLayout.cpp \
  test/ui_layout/PreachingShowLayoutTest.cpp test/ui_layout/CMakeLists.txt
git commit -m "feat(preaching): view a topic's Verse cards for show mode" -m "Claude-Session: https://claude.ai/code/session_01HGfEft5zNU6U3sPjFTUYvP"
```

---

## Task 2 — Stepping between pages and verses (A13)

**2.1** Append to the end of `test/ui_layout/PreachingShowLayoutTest.cpp`:

```cpp
TEST(PreachingShowStep, NextTurnsThePageWithinAVerse) {
  show::Cursor cursor{0, 0};
  EXPECT_TRUE(show::step(cursor, 3, 2, 1));
  EXPECT_EQ(cursor.verse, 0);
  EXPECT_EQ(cursor.page, 1);
}

TEST(PreachingShowStep, NextFromALastPageOpensTheNextVerseAtItsStart) {
  show::Cursor cursor{0, 2};
  EXPECT_TRUE(show::step(cursor, 3, 2, 1));
  EXPECT_EQ(cursor.verse, 1);
  EXPECT_EQ(cursor.page, 0);
}

TEST(PreachingShowStep, NextAtTheEndMovesNowhere) {
  show::Cursor cursor{1, 0};
  EXPECT_FALSE(show::step(cursor, 1, 2, 1));
  EXPECT_EQ(cursor.verse, 1);
  EXPECT_EQ(cursor.page, 0);
}

TEST(PreachingShowStep, PreviousTurnsBackWithinAVerse) {
  show::Cursor cursor{1, 2};
  EXPECT_TRUE(show::step(cursor, 3, 2, -1));
  EXPECT_EQ(cursor.verse, 1);
  EXPECT_EQ(cursor.page, 1);
}

TEST(PreachingShowStep, PreviousFromAFirstPageOpensThePreviousVerseAtItsStart) {
  show::Cursor cursor{1, 0};
  EXPECT_TRUE(show::step(cursor, 1, 2, -1));
  EXPECT_EQ(cursor.verse, 0);
  EXPECT_EQ(cursor.page, 0);
}

TEST(PreachingShowStep, PreviousAtTheStartMovesNowhere) {
  show::Cursor cursor{0, 0};
  EXPECT_FALSE(show::step(cursor, 3, 2, -1));
  EXPECT_EQ(cursor.verse, 0);
  EXPECT_EQ(cursor.page, 0);
}
```

Run the host commands. Expected: link fails, undefined `preaching::show::step`.

**2.2** In `src/activities/preaching/PreachingShowLayout.cpp`, insert before the closing
`}  // namespace preaching::show`:

```cpp
bool step(Cursor& cursor, const int pagesInVerse, const int verses, const int direction) {
  if (direction > 0) {
    if (cursor.page + 1 < pagesInVerse) {
      ++cursor.page;
      return true;
    }
    if (cursor.verse + 1 < verses) {
      ++cursor.verse;
      cursor.page = 0;
      return true;
    }
    return false;
  }
  if (cursor.page > 0) {
    --cursor.page;
    return true;
  }
  if (cursor.verse > 0) {
    --cursor.verse;
    cursor.page = 0;
    return true;
  }
  return false;
}
```

Run the host commands. Expected: 10 `PreachingShow*` tests pass.

**2.3** Commit:

```bash
git add src/activities/preaching/PreachingShowLayout.cpp test/ui_layout/PreachingShowLayoutTest.cpp
git commit -m "feat(preaching): step show mode through pages and verses" -m "Claude-Session: https://claude.ai/code/session_01HGfEft5zNU6U3sPjFTUYvP"
```

---

## Task 3 — The size ladder, one page (A3, A4)

The fake measure is 14/12/10 px per byte at rungs 0/1/2, line heights 30/24/20, column 200 px,
gap 10. So a line holds at most 14, 16 and 20 bytes.

**3.1** Append to the end of `test/ui_layout/PreachingShowLayoutTest.cpp`:

```cpp
namespace {

constexpr int PX_PER_BYTE[show::RUNG_COUNT] = {14, 12, 10};

int widthByRung(const void*, const uint8_t rung, Face, const char* text) {
  return static_cast<int>(std::strlen(text)) * PX_PER_BYTE[rung];
}

show::Metrics showMetrics(const int height) {
  show::Metrics metrics;
  metrics.width = 200;
  metrics.height = height;
  metrics.referenceGap = 10;
  metrics.lineHeight[0] = 30;
  metrics.lineHeight[1] = 24;
  metrics.lineHeight[2] = 20;
  return metrics;
}

show::Layout layOut(const std::string_view quote, const std::string_view reference, const int height) {
  show::Layout layout;
  show::layOut(CardInput{CardKind::Verse, quote, reference}, showMetrics(height), &widthByRung, nullptr, layout);
  return layout;
}

}  // namespace

TEST(PreachingShowFit, AShortQuoteTakesTheLargestSize) {
  const auto layout = layOut("one two three", "Jn 3:16", 100);
  EXPECT_EQ(layout.rung, 0);
  EXPECT_EQ(layout.quote.size(), 1u);
  EXPECT_EQ(layout.reference.size(), 1u);
  EXPECT_EQ(layout.pageCount(), 1);
  EXPECT_EQ(layout.pageHeight(0, showMetrics(100)), 70);
}

TEST(PreachingShowFit, StepsDownToTheMiddleSizeWhenTheLargestIsTooTall) {
  // Rung 0: 2 quote lines, 100 px. Rung 1: 2 lines, 82 px.
  const auto layout = layOut("abcd efgh ijkl mnop", "Jn 3:16", 90);
  EXPECT_EQ(layout.rung, 1);
  EXPECT_EQ(layout.pageCount(), 1);
}

TEST(PreachingShowFit, StepsDownToTheSmallestSize) {
  // Rung 1: 82 px. Rung 2: one 19-byte line, 50 px.
  const auto layout = layOut("abcd efgh ijkl mnop", "Jn 3:16", 60);
  EXPECT_EQ(layout.rung, 2);
  EXPECT_EQ(layout.quote.size(), 1u);
  EXPECT_EQ(layout.pageCount(), 1);
}

TEST(PreachingShowFit, AWordWiderThanTheColumnStepsDownRatherThanSplit) {
  // 16 bytes: 224 px at rung 0, 192 px at rung 1. The height fits either.
  const auto layout = layOut("abcdefghijklmnop", "Jn 3:16", 500);
  EXPECT_EQ(layout.rung, 1);
  ASSERT_EQ(layout.quote.size(), 1u);
  EXPECT_EQ(layout.quote[0].length, 16);
}

TEST(PreachingShowFit, AnEmptyQuoteShowsTheReferenceAlone) {
  const auto layout = layOut("", "Jn 3:16", 100);
  EXPECT_TRUE(layout.quote.empty());
  EXPECT_EQ(layout.reference.size(), 1u);
  EXPECT_EQ(layout.pageCount(), 1);
  EXPECT_EQ(layout.pageHeight(0, showMetrics(100)), 30);
}

TEST(PreachingShowFit, AnEmptyReferenceCountsNoGap) {
  const auto layout = layOut("one two three", "", 100);
  EXPECT_TRUE(layout.reference.empty());
  EXPECT_EQ(layout.pageCount(), 1);
  EXPECT_EQ(layout.pageHeight(0, showMetrics(100)), 30);
}

TEST(PreachingShowFit, AnEmptyCardIsOneEmptyPage) {
  const auto layout = layOut("", "", 100);
  EXPECT_EQ(layout.lineCount(), 0);
  EXPECT_EQ(layout.pageCount(), 1);
  EXPECT_EQ(layout.pageEnd(0), 0);
  EXPECT_EQ(layout.pageHeight(0, showMetrics(100)), 0);
}

TEST(PreachingShowFit, ReusesTheCallersCapacity) {
  show::Layout layout;
  layout.quote.reserve(64);
  layout.reference.reserve(4);
  layout.pageFirstLine.reserve(8);
  const auto* quoteStorage = layout.quote.data();
  show::layOut(CardInput{CardKind::Verse, "abcd efgh ijkl mnop", "Jn 3:16"}, showMetrics(60), &widthByRung, nullptr,
               layout);
  EXPECT_EQ(layout.quote.data(), quoteStorage);
  EXPECT_GE(layout.quote.capacity(), 64u);
  EXPECT_GE(layout.pageFirstLine.capacity(), 8u);
}
```

Run the host commands. Expected: link fails, undefined `preaching::show::layOut`,
`Layout::pageHeight`, `Layout::pageEnd`.

**3.2** In `src/activities/preaching/PreachingShowLayout.cpp`:

Replace the first two lines

```cpp
#include "PreachingShowLayout.h"

namespace preaching::show {
```

with:

```cpp
#include "PreachingShowLayout.h"

#include <string_view>

namespace preaching::show {

namespace {

// wrapText's break bytes; PreachingCardLayout.cpp keeps its own copy file-local.
bool isBreak(const char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }

// wrapText ends a line inside a word only when that word is wider than the column.
bool splitsAWord(const std::string_view text, const std::vector<Span>& spans) {
  for (const Span span : spans) {
    const size_t end = static_cast<size_t>(span.begin) + span.length;
    if (end < text.size() && !isBreak(text[end])) return true;
  }
  return false;
}

struct RungMeasure {
  MeasureFn measure;
  const void* ctx;
  uint8_t rung;
};

int measureAtRung(const void* ctx, const Face face, const char* text) {
  const auto* rungMeasure = static_cast<const RungMeasure*>(ctx);
  return rungMeasure->measure(rungMeasure->ctx, rungMeasure->rung, face, text);
}

int linesHeight(const Layout& layout, const Metrics& metrics, const int first, const int end) {
  const int quoteLines = static_cast<int>(layout.quote.size());
  const bool holdsBoth = first < quoteLines && end > quoteLines;
  return (end - first) * metrics.lineHeight[layout.rung] + (holdsBoth ? metrics.referenceGap : 0);
}

void paginate(Layout& out, const Metrics&) { out.pageFirstLine.push_back(0); }

}  // namespace

int Layout::pageEnd(const int page) const {
  return page + 1 < pageCount() ? pageFirstLine[static_cast<size_t>(page) + 1] : lineCount();
}

int Layout::pageHeight(const int page, const Metrics& metrics) const {
  return linesHeight(*this, metrics, pageFirstLine[static_cast<size_t>(page)], pageEnd(page));
}

void layOut(const CardInput& card, const Metrics& metrics, const MeasureFn measure, const void* const ctx,
            Layout& out) {
  for (uint8_t rung = 0; rung < RUNG_COUNT; ++rung) {
    out.rung = rung;
    out.quote.clear();
    out.reference.clear();
    out.pageFirstLine.clear();
    const RungMeasure rungMeasure{measure, ctx, rung};
    wrapText(card.body, Face::Quote, metrics.width, &measureAtRung, &rungMeasure, out.quote);
    wrapText(card.reference, Face::Reference, metrics.width, &measureAtRung, &rungMeasure, out.reference);
    const bool fitsOnePage = !splitsAWord(card.body, out.quote) && !splitsAWord(card.reference, out.reference) &&
                             linesHeight(out, metrics, 0, out.lineCount()) <= metrics.height;
    if (fitsOnePage || rung == PAGING_RUNG) break;
  }
  paginate(out, metrics);
}
```

(The `verseCount`, `verseCard` and `step` definitions stay where they are, after this block.)

Run the host commands. Expected: 18 `PreachingShow*` tests pass.

**3.3** Commit:

```bash
git add src/activities/preaching/PreachingShowLayout.cpp test/ui_layout/PreachingShowLayoutTest.cpp
git commit -m "feat(preaching): size a show-mode verse 18, 14 or 12" -m "Claude-Session: https://claude.ai/code/session_01HGfEft5zNU6U3sPjFTUYvP"
```

---

## Task 4 — Paging a long quote at 12 (A5, A6, A9's page count)

Fixture: `words(20)` is 20 × "word", 99 bytes. At rung 2 it wraps 4 words a line → 5 quote lines;
at rungs 0 and 1, 3 a line → 7 lines (250 and 202 px with a one-line reference), so any box under
202 px pages at rung 2. Lines are numbered quote 0–4, reference 5 (or 5–6).

**4.1** Append to the end of `test/ui_layout/PreachingShowLayoutTest.cpp`:

```cpp
namespace {

std::string words(const int count) {
  std::string text;
  for (int i = 0; i < count; ++i) {
    if (i > 0) text.push_back(' ');
    text.append("word");
  }
  return text;
}

std::string withoutBreaks(const std::string_view text) {
  std::string kept;
  for (const char c : text) {
    if (c != ' ' && c != '\t' && c != '\n' && c != '\r') kept.push_back(c);
  }
  return kept;
}

std::string joined(const std::string_view text, const std::vector<Span>& spans) {
  std::string out;
  for (const Span span : spans) out.append(text.substr(span.begin, span.length));
  return out;
}

std::vector<uint16_t> pages(const show::Layout& layout) { return layout.pageFirstLine; }

}  // namespace

TEST(PreachingShowPaging, ALongQuotePagesAtTheSmallestSize) {
  const std::string quote = words(20);
  const auto layout = layOut(quote, "Ref 1:1", 60);
  EXPECT_EQ(layout.rung, show::PAGING_RUNG);
  EXPECT_EQ(layout.quote.size(), 5u);
  EXPECT_EQ(pages(layout), (std::vector<uint16_t>{0, 3, 4}));
  EXPECT_GT(layout.pageCount(), 1);
  const auto metrics = showMetrics(60);
  EXPECT_EQ(layout.pageHeight(0, metrics), 60);
  EXPECT_EQ(layout.pageHeight(1, metrics), 20);
  EXPECT_EQ(layout.pageHeight(2, metrics), 50);
}

TEST(PreachingShowPaging, NothingIsCut) {
  const std::string quote = words(20);
  const std::string reference = "Ref 1:1";
  const auto layout = layOut(quote, reference, 60);
  EXPECT_EQ(withoutBreaks(joined(quote, layout.quote)), withoutBreaks(quote));
  EXPECT_EQ(withoutBreaks(joined(reference, layout.reference)), withoutBreaks(reference));
  const auto metrics = showMetrics(60);
  ASSERT_GE(layout.pageCount(), 1);
  EXPECT_EQ(layout.pageFirstLine[0], 0);
  for (int page = 0; page < layout.pageCount(); ++page) {
    EXPECT_LT(layout.pageFirstLine[static_cast<size_t>(page)], layout.pageEnd(page));
    EXPECT_LE(layout.pageHeight(page, metrics), 60);
  }
  EXPECT_EQ(layout.pageEnd(layout.pageCount() - 1), layout.lineCount());
}

TEST(PreachingShowPaging, TheReferenceNeverStartsAPageAlone) {
  // Five quote lines fill 100 px exactly; the last one moves to the reference's page.
  const auto layout = layOut(words(20), "Ref 1:1", 100);
  EXPECT_EQ(pages(layout), (std::vector<uint16_t>{0, 4}));
}

TEST(PreachingShowPaging, AWrappedReferenceIsNeverSplit) {
  // The reference wraps to two lines (5 and 6). Greedy filling from line 3 would split it.
  const std::string reference = "Ref 1:1 and 2:2 and 3:3";
  const auto layout = layOut(words(20), reference, 70);
  ASSERT_EQ(layout.reference.size(), 2u);
  EXPECT_EQ(pages(layout), (std::vector<uint16_t>{0, 3, 4}));
  const int lastPageFirst = layout.pageFirstLine.back();
  EXPECT_EQ(lastPageFirst, static_cast<int>(layout.quote.size()) - 1);
  EXPECT_FALSE(layout.isReferenceLine(lastPageFirst));
}

TEST(PreachingShowPaging, ABoxSmallerThanALineStillAdvancesOneLineAPage) {
  const auto layout = layOut(words(20), "Ref 1:1", 10);
  EXPECT_EQ(pages(layout), (std::vector<uint16_t>{0, 1, 2, 3, 4, 5}));
}

TEST(PreachingShowPaging, AShortQuoteIsOnePage) {
  EXPECT_EQ(layOut("one two three", "Jn 3:16", 100).pageCount(), 1);
}
```

Run the host commands. Expected: `ALongQuotePagesAtTheSmallestSize`,
`TheReferenceNeverStartsAPageAlone`, `AWrappedReferenceIsNeverSplit` and
`ABoxSmallerThanALineStillAdvancesOneLineAPage` fail (one page where several are expected);
`NothingIsCut` fails on `pageHeight ≤ 60`.

**4.2** In `src/activities/preaching/PreachingShowLayout.cpp`, replace

```cpp
void paginate(Layout& out, const Metrics&) { out.pageFirstLine.push_back(0); }
```

with:

```cpp
// Every page takes at least one line. The quote's last line and the whole reference stay on one
// page whenever that block fits the box, so a reference neither starts a page alone nor splits.
void paginate(Layout& out, const Metrics& metrics) {
  out.pageFirstLine.push_back(0);
  const int lines = out.lineCount();
  const int quoteLines = static_cast<int>(out.quote.size());
  const int tailStart = quoteLines > 0 ? quoteLines - 1 : 0;
  const bool keepTail = !out.reference.empty() && linesHeight(out, metrics, tailStart, lines) <= metrics.height;
  const int pageLimit = keepTail ? tailStart : lines;
  int first = 0;
  while (linesHeight(out, metrics, first, lines) > metrics.height) {
    int end = first + 1;
    while (end < pageLimit && linesHeight(out, metrics, first, end + 1) <= metrics.height) ++end;
    if (end >= lines) return;
    first = end;
    out.pageFirstLine.push_back(static_cast<uint16_t>(first));
  }
}
```

Run the host commands. Expected: 24 `PreachingShow*` tests pass. Run
`./build/test/ui_layout/PreachingCardsTest` unfiltered: all pass.

**4.3** Commit:

```bash
git add src/activities/preaching/PreachingShowLayout.cpp test/ui_layout/PreachingShowLayoutTest.cpp
git commit -m "feat(preaching): page a long show-mode quote at 12" -m "Claude-Session: https://claude.ai/code/session_01HGfEft5zNU6U3sPjFTUYvP"
```

---

## Task 5 — `PreachingShowActivity` (A2, A3, A7–A12, A14, A16, A17)

No host test: the screen is renderer and input glue over the tested module (as
`PreachingTopicActivity` is over `PreachingCardLayout`). It is verified by the firmware build here
and on the device at the end of the batch.

**5.1** Create `src/activities/preaching/PreachingShowActivity.h`:

```cpp
#pragma once

#include "PreachingShowLayout.h"
#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

// One Verse card full screen, as large as it fits, for the householder. Pushed over the topic
// screen with startActivityForResult; it views the topic's cards and returns, as a PreachingResult,
// the Verse ordinal it ended on. Centred as StudySleepScreen sets its passage.
class PreachingShowActivity final : public Activity {
 public:
  // `deck` and `title` view the topic beneath, which outlives this screen.
  PreachingShowActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, preaching::show::Deck deck,
                        const char* title, int startVerse);

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  struct Geometry {
    int boxTop = 0;
    int footerY = 0;
    int columnWidth = 0;
  };

  void computeGeometry();
  void layOutVerse();
  void stepBy(int direction);
  void finishWithResult();
  void drawPage() const;
  void drawFooter() const;

  preaching::show::Deck deck;
  const char* title;
  int startVerse;
  int verses = 0;
  preaching::show::Cursor cursor;
  preaching::show::Metrics metrics;
  preaching::show::Layout layout;
  Geometry geometry;
  ButtonNavigator buttonNavigator;
  int pagesUntilFullRefresh = 0;
};
```

**5.2** Create `src/activities/preaching/PreachingShowActivity.cpp`:

```cpp
#include "PreachingShowActivity.h"

#include <EpdFontFamily.h>
#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>

#include <algorithm>
#include <cstdio>
#include <string>
#include <string_view>

#include "MappedInputManager.h"
#include "activities/reader/ReaderUtils.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace show = preaching::show;

namespace {

constexpr const char* MODULE = "PREACH";

constexpr int RUNG_FONT_IDS[show::RUNG_COUNT] = {NOTOSERIF_18_FONT_ID, NOTOSERIF_14_FONT_ID, NOTOSERIF_12_FONT_ID};
constexpr int FOOTER_FONT_ID = UI_10_FONT_ID;

constexpr size_t QUOTE_SPANS_RESERVED = 64;
constexpr size_t REFERENCE_SPANS_RESERVED = 4;
constexpr size_t PAGES_RESERVED = 8;

constexpr preaching::CardInput NO_CARD{};

constexpr EpdFontFamily::Style styleOf(const preaching::Face face) {
  return face == preaching::Face::Reference ? EpdFontFamily::BOLD : EpdFontFamily::ITALIC;
}

int measureAtRung(const void* ctx, const uint8_t rung, const preaching::Face face, const char* text) {
  return static_cast<const GfxRenderer*>(ctx)->getTextWidth(RUNG_FONT_IDS[rung], text, styleOf(face));
}

void copyLine(const std::string_view source, const preaching::Span span, char (&out)[preaching::LINE_BUFFER_BYTES]) {
  const size_t length = std::min<size_t>(span.length, sizeof(out) - 1);
  // Tabs and newlines in a passage's text are break points to the layout and spaces on screen.
  for (size_t i = 0; i < length; ++i) {
    const char c = source[span.begin + i];
    out[i] = static_cast<unsigned char>(c) < 0x20 ? ' ' : c;
  }
  out[length] = '\0';
}

}  // namespace

PreachingShowActivity::PreachingShowActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                             const show::Deck deck, const char* title, const int startVerse)
    : Activity("PreachingShow", renderer, mappedInput), deck(deck), title(title), startVerse(startVerse) {}

void PreachingShowActivity::onEnter() {
  Activity::onEnter();
  verses = show::verseCount(deck);
  if (verses == 0) {
    LOG_ERR(MODULE, "Show mode opened on a topic with no verse");
    finishWithResult();
    return;
  }
  computeGeometry();
  layout.quote.reserve(QUOTE_SPANS_RESERVED);
  layout.reference.reserve(REFERENCE_SPANS_RESERVED);
  layout.pageFirstLine.reserve(PAGES_RESERVED);
  {
    RenderLock lock(*this);
    cursor = {std::clamp(startVerse, 0, verses - 1), 0};
    layOutVerse();
  }
  requestUpdate();
}

void PreachingShowActivity::computeGeometry() {
  int viewTop = 0;
  int viewRight = 0;
  int viewBottom = 0;
  int viewLeft = 0;
  renderer.getOrientedViewableTRBL(&viewTop, &viewRight, &viewBottom, &viewLeft);
  const auto& theme = UITheme::getInstance().getMetrics();

  geometry.footerY =
      renderer.getScreenHeight() - viewBottom - theme.verticalSpacing - renderer.getLineHeight(FOOTER_FONT_ID);
  geometry.boxTop = viewTop + theme.verticalSpacing;
  geometry.columnWidth = renderer.getScreenWidth() - viewLeft - viewRight - 2 * theme.contentSidePadding;

  metrics.width = geometry.columnWidth;
  metrics.height = geometry.footerY - theme.verticalSpacing - geometry.boxTop;
  metrics.referenceGap = theme.verticalSpacing;
  for (uint8_t rung = 0; rung < show::RUNG_COUNT; ++rung) {
    metrics.lineHeight[rung] = renderer.getLineHeight(RUNG_FONT_IDS[rung]);
  }
}

void PreachingShowActivity::layOutVerse() {
  const preaching::CardInput* card = show::verseCard(deck, cursor.verse);
  show::layOut(card ? *card : NO_CARD, metrics, &measureAtRung, &renderer, layout);
}

void PreachingShowActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back) ||
      ReaderUtils::isTouchMenuTap(renderer, mappedInput)) {
    finishWithResult();
    return;
  }

  const auto touch = ReaderUtils::detectTouchPageTurn(renderer, mappedInput);
  if (touch.next) {
    stepBy(1);
    return;
  }
  if (touch.prev) {
    stepBy(-1);
    return;
  }

  buttonNavigator.onNextRelease([this] { stepBy(1); });
  buttonNavigator.onPreviousRelease([this] { stepBy(-1); });
}

void PreachingShowActivity::stepBy(const int direction) {
  show::Cursor next = cursor;
  if (!show::step(next, layout.pageCount(), verses, direction)) return;
  {
    RenderLock lock(*this);
    const bool verseChanged = next.verse != cursor.verse;
    cursor = next;
    if (verseChanged) layOutVerse();
  }
  requestUpdate();
}

void PreachingShowActivity::finishWithResult() {
  PreachingOrigin at;
  at.verse = static_cast<uint16_t>(cursor.verse);
  setResult(PreachingResult{at});
  finish();
}

void PreachingShowActivity::render(RenderLock&&) {
  renderer.clearScreen();
  drawPage();
  drawFooter();
  ReaderUtils::displayWithRefreshCycle(renderer, pagesUntilFullRefresh);
}

void PreachingShowActivity::drawPage() const {
  const preaching::CardInput* card = show::verseCard(deck, cursor.verse);
  if (!card || cursor.page >= layout.pageCount()) return;

  const int fontId = RUNG_FONT_IDS[layout.rung];
  const int lineHeight = metrics.lineHeight[layout.rung];
  const int first = layout.pageFirstLine[static_cast<size_t>(cursor.page)];
  const int end = layout.pageEnd(cursor.page);
  const int quoteLines = static_cast<int>(layout.quote.size());
  int y = geometry.boxTop + std::max(0, (metrics.height - layout.pageHeight(cursor.page, metrics)) / 2);

  char text[preaching::LINE_BUFFER_BYTES];
  for (int line = first; line < end; ++line) {
    const bool reference = layout.isReferenceLine(line);
    if (reference && line == quoteLines && line > first) y += metrics.referenceGap;
    if (reference) {
      copyLine(card->reference, layout.reference[static_cast<size_t>(line - quoteLines)], text);
    } else {
      copyLine(card->body, layout.quote[static_cast<size_t>(line)], text);
    }
    renderer.drawCenteredText(fontId, y, text, true,
                              styleOf(reference ? preaching::Face::Reference : preaching::Face::Quote));
    y += lineHeight;
  }
}

void PreachingShowActivity::drawFooter() const {
  char footer[96];
  const int written =
      snprintf(footer, sizeof(footer), tr(STR_PREACHING_SHOW_POSITION), cursor.verse + 1, verses, title ? title : "");
  if (layout.pageCount() > 1 && written > 0 && static_cast<size_t>(written) < sizeof(footer)) {
    snprintf(footer + written, sizeof(footer) - static_cast<size_t>(written), " · %d/%d", cursor.page + 1,
             layout.pageCount());
  }
  if (renderer.getTextWidth(FOOTER_FONT_ID, footer) <= geometry.columnWidth) {
    renderer.drawCenteredText(FOOTER_FONT_ID, geometry.footerY, footer);
    return;
  }
  const std::string shortened = renderer.truncatedText(FOOTER_FONT_ID, footer, geometry.columnWidth);
  renderer.drawCenteredText(FOOTER_FONT_ID, geometry.footerY, shortened.c_str());
}
```

Notes for the implementer, so nothing above looks arbitrary:
- `PreachingOrigin` / `PreachingResult` come in through `activities/Activity.h` →
  `ActivityResult.h`. Only `at.verse` is set (spec A16).
- `pagesUntilFullRefresh = 0` makes the first frame a `HALF_REFRESH` (spec A14).
- Every `cursor` write is under `RenderLock` (spec A17, review 0 m2). `layout.pageCount()` is read
  outside the lock in `stepBy` because only the loop task writes `layout`.
- The `" · %d/%d"` page suffix is digits and punctuation, as the topic screen's own `"%d/%d"`
  (spec A9) — not a `tr()` string.

**5.3** Build the firmware:

```bash
~/.platformio/penv/bin/pio run -e x4pro > .pio/show-build.log 2>&1; echo "exit $?"
grep -n "PreachingShow" .pio/show-build.log | grep -i "warning\|error"
```

Expected: `exit 0`, and no warning or error line naming a `PreachingShow` file. (The screen is not
reachable yet; Task 6 wires it.) If the compiler rejects `constexpr preaching::CardInput NO_CARD{}`,
change `constexpr` to `const` on that line only.

**5.4** Commit:

```bash
git add src/activities/preaching/PreachingShowActivity.h src/activities/preaching/PreachingShowActivity.cpp
git commit -m "feat(preaching): show one verse full screen for the householder" -m "Claude-Session: https://claude.ai/code/session_01HGfEft5zNU6U3sPjFTUYvP"
```

---

## Task 6 — Open show mode from a quote tap (A15, A16, A18)

**6.1** In `src/activities/preaching/PreachingTopicActivity.h`, replace

```cpp
  // Show mode at `verse` (T4).
  void openShow(int verse);
```

with:

```cpp
  // Show mode at `verse`; the Verse card it returns is selected on the way back.
  void openShow(int verse);
  void onShowFinished(const ActivityResult& result);
```

**6.2** In `src/activities/preaching/PreachingTopicActivity.cpp`:

Add to the quoted includes, keeping them sorted (after `#include "MappedInputManager.h"`):

```cpp
#include "PreachingShowActivity.h"
```

and add `#include <variant>` to the `<...>` standard-library block (after `#include <utility>`).

Replace

```cpp
void PreachingTopicActivity::openShow(const int verse) {
  LOG_INF(MODULE, "Show mode is not built yet (verse %d)", verse);
}
```

with:

```cpp
void PreachingTopicActivity::openShow(const int verse) {
  if (verse < 0 || verse >= topic->verseCount()) return;
  auto show = makeUniqueNoThrow<PreachingShowActivity>(
      renderer, mappedInput, preaching::show::Deck{topic->cards.data(), topic->cards.size()}, topic->header.c_str(),
      verse);
  if (!show) {
    LOG_ERR(MODULE, "OOM: show mode");
    reportFailure(tr(STR_PREACHING_OPEN_FAILED));
    return;
  }
  app.clearTapFlash();
  startActivityForResult(std::move(show), [this](const ActivityResult& result) { onShowFinished(result); });
}

void PreachingTopicActivity::onShowFinished(const ActivityResult& result) {
  // Coming back from full-screen large type: a fast refresh would leave it ghosted.
  halfRefreshPending = true;
  const auto* returned = std::get_if<PreachingResult>(&result.data);
  if (!returned || !layout) return;
  RenderLock lock(*this);
  cursor = preaching::selectVerse(*layout, returned->at.verse);
}
```

Why each line is there (spec): the range check guards a stale hit rect; `clearTapFlash` keeps the
flashed quote from being drawn on return (A18); the handler runs on the loop task with the render
lock released by the manager, so it takes `RenderLock` itself (A17); `selectVerse` turns to the
card's page and falls back to the first Verse card when out of range; the manager re-renders the
topic after the handler returns.

**6.3** Build the firmware:

```bash
~/.platformio/penv/bin/pio run -e x4pro > .pio/show-build.log 2>&1; echo "exit $?"
grep -n "Preaching" .pio/show-build.log | grep -i "warning\|error"
```

Expected: `exit 0`, no warning or error naming a `Preaching` file.

**6.4** Commit:

```bash
git add src/activities/preaching/PreachingTopicActivity.h src/activities/preaching/PreachingTopicActivity.cpp
git commit -m "feat(preaching): open show mode from a quote tap" -m "Claude-Session: https://claude.ai/code/session_01HGfEft5zNU6U3sPjFTUYvP"
```

---

## Task 7 — Format and verify everything

**7.1** Format the whole tree (the new files are already tracked, so the wrapper sees them):

```bash
./bin/clang-format-fix
git status --short
```

If it changed files, run the host commands once more (they must still pass), then:

```bash
git add src/activities/preaching/PreachingShowLayout.h src/activities/preaching/PreachingShowLayout.cpp \
  src/activities/preaching/PreachingShowActivity.h src/activities/preaching/PreachingShowActivity.cpp \
  src/activities/preaching/PreachingTopicActivity.h src/activities/preaching/PreachingTopicActivity.cpp \
  test/ui_layout/PreachingShowLayoutTest.cpp
git commit -m "style(preaching): clang-format show mode" -m "Claude-Session: https://claude.ai/code/session_01HGfEft5zNU6U3sPjFTUYvP"
```

If it changed a file outside that list, do not commit it; report it.

**7.2** Full verification:

```bash
cmake --build build/test --target PreachingCardsTest && ./build/test/ui_layout/PreachingCardsTest
~/.platformio/penv/bin/pio run -e x4pro > .pio/final-build.log 2>&1; echo "exit $?"
grep -c "warning:" .pio/baseline-build.log .pio/final-build.log
./bin/clang-format-fix && git status --short
```

Expected: every `PreachingCardsTest` test passes (the 24 `PreachingShow*` among them); `exit 0`;
the two warning counts are equal; `git status --short` prints nothing.

Report back: the test summary line, the build exit code, both warning counts, and the commit list
(`git log --oneline 61bb11f9..HEAD`).

## Device check (the owner's, at the end of the batch — not part of this plan's run)

Tap a quote → that verse at Noto Serif 18, legible at arm's length (D6); a long tag passage pages
at 12 with its reference on the last page and the footer reading `… · 1/3` → `2/3` → `3/3`; Left/Right
and the side thirds step; the centre tap and the left-edge swipe return with that card selected and
its page shown; free heap equal across topic → show → topic (`LOG_DBG("MEM", …)` on serial).
