# Plan: Predicación's screens and the reader entry contract (berean-o-16, T3)

Spec: `docs/superpowers/specs/2026-10-15-berean-o-16-design.md` (cleared review 0). Research:
`docs/superpowers/research/2026-10-15-berean-o-16-research.md`. Branch `feature/preaching-screens`,
cut from `batch/20261015-predicaci-n-193x`.

Every line of code below was built and run before this plan was written: the host suites passed
(`PreachingCardsTest` 21 tests, `ReaderEntryIntentTest` 11, `HomeLayoutTest` 42), `pio run -e x4pro`
succeeded with no warning from `src/`, and the code is shown as `./bin/clang-format-fix` left it. A replay of
this plan's file steps onto the base reproduced that tree byte for byte. Copy it
exactly; if a step's result differs from what the step says, stop and find out why before going on.

## Rules for whoever carries this out

- Work only in this worktree, on `feature/preaching-screens`. Commit after each task as shown.
  **Never push, open a PR or run `hpipe submit`.**
- Before editing a file, re-read it: a sibling may have changed it since this plan was written. If a
  patch below does not apply, read the file, apply the same change by hand, and say so.
- Code is self-documenting: meaningful names, no comments that restate the code or narrate history.
  A comment is only for a non-obvious *why* (a constraint, a gotcha, a workaround). The code below
  already follows this; keep it that way in any fix.
- Follow `CLAUDE.md`: `makeUniqueNoThrow` for fallible allocations with a `LOG_ERR`, no
  `std::function` of your own, `tr()` for user text, locals under 256 B, `.reserve()` before
  `push_back` loops.
- Do not name an unlanded `STR_` key in a comment: `scripts/gen_i18n.py` scans comments.
- `pio` is not on PATH: use `~/.platformio/penv/bin/pio`. Never run two `pio run` at once.
- Host tests: `cmake -S test -B build/test`, then build one target and run it from
  `build/test/<suite>/<Target>`. The first configure fetches googletest.

## Files

FILES: src/activities/preaching/
FILES: src/activities/reader/ReaderEntryIntent.h, src/activities/reader/EpubReaderActivity.cpp
FILES: src/activities/ActivityResult.h, src/activities/ActivityManager.h, src/activities/ActivityManager.cpp
FILES: lib/I18n/translations/english.yaml, lib/I18n/translations/spanish.yaml
FILES: test/ui_layout/ReaderEntryIntentTest.cpp, test/ui_layout/PreachingCardLayoutTest.cpp, test/ui_layout/PreachingCardNavTest.cpp, test/ui_layout/CMakeLists.txt

`src/activities/preaching/` holds: `PreachingOrigin.h`, `PreachingCardLayout.{h,cpp}`,
`PreachingCardNav.h`, `PreachingSession.{h,cpp}`, `PreachingContent.{h,cpp}`,
`PreachingTopicActivity.{h,cpp}`, `PreachingActivity.{h,cpp}`. The generated i18n headers
(`lib/I18n/I18nKeys.h`, `I18nStrings.h`, `I18nStrings.cpp`) are gitignored and never committed.

## Task 0 — Pre-flight (2 min)

```bash
git branch --show-current          # feature/preaching-screens
git status --short                 # clean
ls .venv/bin/clang-format && git submodule status | head -1   # else run ./bin/bootstrap
git fetch -q origin && git log --oneline HEAD..origin/batch/20261015-predicaci-n-193x
```

If the last command prints commits, the batch moved: stop and report it rather than planning around it.
Confirm that `src/activities/preaching/` does not exist yet and that
`grep -n "PREACHING" lib/I18n/translations/english.yaml` prints nothing.

## Task 1 — `PreachingOrigin` and the `ReaderEntryIntent` contract

### Step 1.1 — failing tests (3 min)

Apply this patch to `test/ui_layout/ReaderEntryIntentTest.cpp` (run from the worktree root):

```bash
git apply <<'EOF_PATCH'
diff --git a/test/ui_layout/ReaderEntryIntentTest.cpp b/test/ui_layout/ReaderEntryIntentTest.cpp
index 8e0ad789..18f33eef 100644
--- a/test/ui_layout/ReaderEntryIntentTest.cpp
+++ b/test/ui_layout/ReaderEntryIntentTest.cpp
@@ -1,5 +1,8 @@
 #include <gtest/gtest.h>
 
+#include <string>
+#include <type_traits>
+
 #include "activities/reader/ReaderEntryIntent.h"
 
 using Kind = ReaderEntryIntent::Kind;
@@ -41,3 +44,43 @@ TEST(ReaderEntryIntent, OpenAtCarriesThePlacesUnitAndHint) {
   EXPECT_EQ(intent.unit, place.unit);
   EXPECT_EQ(intent.spineHint, 1201);
 }
+
+static_assert(std::is_trivially_copyable_v<ReaderEntryIntent>, "the reader copies the intent by value");
+
+TEST(ReaderEntryIntent, OpenHrefIsBibleOnly) {
+  EXPECT_EQ(ReaderEntryIntent::route(Kind::OpenHref, true), Route::Href);
+  EXPECT_EQ(ReaderEntryIntent::route(Kind::OpenHref, false), Route::None);
+}
+
+TEST(ReaderEntryIntent, OpenHrefCarriesTheLinkAndTheOrigin) {
+  const std::string link(ReaderEntryIntent::HREF_BYTES - 1, 'x');
+  const PreachingOrigin origin = PreachingOrigin::ofQuestion(8, 3);
+  ReaderEntryIntent intent;
+  ASSERT_TRUE(ReaderEntryIntent::openHref(link, origin, intent));
+  EXPECT_EQ(intent.kind, Kind::OpenHref);
+  EXPECT_EQ(std::string(intent.href), link);
+  EXPECT_EQ(intent.origin.kind, PreachingOrigin::Kind::Question);
+  EXPECT_EQ(intent.origin.question, 8);
+  EXPECT_EQ(intent.origin.verse, 3);
+}
+
+TEST(ReaderEntryIntent, OpenHrefRefusesAnOverLongOrEmptyLink) {
+  ReaderEntryIntent intent = ReaderEntryIntent::of(Kind::GoTo);
+  EXPECT_FALSE(ReaderEntryIntent::openHref(std::string(ReaderEntryIntent::HREF_BYTES, 'x'), {}, intent));
+  EXPECT_FALSE(ReaderEntryIntent::openHref("", {}, intent));
+  EXPECT_EQ(intent.kind, Kind::GoTo);
+  EXPECT_EQ(intent.href[0], '\0');
+}
+
+TEST(ReaderEntryIntent, AnIntentHasNoOriginUntilGivenOne) {
+  EXPECT_FALSE(ReaderEntryIntent{}.origin.isSet());
+  EXPECT_FALSE(ReaderEntryIntent::of(Kind::OpenAt).origin.isSet());
+}
+
+TEST(ReaderEntryIntent, OriginsKeepTheirTopicAndVerse) {
+  const PreachingOrigin tag = PreachingOrigin::ofTag(31, 2);
+  EXPECT_EQ(tag.kind, PreachingOrigin::Kind::Tag);
+  EXPECT_EQ(tag.tag, 31);
+  EXPECT_EQ(tag.verse, 2);
+  EXPECT_EQ(PreachingOrigin::ofQuestion(5, -1).verse, 0);
+}
EOF_PATCH
```

Run it and watch it fail to compile (`PreachingOrigin`, `Kind::OpenHref`, `openHref` do not exist):

```bash
cmake -S test -B build/test >/dev/null && cmake --build build/test --target ReaderEntryIntentTest -j8
```

### Step 1.2 — `PreachingOrigin` (2 min)

```bash
mkdir -p src/activities/preaching
cat > src/activities/preaching/PreachingOrigin.h <<'EOF_FILE'
#pragma once

#include <cstdint>

// Where in Predicación a screen, or the reader, came from: a question or a tag topic, and the
// Verse card within it. RAM only, and free of Arduino and StudyStore so test/ui_layout builds it.
struct PreachingOrigin {
  enum class Kind : uint8_t { None, Question, Tag };

  Kind kind = Kind::None;
  uint8_t question = 0;  // 1..IntroSection::QUESTION_COUNT when kind == Question
  uint16_t tag = 0;      // the raw study::TagId when kind == Tag
  uint16_t verse = 0;    // ordinal among the topic's Verse cards

  static constexpr PreachingOrigin ofQuestion(const int question, const int verse) {
    PreachingOrigin origin;
    origin.kind = Kind::Question;
    origin.question = static_cast<uint8_t>(question);
    origin.verse = static_cast<uint16_t>(verse < 0 ? 0 : verse);
    return origin;
  }

  static constexpr PreachingOrigin ofTag(const uint16_t tag, const int verse) {
    PreachingOrigin origin;
    origin.kind = Kind::Tag;
    origin.tag = tag;
    origin.verse = static_cast<uint16_t>(verse < 0 ? 0 : verse);
    return origin;
  }

  constexpr bool isSet() const { return kind != Kind::None; }
};
EOF_FILE
```

### Step 1.3 — `Kind::OpenHref`, `Route::Href`, `href`, `origin`, `openHref()` (3 min)

Apply this patch to `src/activities/reader/ReaderEntryIntent.h` (run from the worktree root):

```bash
git apply <<'EOF_PATCH'
diff --git a/src/activities/reader/ReaderEntryIntent.h b/src/activities/reader/ReaderEntryIntent.h
index 7bc8a8ae..7d172164 100644
--- a/src/activities/reader/ReaderEntryIntent.h
+++ b/src/activities/reader/ReaderEntryIntent.h
@@ -2,21 +2,34 @@
 
 #include <StudyStore/Unit.h>
 
+#include <cstddef>
 #include <cstdint>
+#include <cstring>
+#include <string_view>
 
 #include "Place.h"
+#include "activities/preaching/PreachingOrigin.h"
 
-// A one-shot instruction to the reader, carried by ActivityManager::goToReader and consumed once
-// the book has loaded: open at a place, or straight into the chapter grid, verse search or tags.
-// RAM only. Free of Arduino so test/ui_layout can exercise route().
+// An instruction to the reader, carried by ActivityManager::goToReader: open at a place or an href,
+// or straight into the chapter grid, verse search or tags. `kind` and its fields are consumed once
+// the book has loaded. `origin` is not: the reader keeps it for its whole life, so it must be copied
+// out before onBookLoaded clears the intent. RAM only. Free of Arduino so test/ui_layout can
+// exercise route().
 struct ReaderEntryIntent {
-  enum class Kind : uint8_t { None, OpenAt, GoTo, Search, Tags };
-  enum class Route : uint8_t { None, Locate, ChapterGrid, TocList, Search, Highlights };
+  enum class Kind : uint8_t { None, OpenAt, GoTo, Search, Tags, OpenHref };
+  enum class Route : uint8_t { None, Locate, ChapterGrid, TocList, Search, Highlights, Href };
+
+  // The longest href in the NWT's preaching section is 45 bytes.
+  static constexpr size_t HREF_BYTES = 64;
 
   Kind kind = Kind::None;
   // OpenAt only. The unit, not an offset: the reader resolves it in whichever edition is open.
   study::Unit unit{};
   uint16_t spineHint = 0;
+  // OpenHref only: a link as the publication's markup has it, NUL-terminated.
+  char href[HREF_BYTES] = {};
+  // Where in Predicación the reader was opened from, for its way back.
+  PreachingOrigin origin{};
 
   static ReaderEntryIntent of(const Kind kind) {
     ReaderEntryIntent intent;
@@ -32,7 +45,19 @@ struct ReaderEntryIntent {
     return intent;
   }
 
-  // Places, verse search and tags exist only in the Bible; outside it the grid is the TOC list.
+  // False, leaving `out` untouched, for an empty href or one too long for HREF_BYTES with its NUL.
+  static bool openHref(const std::string_view link, const PreachingOrigin& from, ReaderEntryIntent& out) {
+    if (link.empty() || link.size() >= HREF_BYTES) return false;
+    ReaderEntryIntent intent;
+    intent.kind = Kind::OpenHref;
+    std::memcpy(intent.href, link.data(), link.size());
+    intent.href[link.size()] = '\0';
+    intent.origin = from;
+    out = intent;
+    return true;
+  }
+
+  // Places, hrefs, verse search and tags exist only in the Bible; outside it the grid is the TOC list.
   static constexpr Route route(const Kind kind, const bool isBible) {
     switch (kind) {
       case Kind::OpenAt:
@@ -43,6 +68,8 @@ struct ReaderEntryIntent {
         return isBible ? Route::Search : Route::None;
       case Kind::Tags:
         return isBible ? Route::Highlights : Route::None;
+      case Kind::OpenHref:
+        return isBible ? Route::Href : Route::None;
       case Kind::None:
         return Route::None;
     }
EOF_PATCH
```

Run both suites that compile this header (`HomeLayoutTest` reaches it through `HomeTargets.h`):

```bash
cmake -S test -B build/test >/dev/null && cmake --build build/test --target ReaderEntryIntentTest HomeLayoutTest -j8
build/test/ui_layout/ReaderEntryIntentTest   # 11 tests pass
build/test/home_layout/HomeLayoutTest        # 42 tests pass
```

### Step 1.4 — the reader's placeholder case (2 min)

`EpubReaderActivity::onBookLoaded` switches over `Route` with no `default`, so `-Wall` warns about the
new `Route::Href` until it has a case. T7 replaces this one with `navigateToHref`.

Apply this patch to `src/activities/reader/EpubReaderActivity.cpp` (run from the worktree root):

```bash
git apply <<'EOF_PATCH'
diff --git a/src/activities/reader/EpubReaderActivity.cpp b/src/activities/reader/EpubReaderActivity.cpp
index ca78bd53..a9e99b57 100644
--- a/src/activities/reader/EpubReaderActivity.cpp
+++ b/src/activities/reader/EpubReaderActivity.cpp
@@ -1129,6 +1129,9 @@ void EpubReaderActivity::onBookLoaded() {
     case ReaderEntryIntent::Route::Highlights:
       openHighlights(CancelTo::Page);
       return;
+    case ReaderEntryIntent::Route::Href:
+      LOG_INF("ERS", "Href entry intent not honoured yet; opening normally");
+      return;
   }
 }
 
EOF_PATCH
```

No host suite covers the reader; Task 5's `pio run` is the check (no `-Wswitch` warning).

```bash
git add src/activities/preaching/PreachingOrigin.h src/activities/reader/ReaderEntryIntent.h src/activities/reader/EpubReaderActivity.cpp test/ui_layout/ReaderEntryIntentTest.cpp
git commit -m "feat(preaching): add OpenHref and the preaching origin to the reader intent" -m "Claude-Session: https://claude.ai/code/session_01AQ3bc72oCQQ2JANTbFtX9Y"
```

## Task 2 — `PreachingCardLayout` (pure, host-tested)

### Step 2.1 — failing tests (4 min)

```bash
mkdir -p test/ui_layout
cat > test/ui_layout/PreachingCardLayoutTest.cpp <<'EOF_FILE'
#include <gtest/gtest.h>

#include <string>
#include <string_view>
#include <vector>

#include "activities/preaching/PreachingCardLayout.h"

using preaching::CardInput;
using preaching::CardKind;
using preaching::Face;
using preaching::Layout;
using preaching::Metrics;
using preaching::Span;

namespace {

constexpr int PX_PER_BYTE = 10;

int widthByBytes(const void*, Face, const char* text) {
  return static_cast<int>(std::string_view(text).size()) * PX_PER_BYTE;
}

std::vector<std::string> wrap(const std::string_view text, const int width) {
  std::vector<Span> spans;
  preaching::wrapText(text, Face::Body, width, &widthByBytes, nullptr, spans);
  std::vector<std::string> lines;
  for (const Span span : spans) lines.emplace_back(text.substr(span.begin, span.length));
  return lines;
}

bool isContinuationByte(const char c) { return (static_cast<unsigned char>(c) & 0xC0) == 0x80; }

void expectCodepointBoundaries(const std::string_view text, const std::vector<Span>& spans) {
  for (const Span span : spans) {
    EXPECT_FALSE(isContinuationByte(text[span.begin])) << "line starts mid-codepoint at " << span.begin;
    const size_t end = span.begin + span.length;
    if (end < text.size()) EXPECT_FALSE(isContinuationByte(text[end])) << "line ends mid-codepoint at " << end;
  }
}

// Every face 10 px tall, no padding or gaps, a column 100 px (10 bytes) wide.
Metrics plainMetrics(const int pageHeight) {
  Metrics metrics;
  metrics.width = 100;
  metrics.pageHeight = pageHeight;
  for (int& height : metrics.lineHeight) height = 10;
  return metrics;
}

Layout layOut(const std::vector<CardInput>& cards, const Metrics& metrics) {
  Layout layout;
  EXPECT_TRUE(preaching::layoutCards(cards.data(), cards.size(), metrics, &widthByBytes, nullptr, layout));
  return layout;
}

std::vector<uint16_t> cardsOnPage(const Layout& layout, const int page) {
  std::vector<uint16_t> cards;
  for (size_t s = layout.pageFirstSegment[static_cast<size_t>(page)]; s < layout.segmentsEnd(page); ++s) {
    cards.push_back(layout.segments[s].card);
  }
  return cards;
}

// Ten bytes per word: one line each at the 100-px column.
CardInput textOfLines(const CardKind kind, const char* body, const char* reference = "") {
  return CardInput{kind, body, reference};
}

}  // namespace

TEST(PreachingCardWrap, BreaksAtSpacesOnly) {
  EXPECT_EQ(wrap("aaa bbb ccc", 70), (std::vector<std::string>{"aaa bbb", "ccc"}));
}

TEST(PreachingCardWrap, NoBreakSpaceKeepsItsWordsTogether) {
  EXPECT_EQ(wrap("aa\xC2\xA0"
                 "bb cc",
                 60),
            (std::vector<std::string>{"aa\xC2\xA0"
                                      "bb",
                                      "cc"}));
}

TEST(PreachingCardWrap, OverWideWordBreaksAtACodepointBoundary) {
  const std::string_view text = "\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9";  // five e-acute, two bytes each
  std::vector<Span> spans;
  preaching::wrapText(text, Face::Body, 30, &widthByBytes, nullptr, spans);
  ASSERT_EQ(spans.size(), 5u);
  for (const Span span : spans) EXPECT_EQ(span.length, 2);
  expectCodepointBoundaries(text, spans);
}

TEST(PreachingCardWrap, NoLineReachesTheLineBuffer) {
  const std::string text(300, 'a');
  std::vector<Span> spans;
  preaching::wrapText(text, Face::Body, 1 << 20, &widthByBytes, nullptr, spans);
  std::string joined;
  for (const Span span : spans) {
    EXPECT_LT(span.length, preaching::LINE_BUFFER_BYTES);
    joined.append(text, span.begin, span.length);
  }
  EXPECT_EQ(joined, text);
}

TEST(PreachingCardWrap, NewlinesBreakLikeSpaces) {
  EXPECT_EQ(wrap("aaa\nbbb", 30), (std::vector<std::string>{"aaa", "bbb"}));
}

TEST(PreachingCardLayout, EmptyInputIsOneEmptyPage) {
  const Layout layout = layOut({}, plainMetrics(100));
  EXPECT_EQ(layout.pageCount(), 1);
  EXPECT_TRUE(layout.segments.empty());
}

TEST(PreachingCardLayout, CardsThatFitShareAPage) {
  const std::vector<CardInput> cards(5, textOfLines(CardKind::Text, "a"));
  const Layout layout = layOut(cards, plainMetrics(100));
  ASSERT_EQ(layout.pageCount(), 1);
  EXPECT_EQ(cardsOnPage(layout, 0), (std::vector<uint16_t>{0, 1, 2, 3, 4}));
}

TEST(PreachingCardLayout, ACardThatDoesNotFitStartsTheNextPage) {
  const std::vector<CardInput> cards(3, textOfLines(CardKind::Text, "aaaaaaaaaa bbbbbbbbbb"));
  const Layout layout = layOut(cards, plainMetrics(30));
  ASSERT_EQ(layout.pageCount(), 3);
  for (int page = 0; page < 3; ++page) {
    EXPECT_EQ(cardsOnPage(layout, page), (std::vector<uint16_t>{static_cast<uint16_t>(page)}));
  }
  EXPECT_TRUE(layout.segments[1].opensCard && layout.segments[1].closesCard);
}

TEST(PreachingCardLayout, ACardTallerThanAPageContinuesAndKeepsEveryLine) {
  const std::vector<CardInput> cards{textOfLines(CardKind::Text, "a b c d e f g")};
  Metrics metrics = plainMetrics(30);
  metrics.width = 10;
  const Layout layout = layOut(cards, metrics);
  ASSERT_EQ(layout.pageCount(), 3);
  ASSERT_EQ(layout.segments.size(), 3u);
  EXPECT_EQ(layout.segments[0].lineCount, 3);
  EXPECT_EQ(layout.segments[1].lineCount, 3);
  EXPECT_EQ(layout.segments[2].lineCount, 1);
  EXPECT_TRUE(layout.segments[0].opensCard);
  EXPECT_FALSE(layout.segments[0].closesCard);
  EXPECT_FALSE(layout.segments[1].opensCard);
  EXPECT_TRUE(layout.segments[2].closesCard);
  uint16_t nextLine = 0;
  for (const auto& segment : layout.segments) {
    EXPECT_EQ(segment.firstLine, nextLine);
    nextLine = static_cast<uint16_t>(segment.firstLine + segment.lineCount);
  }
  EXPECT_EQ(nextLine, layout.lines.size());
}

TEST(PreachingCardLayout, AHeadingNeverEndsAPage) {
  const std::vector<CardInput> cards{
      textOfLines(CardKind::Text, "aaaaaaaaaa bbbbbbbbbb"),
      textOfLines(CardKind::Heading, "HEADING"),
      textOfLines(CardKind::Text, "aaaaaaaaaa bbbbbbbbbb"),
  };
  const Layout layout = layOut(cards, plainMetrics(30));
  ASSERT_EQ(layout.pageCount(), 2);
  EXPECT_EQ(cardsOnPage(layout, 0), (std::vector<uint16_t>{0}));
  EXPECT_EQ(cardsOnPage(layout, 1), (std::vector<uint16_t>{1, 2}));
}

TEST(PreachingCardLayout, AReferenceNeverStartsAPageAlone) {
  // Four quote lines fill a 40-px page; the reference must not open the next page alone, so the
  // last quote line moves with it.
  const std::vector<CardInput> cards{textOfLines(CardKind::Verse, "aaaaaaaaaa bbbbbbbbbb cccccccccc dddddddddd", "R")};
  const Layout layout = layOut(cards, plainMetrics(40));
  ASSERT_EQ(layout.segments.size(), 2u);
  EXPECT_EQ(layout.segments[0].lineCount, 3);
  EXPECT_FALSE(layout.lines[layout.segments[1].firstLine].reference);
  EXPECT_EQ(layout.segments[1].lineCount, 2);
}

TEST(PreachingCardLayout, AHeadingBeforeAnOverTallCardSharesItsPage) {
  const std::vector<CardInput> cards{
      textOfLines(CardKind::Text, "a"),
      textOfLines(CardKind::Heading, "H"),
      textOfLines(CardKind::Verse, "a b c d e f", "R"),
  };
  Metrics metrics = plainMetrics(40);
  metrics.width = 10;
  const Layout layout = layOut(cards, metrics);
  ASSERT_GE(layout.pageCount(), 3);
  EXPECT_EQ(cardsOnPage(layout, 0), (std::vector<uint16_t>{0}));
  const auto secondPage = cardsOnPage(layout, 1);
  ASSERT_EQ(secondPage.size(), 2u);
  EXPECT_EQ(secondPage[0], 1);
  EXPECT_EQ(secondPage[1], 2);
  size_t verseLines = 0;
  for (const auto& segment : layout.segments) {
    if (segment.card == 2) verseLines += segment.lineCount;
  }
  EXPECT_EQ(verseLines, 7u);
}

TEST(PreachingCardLayout, BoxedCardsCarryTheirPadding) {
  const std::vector<CardInput> cards{textOfLines(CardKind::Verse, "a", "R"), textOfLines(CardKind::Text, "b")};
  Metrics metrics = plainMetrics(200);
  metrics.cardPadding = 4;
  metrics.cardGap = 6;
  const Layout layout = layOut(cards, metrics);
  ASSERT_EQ(layout.segments.size(), 2u);
  EXPECT_EQ(layout.segments[0].y, 0);
  EXPECT_EQ(layout.segments[0].height, 4 + 10 + 10 + 4);
  EXPECT_EQ(layout.segments[1].y, 28 + 6);
  EXPECT_EQ(layout.segments[1].height, 10);
}

TEST(PreachingCardLayout, AVerseReferenceKeepsRoomForItsMark) {
  const std::vector<CardInput> cards{textOfLines(CardKind::Verse, "a", "abcd efgh")};
  Metrics metrics = plainMetrics(200);
  metrics.width = 90;
  metrics.referenceIndent = 20;  // 70 px left: "abcd efgh" (90 px) wraps
  const Layout layout = layOut(cards, metrics);
  ASSERT_EQ(layout.lines.size(), 3u);
  EXPECT_TRUE(layout.lines[1].reference);
  EXPECT_TRUE(layout.lines[1].firstReference);
  EXPECT_TRUE(layout.lines[2].reference);
  EXPECT_FALSE(layout.lines[2].firstReference);
}

TEST(PreachingCardLayout, VerseOrdinalsAndTheirPages) {
  const std::vector<CardInput> cards{
      textOfLines(CardKind::Title, "T"),
      textOfLines(CardKind::Verse, "a", "R"),
      textOfLines(CardKind::Text, "b"),
      textOfLines(CardKind::Verse, "aaaaaaaaaa bbbbbbbbbb", "R"),
      textOfLines(CardKind::NextVisit, "Next", "Q"),
  };
  const Layout layout = layOut(cards, plainMetrics(50));
  EXPECT_EQ(layout.cardVerse, (std::vector<int16_t>{-1, 0, -1, 1, -1}));
  ASSERT_EQ(layout.verseFirstPage.size(), 2u);
  EXPECT_EQ(layout.verseFirstPage[0], 0);
  EXPECT_EQ(layout.verseFirstPage[1], 1);
  const int last = layout.pageCount() - 1;
  EXPECT_EQ(layout.segments.back().card, 4);
  EXPECT_EQ(cardsOnPage(layout, last).back(), 4);
}
EOF_FILE
```

Register the suite in `test/ui_layout/CMakeLists.txt` (append; this is the suite's own file, not
the shared `test/CMakeLists.txt`):

```bash
cat >> test/ui_layout/CMakeLists.txt <<'EOF_CMAKE'

add_executable(PreachingCardsTest
  PreachingCardLayoutTest.cpp
  ${REPO_ROOT}/src/activities/preaching/PreachingCardLayout.cpp
)

target_include_directories(PreachingCardsTest PRIVATE
  ${REPO_ROOT}/src
)

target_link_libraries(PreachingCardsTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(PreachingCardsTest)
EOF_CMAKE
```

```bash
cmake -S test -B build/test >/dev/null && cmake --build build/test --target PreachingCardsTest -j8   # fails: PreachingCardLayout.h does not exist
```

### Step 2.2 — the header (3 min)

```bash
mkdir -p src/activities/preaching
cat > src/activities/preaching/PreachingCardLayout.h <<'EOF_FILE'
#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

// Lays out a Predicación topic -- a question's blocks or a tag's passages -- as cards on pages:
// lines measured once, pages broken between cards where they can be, and a card taller than a
// page continued on the next. Nothing is cut. Free of Arduino and the renderer -- widths come
// through MeasureFn -- so test/ui_layout runs it on the host (modelled on StudySleepFit.h).
namespace preaching {

enum class CardKind : uint8_t { Title, Heading, Verse, Text, NextVisit };
enum class Face : uint8_t { Title, Heading, Quote, Body, Reference, Label };
inline constexpr size_t FACE_COUNT = 6;

// A line is copied into a buffer of this size, NUL included, to be drawn.
inline constexpr size_t LINE_BUFFER_BYTES = 128;

// Pixel width of `text` set in `face`.
using MeasureFn = int (*)(const void* ctx, Face face, const char* text);

// `body` and `reference` are views: whatever owns the strings must outlive the Layout.
struct CardInput {
  CardKind kind = CardKind::Text;
  std::string_view body;
  // Verse: the scripture reference. NextVisit: the next question. Empty otherwise.
  std::string_view reference;
};

struct Metrics {
  int width = 0;            // the text column, px
  int pageHeight = 0;       // the body band a page fills, px
  int cardPadding = 0;      // inside a boxed card, above its first line and below its last
  int cardGap = 0;          // between cards on a page
  int referenceIndent = 0;  // room a Verse reference keeps on its left for the "›" mark
  int lineHeight[FACE_COUNT] = {};
};

struct Line {
  uint16_t card = 0;
  uint32_t begin = 0;  // byte offset into the card's body, or its reference when `reference`
  uint8_t length = 0;
  Face face = Face::Body;
  bool reference = false;
  bool firstReference = false;
};

// The part of one card that sits on one page.
struct Segment {
  uint16_t card = 0;
  uint16_t firstLine = 0;
  uint16_t lineCount = 0;
  int16_t y = 0;  // from the top of the page's body band
  int16_t height = 0;
  bool opensCard = false;
  bool closesCard = false;
};

struct Layout {
  std::vector<Line> lines;
  std::vector<Segment> segments;
  std::vector<uint16_t> pageFirstSegment;  // page p holds [pageFirstSegment[p], pageFirstSegment[p + 1])
  std::vector<int16_t> cardVerse;          // per card: its Verse ordinal, -1 for any other kind
  std::vector<uint16_t> verseFirstPage;    // per Verse ordinal: the page where that card begins

  int pageCount() const { return static_cast<int>(pageFirstSegment.size()); }
  size_t segmentsEnd(int page) const {
    return page + 1 < pageCount() ? pageFirstSegment[static_cast<size_t>(page) + 1] : segments.size();
  }
};

constexpr bool isBoxed(const CardKind kind) { return kind == CardKind::Verse || kind == CardKind::NextVisit; }

// False, with `out` cleared, when a count would not fit the layout's index types. An empty input
// is one empty page.
bool layoutCards(const CardInput* cards, size_t count, const Metrics& metrics, MeasureFn measure, const void* ctx,
                 Layout& out);

// The lines of `text` no wider than `width` in `face`, as [begin, begin + length) byte spans,
// appended to `out`. Breaks at U+0020 (and ASCII tab, CR, LF, which the draw path shows as spaces)
// only: U+00A0 and U+202F keep their words together, as GfxRenderer::wrappedText does. A word
// wider than the column, or a run of LINE_BUFFER_BYTES or more, breaks at a codepoint boundary.
struct Span {
  uint32_t begin;
  uint8_t length;
};
void wrapText(std::string_view text, Face face, int width, MeasureFn measure, const void* ctx, std::vector<Span>& out);

}  // namespace preaching
EOF_FILE
```

### Step 2.3 — the implementation (5 min)

Wrapping is greedy over U+0020 (and tab, CR, LF); a word wider than the column breaks at a codepoint
boundary. Paging places a title or heading together with the card after it; a group taller than a
page starts a new page unless the page holds only titles and headings, and its last card continues
line by line; a Verse's reference line never starts a page alone.

```bash
mkdir -p src/activities/preaching
cat > src/activities/preaching/PreachingCardLayout.cpp <<'EOF_FILE'
#include "PreachingCardLayout.h"

#include <string>

namespace preaching {
namespace {

bool isBreak(const char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }

size_t nextCodepoint(const std::string_view text, size_t at) {
  ++at;
  while (at < text.size() && (static_cast<unsigned char>(text[at]) & 0xC0) == 0x80) ++at;
  return at;
}

Face bodyFace(const CardKind kind) {
  switch (kind) {
    case CardKind::Title:
      return Face::Title;
    case CardKind::Heading:
      return Face::Heading;
    case CardKind::Verse:
      return Face::Quote;
    case CardKind::Text:
      return Face::Body;
    case CardKind::NextVisit:
      return Face::Label;
  }
  return Face::Body;
}

Face referenceFace(const CardKind kind) { return kind == CardKind::Verse ? Face::Reference : Face::Body; }

bool isHeader(const CardKind kind) { return kind == CardKind::Title || kind == CardKind::Heading; }

struct CardLines {
  size_t first = 0;
  size_t count = 0;
};

class Paginator {
 public:
  Paginator(const CardInput* cards, const std::vector<CardLines>& cardLines, const Metrics& metrics, Layout& out)
      : cards(cards), cardLines(cardLines), metrics(metrics), out(out) {
    newPage();
  }

  void run(const size_t count) {
    size_t first = 0;
    while (first < count) {
      // A title or heading travels with the card after it, so it never ends a page.
      size_t last = first;
      while (last + 1 < count && isHeader(cards[last].kind)) ++last;

      int groupHeight = 0;
      for (size_t card = first; card <= last; ++card) {
        groupHeight += cardHeight(card) + (card > first ? metrics.cardGap : 0);
      }

      if (groupHeight <= metrics.pageHeight - y - gapBefore()) {
        for (size_t card = first; card <= last; ++card) placeWhole(card);
      } else if (groupHeight <= metrics.pageHeight) {
        newPage();
        for (size_t card = first; card <= last; ++card) placeWhole(card);
      } else {
        // Taller than a page. A page holding only titles and headings counts as empty here, so
        // they share their page with the tall card's first lines.
        if (!pageOnlyHeaders) newPage();
        for (size_t card = first; card < last; ++card) placeWhole(card);
        placeSplit(last);
      }
      first = last + 1;
    }
  }

 private:
  const CardInput* cards;
  const std::vector<CardLines>& cardLines;
  const Metrics& metrics;
  Layout& out;
  int y = 0;
  bool pageEmpty = true;
  bool pageOnlyHeaders = true;

  void newPage() {
    out.pageFirstSegment.push_back(static_cast<uint16_t>(out.segments.size()));
    y = 0;
    pageEmpty = true;
    pageOnlyHeaders = true;
  }

  int gapBefore() const { return pageEmpty ? 0 : metrics.cardGap; }

  int lineHeight(const size_t line) const { return metrics.lineHeight[static_cast<size_t>(out.lines[line].face)]; }

  int padding(const size_t card) const { return isBoxed(cards[card].kind) ? metrics.cardPadding : 0; }

  int cardHeight(const size_t card) const {
    int height = 2 * padding(card);
    const CardLines& lines = cardLines[card];
    for (size_t line = lines.first; line < lines.first + lines.count; ++line) height += lineHeight(line);
    return height;
  }

  void push(const size_t card, const size_t firstLine, const size_t lineCount, const int top, const int height,
            const bool opens, const bool closes) {
    Segment segment;
    segment.card = static_cast<uint16_t>(card);
    segment.firstLine = static_cast<uint16_t>(firstLine);
    segment.lineCount = static_cast<uint16_t>(lineCount);
    segment.y = static_cast<int16_t>(top);
    segment.height = static_cast<int16_t>(height);
    segment.opensCard = opens;
    segment.closesCard = closes;
    out.segments.push_back(segment);
    y = top + height;
    pageEmpty = false;
    pageOnlyHeaders = pageOnlyHeaders && isHeader(cards[card].kind);
  }

  void placeWhole(const size_t card) {
    const int height = cardHeight(card);
    if (!pageEmpty && height > metrics.pageHeight - y - gapBefore()) newPage();
    const CardLines& lines = cardLines[card];
    push(card, lines.first, lines.count, y + gapBefore(), height, true, true);
  }

  void placeSplit(const size_t card) {
    const CardLines& lines = cardLines[card];
    if (lines.count == 0) {
      placeWhole(card);
      return;
    }
    const int pad = padding(card);
    const size_t end = lines.first + lines.count;
    size_t line = lines.first;
    bool opens = true;
    while (true) {
      const int top = y + gapBefore();
      const int room = metrics.pageHeight - top;
      int used = opens ? pad : 0;
      size_t take = line;
      while (take < end) {
        const int closingPad = take + 1 == end ? pad : 0;
        if (used + lineHeight(take) + closingPad > room) break;
        used += lineHeight(take);
        ++take;
      }
      if (take == line) {
        if (!pageEmpty) {
          newPage();
          continue;
        }
        // Not even one line fits an empty page: place it anyway so the layout always advances.
        used += lineHeight(take);
        ++take;
      }
      // A Verse's reference never starts a page alone: its last quote line goes with it.
      if (take < end && take > line + 1 && out.lines[take].firstReference) {
        --take;
        used -= lineHeight(take);
      }
      const bool closes = take == end;
      if (closes) used += pad;
      push(card, line, take - line, top, used, opens, closes);
      if (closes) return;
      line = take;
      opens = false;
      newPage();
    }
  }
};

}  // namespace

void wrapText(const std::string_view text, const Face face, const int width, const MeasureFn measure,
              const void* const ctx, std::vector<Span>& out) {
  std::string scratch;
  scratch.reserve(LINE_BUFFER_BYTES);
  auto fits = [&](const size_t begin, const size_t end) {
    if (end - begin >= LINE_BUFFER_BYTES) return false;
    scratch.assign(text.data() + begin, end - begin);
    return measure(ctx, face, scratch.c_str()) <= width;
  };
  auto wordEndFrom = [&](size_t at) {
    while (at < text.size() && !isBreak(text[at])) ++at;
    return at;
  };
  auto skipBreaks = [&](size_t at) {
    while (at < text.size() && isBreak(text[at])) ++at;
    return at;
  };

  size_t pos = skipBreaks(0);
  while (pos < text.size()) {
    size_t lineEnd = pos;
    size_t cursor = pos;
    while (cursor < text.size()) {
      const size_t wordEnd = wordEndFrom(cursor);
      if (!fits(pos, wordEnd)) break;
      lineEnd = wordEnd;
      cursor = skipBreaks(wordEnd);
    }
    if (lineEnd == pos) {
      // The word does not fit alone: break it at the last codepoint that fits, taking at least one.
      const size_t wordEnd = wordEndFrom(pos);
      size_t cut = nextCodepoint(text, pos);
      while (cut < wordEnd) {
        const size_t next = nextCodepoint(text, cut);
        if (!fits(pos, next)) break;
        cut = next;
      }
      lineEnd = cut;
      cursor = cut;
    }
    out.push_back({static_cast<uint32_t>(pos), static_cast<uint8_t>(lineEnd - pos)});
    pos = cursor;
  }
}

bool layoutCards(const CardInput* const cards, const size_t count, const Metrics& metrics, const MeasureFn measure,
                 const void* const ctx, Layout& out) {
  out = Layout{};
  if (count > UINT16_MAX) return false;

  std::vector<CardLines> cardLines;
  cardLines.reserve(count);
  out.cardVerse.reserve(count);
  out.lines.reserve(count * 4);
  std::vector<Span> spans;
  spans.reserve(16);
  int16_t verses = 0;

  for (size_t card = 0; card < count; ++card) {
    const CardInput& input = cards[card];
    CardLines lines;
    lines.first = out.lines.size();

    spans.clear();
    const Face body = bodyFace(input.kind);
    wrapText(input.body, body, metrics.width, measure, ctx, spans);
    for (const Span span : spans) {
      out.lines.push_back({static_cast<uint16_t>(card), span.begin, span.length, body, false, false});
    }

    if (!input.reference.empty()) {
      spans.clear();
      const Face reference = referenceFace(input.kind);
      const int width = input.kind == CardKind::Verse ? metrics.width - metrics.referenceIndent : metrics.width;
      wrapText(input.reference, reference, width, measure, ctx, spans);
      for (size_t i = 0; i < spans.size(); ++i) {
        out.lines.push_back({static_cast<uint16_t>(card), spans[i].begin, spans[i].length, reference, true, i == 0});
      }
    }

    lines.count = out.lines.size() - lines.first;
    cardLines.push_back(lines);
    out.cardVerse.push_back(input.kind == CardKind::Verse ? verses++ : static_cast<int16_t>(-1));
    if (out.lines.size() > UINT16_MAX || verses < 0) {
      out = Layout{};
      return false;
    }
  }

  out.segments.reserve(count + 8);
  out.pageFirstSegment.reserve(count / 4 + 2);
  Paginator(cards, cardLines, metrics, out).run(count);

  out.verseFirstPage.assign(static_cast<size_t>(verses), 0);
  for (int page = 0; page < out.pageCount(); ++page) {
    for (size_t s = out.pageFirstSegment[static_cast<size_t>(page)]; s < out.segmentsEnd(page); ++s) {
      const Segment& segment = out.segments[s];
      const int16_t verse = out.cardVerse[segment.card];
      if (segment.opensCard && verse >= 0) out.verseFirstPage[static_cast<size_t>(verse)] = static_cast<uint16_t>(page);
    }
  }
  return true;
}

}  // namespace preaching
EOF_FILE
```

```bash
cmake -S test -B build/test >/dev/null && cmake --build build/test --target PreachingCardsTest -j8
build/test/ui_layout/PreachingCardsTest   # 15 tests pass: PreachingCardWrap (5), PreachingCardLayout (10)
```

```bash
git add src/activities/preaching/PreachingCardLayout.h src/activities/preaching/PreachingCardLayout.cpp test/ui_layout/PreachingCardLayoutTest.cpp test/ui_layout/CMakeLists.txt
git commit -m "feat(preaching): lay out topic cards on pages" -m "Claude-Session: https://claude.ai/code/session_01AQ3bc72oCQQ2JANTbFtX9Y"
```

## Task 3 — `PreachingCardNav` (pure, host-tested)

### Step 3.1 — failing tests (3 min)

```bash
mkdir -p test/ui_layout
cat > test/ui_layout/PreachingCardNavTest.cpp <<'EOF_FILE'
#include <gtest/gtest.h>

#include <vector>

#include "activities/preaching/PreachingCardNav.h"

using preaching::CardCursor;
using preaching::Layout;

namespace {

// A layout reduced to what navigation reads: the page count and where each Verse card begins.
Layout pagesWithVerses(const int pages, const std::vector<uint16_t>& verseFirstPage) {
  Layout layout;
  for (int page = 0; page < pages; ++page) layout.pageFirstSegment.push_back(0);
  layout.verseFirstPage = verseFirstPage;
  return layout;
}

bool sameCursor(const CardCursor& a, const CardCursor& b) { return a.page == b.page && a.selected == b.selected; }

}  // namespace

TEST(PreachingCardNav, RightStepsThroughAPageThenTurnsIt) {
  const Layout layout = pagesWithVerses(2, {0, 0, 1});
  CardCursor cursor = preaching::selectVerse(layout, 0);
  ASSERT_TRUE(preaching::nextCard(layout, cursor));
  EXPECT_TRUE(sameCursor(cursor, {0, 1}));
  ASSERT_TRUE(preaching::nextCard(layout, cursor));
  EXPECT_TRUE(sameCursor(cursor, {1, 2}));
  EXPECT_FALSE(preaching::nextCard(layout, cursor));
  EXPECT_TRUE(sameCursor(cursor, {1, 2}));
}

TEST(PreachingCardNav, RightReachesAPageWithoutVerses) {
  const Layout layout = pagesWithVerses(3, {0, 2});
  CardCursor cursor = preaching::selectVerse(layout, 0);
  ASSERT_TRUE(preaching::nextCard(layout, cursor));
  EXPECT_TRUE(sameCursor(cursor, {1, -1}));
  ASSERT_TRUE(preaching::nextCard(layout, cursor));
  EXPECT_TRUE(sameCursor(cursor, {2, 1}));
}

TEST(PreachingCardNav, ProseOnlyTopicStillPages) {
  const Layout layout = pagesWithVerses(3, {});
  CardCursor cursor = preaching::selectVerse(layout, 0);
  EXPECT_TRUE(sameCursor(cursor, {0, -1}));
  ASSERT_TRUE(preaching::nextCard(layout, cursor));
  ASSERT_TRUE(preaching::nextCard(layout, cursor));
  EXPECT_TRUE(sameCursor(cursor, {2, -1}));
  EXPECT_FALSE(preaching::nextCard(layout, cursor));
}

TEST(PreachingCardNav, LeftMirrorsRightAndSelectsThePreviousPagesLastVerse) {
  const Layout layout = pagesWithVerses(3, {0, 0, 2});
  CardCursor cursor = preaching::selectVerse(layout, 2);
  ASSERT_TRUE(preaching::previousCard(layout, cursor));
  EXPECT_TRUE(sameCursor(cursor, {1, -1}));
  ASSERT_TRUE(preaching::previousCard(layout, cursor));
  EXPECT_TRUE(sameCursor(cursor, {0, 1}));
  ASSERT_TRUE(preaching::previousCard(layout, cursor));
  EXPECT_TRUE(sameCursor(cursor, {0, 0}));
  EXPECT_FALSE(preaching::previousCard(layout, cursor));
}

TEST(PreachingCardNav, SwipesTurnPagesAndSelectTheirFirstVerse) {
  const Layout layout = pagesWithVerses(3, {0, 1, 1});
  CardCursor cursor = preaching::selectVerse(layout, 0);
  ASSERT_TRUE(preaching::turnPage(layout, cursor, +1));
  EXPECT_TRUE(sameCursor(cursor, {1, 1}));
  ASSERT_TRUE(preaching::turnPage(layout, cursor, +1));
  EXPECT_TRUE(sameCursor(cursor, {2, -1}));
  EXPECT_FALSE(preaching::turnPage(layout, cursor, +1));
  ASSERT_TRUE(preaching::turnPage(layout, cursor, -1));
  EXPECT_TRUE(sameCursor(cursor, {1, 1}));
}

TEST(PreachingCardNav, SelectingAVerseShowsItsPage) {
  const Layout layout = pagesWithVerses(3, {0, 1, 2});
  EXPECT_TRUE(sameCursor(preaching::selectVerse(layout, 2), {2, 2}));
  EXPECT_TRUE(sameCursor(preaching::selectVerse(layout, 9), {0, 0}));
  EXPECT_TRUE(sameCursor(preaching::selectVerse(layout, -1), {0, 0}));
}
EOF_FILE
```

Add the file to the executable:

```bash
python3 - <<'EOF_PY'
path = "test/ui_layout/CMakeLists.txt"
text = open(path).read()
old = "  PreachingCardLayoutTest.cpp\n  ${REPO_ROOT}/src/activities/preaching/PreachingCardLayout.cpp\n"
new = "  PreachingCardLayoutTest.cpp\n  PreachingCardNavTest.cpp\n  ${REPO_ROOT}/src/activities/preaching/PreachingCardLayout.cpp\n"
assert text.count(old) == 1
open(path, "w").write(text.replace(old, new))
EOF_PY
cmake -S test -B build/test >/dev/null && cmake --build build/test --target PreachingCardsTest -j8   # fails: PreachingCardNav.h does not exist
```

### Step 3.2 — the navigation rules (3 min)

```bash
mkdir -p src/activities/preaching
cat > src/activities/preaching/PreachingCardNav.h <<'EOF_FILE'
#pragma once

#include <algorithm>

#include "PreachingCardLayout.h"

// Left/Right and swipes over a laid-out topic. Only Verse cards are selectable; buttons still reach
// every page, so a page of prose is read by stepping through it. Pure, for test/ui_layout.
namespace preaching {

struct CardCursor {
  int page = 0;
  int selected = -1;  // Verse ordinal, -1 for none
};

namespace card_nav_detail {

struct VerseRange {
  int first;
  int end;
};

// The Verse cards that begin on `page`: contiguous, because cards are laid out in order.
inline VerseRange versesBeginningOn(const Layout& layout, const int page) {
  const auto& pages = layout.verseFirstPage;
  const auto first = std::lower_bound(pages.begin(), pages.end(), page);
  const auto end = std::upper_bound(first, pages.end(), page);
  return {static_cast<int>(first - pages.begin()), static_cast<int>(end - pages.begin())};
}

}  // namespace card_nav_detail

// `page`, with the first Verse card that begins on it selected, or none.
inline CardCursor atPage(const Layout& layout, const int page) {
  const auto range = card_nav_detail::versesBeginningOn(layout, page);
  return {page, range.first < range.end ? range.first : -1};
}

// The page where `verse` begins, with it selected; out of range selects the first Verse card.
inline CardCursor selectVerse(const Layout& layout, const int verse) {
  const int verses = static_cast<int>(layout.verseFirstPage.size());
  if (verse >= 0 && verse < verses) return {layout.verseFirstPage[static_cast<size_t>(verse)], verse};
  if (verses > 0) return {layout.verseFirstPage[0], 0};
  return {0, -1};
}

// Right: the next Verse card beginning on this page, else the next page. False at the end.
inline bool nextCard(const Layout& layout, CardCursor& cursor) {
  const auto range = card_nav_detail::versesBeginningOn(layout, cursor.page);
  const bool onPage = cursor.selected >= range.first && cursor.selected < range.end;
  const int candidate = onPage ? cursor.selected + 1 : range.first;
  if (candidate < range.end) {
    cursor.selected = candidate;
    return true;
  }
  if (cursor.page + 1 >= layout.pageCount()) return false;
  cursor = atPage(layout, cursor.page + 1);
  return true;
}

// Left: the previous Verse card beginning on this page, else the previous page with its last
// Verse card selected. False at the start.
inline bool previousCard(const Layout& layout, CardCursor& cursor) {
  const auto range = card_nav_detail::versesBeginningOn(layout, cursor.page);
  const bool onPage = cursor.selected >= range.first && cursor.selected < range.end;
  if (onPage && cursor.selected > range.first) {
    --cursor.selected;
    return true;
  }
  if (cursor.page == 0) return false;
  const int page = cursor.page - 1;
  const auto previous = card_nav_detail::versesBeginningOn(layout, page);
  cursor = {page, previous.first < previous.end ? previous.end - 1 : -1};
  return true;
}

// A vertical swipe: `direction` > 0 for the next page. False past either end.
inline bool turnPage(const Layout& layout, CardCursor& cursor, const int direction) {
  const int page = cursor.page + (direction > 0 ? 1 : -1);
  if (page < 0 || page >= layout.pageCount()) return false;
  cursor = atPage(layout, page);
  return true;
}

}  // namespace preaching
EOF_FILE
```

```bash
cmake -S test -B build/test >/dev/null && cmake --build build/test --target PreachingCardsTest -j8
build/test/ui_layout/PreachingCardsTest   # 21 tests pass
```

```bash
git add src/activities/preaching/PreachingCardNav.h test/ui_layout/PreachingCardNavTest.cpp test/ui_layout/CMakeLists.txt
git commit -m "feat(preaching): step and page through topic cards" -m "Claude-Session: https://claude.ai/code/session_01AQ3bc72oCQQ2JANTbFtX9Y"
```

## Task 4 — every Predicación string (3 min)

Append to the end of each file (English is the reference; other languages fall back to it):

```bash
cat >> lib/I18n/translations/english.yaml <<'EOF_YAML'
STR_PREACHING: "Preaching"
STR_PREACHING_SUBTITLE: "Bible questions · your topics"
STR_PREACHING_QUESTIONS: "Questions"
STR_PREACHING_MY_TOPICS: "My topics"
STR_PREACHING_CHOOSE_TOPICS: "Choose topics"
STR_PREACHING_QUESTION_N: "Question %d"
STR_PREACHING_NEXT_VISIT: "For the return visit"
STR_PREACHING_NO_SECTION: "This Bible doesn't have the preaching questions"
STR_PREACHING_NO_TOPICS: "No topics yet. Choose tags to preach from."
STR_PREACHING_TOPIC_EMPTY: "No Bible verses have this tag"
STR_PREACHING_VERSES_UNREADABLE: "Can't read your tagged verses"
STR_PREACHING_SHOW_POSITION: "%d / %d · %s"
STR_PREACHING_OPEN_FAILED: "Couldn't open this topic"
STR_PREACHING_VERSE_NOT_FOUND: "Verse not found in this Bible"
STR_PREACHING_SAVE_FAILED: "Couldn't save your topics"
EOF_YAML
cat >> lib/I18n/translations/spanish.yaml <<'EOF_YAML'
STR_PREACHING: "Predicación"
STR_PREACHING_SUBTITLE: "Preguntas de la Biblia · tus temas"
STR_PREACHING_QUESTIONS: "Preguntas"
STR_PREACHING_MY_TOPICS: "Mis temas"
STR_PREACHING_CHOOSE_TOPICS: "Elegir temas"
STR_PREACHING_QUESTION_N: "Pregunta %d"
STR_PREACHING_NEXT_VISIT: "Para la revisita"
STR_PREACHING_NO_SECTION: "Esta Biblia no tiene las preguntas para predicar"
STR_PREACHING_NO_TOPICS: "Aún no hay temas. Elige etiquetas para predicar."
STR_PREACHING_TOPIC_EMPTY: "Ningún versículo de la Biblia tiene esta etiqueta"
STR_PREACHING_VERSES_UNREADABLE: "No se pueden leer tus versículos etiquetados"
STR_PREACHING_SHOW_POSITION: "%d / %d · %s"
STR_PREACHING_OPEN_FAILED: "No se pudo abrir este tema"
STR_PREACHING_VERSE_NOT_FOUND: "No se encontró el versículo en esta Biblia"
STR_PREACHING_SAVE_FAILED: "No se pudieron guardar tus temas"
EOF_YAML
python3 scripts/gen_i18n.py lib/I18n/translations lib/I18n/
grep -c "STR_PREACHING" lib/I18n/I18nKeys.h   # 15
git status --short                            # only the two YAML files
```

```bash
git add lib/I18n/translations/english.yaml lib/I18n/translations/spanish.yaml
git commit -m "feat(preaching): add the Predicación strings in English and Spanish" -m "Claude-Session: https://claude.ai/code/session_01AQ3bc72oCQQ2JANTbFtX9Y"
```

## Task 5 — `PreachingResult` (3 min, plus the first firmware build)

Apply this patch to `src/activities/ActivityResult.h` (run from the worktree root):

```bash
git apply <<'EOF_PATCH'
diff --git a/src/activities/ActivityResult.h b/src/activities/ActivityResult.h
index be3cb01c..b149ba51 100644
--- a/src/activities/ActivityResult.h
+++ b/src/activities/ActivityResult.h
@@ -11,6 +11,8 @@
 #include <variant>
 #include <vector>
 
+#include "preaching/PreachingOrigin.h"
+
 struct WifiResult {
   bool connected = false;
   std::string ssid;
@@ -87,9 +89,15 @@ struct TagSelectionResult {
   std::vector<study::TagId> tagIds;
 };
 
+// The Predicación topic, and the Verse card within it, a screen ended on, for the screen beneath to
+// select.
+struct PreachingResult {
+  PreachingOrigin at;
+};
+
 using ResultVariant = std::variant<std::monostate, WifiResult, KeyboardResult, MenuResult, ChapterResult, PercentResult,
                                    IntervalResult, PageResult, ProgressChangeResult, NetworkModeResult, FootnoteResult,
-                                   FilePathResult, TagSelectionResult>;
+                                   FilePathResult, TagSelectionResult, PreachingResult>;
 
 struct ActivityResult {
   bool isCancelled = false;
EOF_PATCH
```

The first `pio run` in a fresh worktree is cold (about 3.5 minutes):

```bash
~/.platformio/penv/bin/pio run -e x4pro 2>&1 | tee /tmp/berean-o-16-pio.log | tail -3
grep -nE "^src/.*warning" /tmp/berean-o-16-pio.log   # prints nothing
```

```bash
git add src/activities/ActivityResult.h
git commit -m "feat(preaching): return a preaching origin as an activity result" -m "Claude-Session: https://claude.ai/code/session_01AQ3bc72oCQQ2JANTbFtX9Y"
```

## Task 6 — `PreachingSession` (5 min)

The Bible (taken from `ParkedBook`, else opened as the reader opens a fresh one), the 21 spine
indices from one `resolveFilenamesToSpineIndices`, the question labels (a TOC entry only when it
belongs to that spine), the picked topics and the Mis temas counts.

```bash
mkdir -p src/activities/preaching
cat > src/activities/preaching/PreachingSession.h <<'EOF_FILE'
#pragma once

#include <Epub.h>
#include <Preaching/IntroSection.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "activities/reader/ParkedBook.h"
#include "study/PreachingPassages.h"
#include "study/PreachingTopicsFile.h"

class GfxRenderer;

// What Predicación's screens share while it is open: the Bible, taken from ParkedBook and parked
// again on close; where the twenty questions sit in its spine and how their rows read; the picked
// topics; and the Mis temas counts once made. Owned by PreachingActivity. PreachingTopicActivity
// holds a reference: it is only ever pushed over its own PreachingActivity, and the activity
// manager destroys it first. Loop task only.
class PreachingSession {
 public:
  PreachingSession() = default;
  PreachingSession(const PreachingSession&) = delete;
  PreachingSession& operator=(const PreachingSession&) = delete;

  // Finds the Bible, takes or opens it, and resolves the section. Never fails hard: whatever is
  // missing shows as no Bible or no section.
  void open(GfxRenderer& renderer);
  // Parks the Bible for the next screen that opens it.
  void close();

  bool hasBible() const { return bible.book != nullptr; }
  const std::string& biblePath() const { return path; }
  const std::shared_ptr<Epub>& book() const { return bible.book; }
  bool sectionFound() const { return section; }
  int spine(int question) const;
  // "8. ¿Tiene Dios la culpa del sufrimiento?", or "Question 8" without a TOC entry of its own.
  const std::string& questionLabel(int question) const;

  void ensureTopics();
  // For after Elegir temas saves: reads the picks again and drops the held counts.
  void reloadTopics();
  // The picked tags still active, in TagPalette::activeIdsByName order.
  const std::vector<study::TagId>& topics() const { return activeTopics; }
  const std::string& topicName(study::TagId id) const { return topicsFile.palette().name(id); }
  // The row of `id` in topics(), -1 when it is not an active pick.
  int topicRow(study::TagId id) const;

  void ensureCounts();
  bool hasCounts() const { return countsLoaded && PreachingPassages::isAvailable(countsStatus); }
  // Passages carrying topics()[row]; only meaningful while hasCounts().
  uint16_t count(size_t row) const { return row < counts.size() ? counts[row].passages : 0; }

 private:
  std::string path;
  ParkedBook::Held bible;
  int spines[preaching::IntroSection::SLOT_COUNT] = {};
  bool section = false;
  std::string labels[preaching::IntroSection::SLOT_COUNT];

  PreachingTopicsFile topicsFile;
  bool topicsLoaded = false;
  std::vector<study::TagId> activeTopics;

  bool countsLoaded = false;
  PreachingPassages::Status countsStatus = PreachingPassages::Status::Missing;
  std::vector<PreachingPassages::TagCount> counts;

  bool takeOrOpenBible(GfxRenderer& renderer);
  void resolveSection();
  std::string labelFor(int question) const;
};
EOF_FILE
```
```bash
mkdir -p src/activities/preaching
cat > src/activities/preaching/PreachingSession.cpp <<'EOF_FILE'
#include "PreachingSession.h"

#include <Arduino.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>
#include <SdPaths.h>

#include <algorithm>
#include <cstdio>
#include <optional>
#include <utility>

#include "CrossPointSettings.h"
#include "RecentBooksStore.h"
#include "activities/launcher/BibleFinder.h"
#include "components/UITheme.h"
#include "study/PsramJsonAllocator.h"

namespace {

constexpr const char* MODULE = "PREACH";

using preaching::IntroSection::QUESTION_COUNT;
using preaching::IntroSection::SLOT_COUNT;

bool isQuestion(const int question) { return question >= 1 && question <= QUESTION_COUNT; }

}  // namespace

void PreachingSession::open(GfxRenderer& renderer) {
  const unsigned long started = millis();
  RECENT_BOOKS.loadFromFile();
  const auto found = BibleFinder::find();
  if (!found) {
    LOG_INF(MODULE, "No Bible on the card");
    return;
  }
  path = found->path;
  if (!takeOrOpenBible(renderer)) return;
  resolveSection();
  LOG_INF(MODULE, "Opened %s in %lu ms, section %s", path.c_str(), millis() - started, section ? "found" : "missing");
}

bool PreachingSession::takeOrOpenBible(GfxRenderer& renderer) {
  const bool skipLoadingCss = SETTINGS.embeddedStyle == 0;
  bible = ParkedBook::take(path, skipLoadingCss);
  if (!bible.book) {
    auto fresh = makeUniqueNoThrow<Epub>(path, sdpaths::CROSSPOINT_DIR);
    if (!fresh) {
      LOG_ERR(MODULE, "OOM: Bible EPUB");
      return false;
    }
    const bool uncached = !Storage.exists((fresh->getCachePath() + "/book.bin").c_str());
    if (uncached) GUI.drawPopup(renderer, tr(STR_INDEXING));
    bool loaded;
    {
      std::optional<GfxRenderer::FrameBufferLoan> loan;
      if (uncached) loan.emplace(renderer);
      loaded = fresh->load(true, skipLoadingCss);
    }
    if (!loaded) {
      LOG_ERR(MODULE, "Failed to load the Bible: %s", path.c_str());
      return false;
    }
    bible = {std::shared_ptr<Epub>(std::move(fresh)), nullptr};
  }
  bible.book->setupCacheDir();
  return true;
}

void PreachingSession::resolveSection() {
  auto names = makeUniqueNoThrow<std::string[]>(SLOT_COUNT);
  if (!names) {
    LOG_ERR(MODULE, "OOM: %d section filenames", SLOT_COUNT);
    return;
  }
  for (int slot = 0; slot < SLOT_COUNT; ++slot) names[slot] = preaching::IntroSection::BASENAMES[slot];
  bible.book->resolveFilenamesToSpineIndices(names.get(), spines, SLOT_COUNT);
  section = std::all_of(spines, spines + SLOT_COUNT, [](const int spineIndex) { return spineIndex >= 0; });
  if (!section) return;
  for (int question = 1; question <= QUESTION_COUNT; ++question) labels[question] = labelFor(question);
}

std::string PreachingSession::labelFor(const int question) const {
  const int spineIndex = spines[question];
  const int toc = bible.book->getTocIndexForSpineIndex(spineIndex);
  if (toc >= 0) {
    // A spine with no TOC entry of its own inherits the previous spine's, so ownership is checked.
    const auto entry = bible.book->getTocItem(toc);
    const std::string_view title = preaching::IntroSection::stripTocPrefix(entry.title);
    if (entry.spineIndex == spineIndex && !title.empty()) {
      std::string label = std::to_string(question) + ". ";
      label.append(title);
      return label;
    }
  }
  char fallback[48];
  snprintf(fallback, sizeof(fallback), tr(STR_PREACHING_QUESTION_N), question);
  return fallback;
}

void PreachingSession::close() {
  if (bible.book) ParkedBook::park(path, std::move(bible));
  bible = {};
}

int PreachingSession::spine(const int question) const { return isQuestion(question) ? spines[question] : -1; }

const std::string& PreachingSession::questionLabel(const int question) const {
  return labels[isQuestion(question) ? question : 0];
}

void PreachingSession::ensureTopics() {
  if (topicsLoaded) return;
  topicsLoaded = true;
  topicsFile.load();

  const std::vector<study::TagId> byName = topicsFile.palette().activeIdsByName();
  const std::vector<study::TagId>& picked = topicsFile.tags();
  activeTopics.clear();
  activeTopics.reserve(picked.size());
  for (const study::TagId id : byName) {
    if (std::find(picked.begin(), picked.end(), id) != picked.end()) activeTopics.push_back(id);
  }
}

void PreachingSession::reloadTopics() {
  topicsLoaded = false;
  countsLoaded = false;
  counts.clear();
  ensureTopics();
}

int PreachingSession::topicRow(const study::TagId id) const {
  const auto found = std::find(activeTopics.begin(), activeTopics.end(), id);
  return found == activeTopics.end() ? -1 : static_cast<int>(found - activeTopics.begin());
}

void PreachingSession::ensureCounts() {
  if (countsLoaded) return;
  ensureTopics();
  countsLoaded = true;
  if (activeTopics.empty()) {
    countsStatus = PreachingPassages::Status::Ok;
    counts.clear();
    return;
  }
  countsStatus = PreachingPassages::countByTag(activeTopics, PsramJsonAllocator::json(), counts);
}
EOF_FILE
```

No host harness covers SD, `Epub` or the renderer here; the firmware build is the check:

```bash
~/.platformio/penv/bin/pio run -e x4pro 2>&1 | tee /tmp/berean-o-16-pio.log | tail -3
grep -nE "^src/.*warning" /tmp/berean-o-16-pio.log   # prints nothing
```

```bash
git add src/activities/preaching/PreachingSession.h src/activities/preaching/PreachingSession.cpp
git commit -m "feat(preaching): hold the Bible, section and topics for Predicación" -m "Claude-Session: https://claude.ai/code/session_01AQ3bc72oCQQ2JANTbFtX9Y"
```

## Task 7 — `PreachingContent` and `TopicCards` (5 min)

`TopicCards` cannot be copied or moved: its `cards` view into its own strings and the session's
labels.

```bash
mkdir -p src/activities/preaching
cat > src/activities/preaching/PreachingContent.h <<'EOF_FILE'
#pragma once

#include <Preaching/QuestionParser.h>

#include <cstdint>
#include <string>
#include <vector>

#include "PreachingCardLayout.h"
#include "PreachingOrigin.h"
#include "study/PreachingPassages.h"

class GfxRenderer;
class PreachingSession;

// One topic's cards: a question read from the Bible, or the Bible passages carrying a tag.
// `cards` views into `parsed`, `passages` and the session's question labels, so a TopicCards is
// never copied or moved -- moving a short std::string relocates its bytes -- and lives behind a
// std::unique_ptr; replacing a topic swaps pointers.
struct TopicCards {
  PreachingOrigin::Kind kind = PreachingOrigin::Kind::None;
  int question = 0;
  study::TagId tag{};
  std::string header;
  preaching::Question parsed;
  std::vector<PreachingPassages::Passage> passages;
  std::vector<preaching::CardInput> cards;
  // Per Verse ordinal: its block in `parsed` (a question) or its passage (a tag).
  std::vector<uint16_t> verseSource;

  TopicCards() = default;
  TopicCards(const TopicCards&) = delete;
  TopicCards& operator=(const TopicCards&) = delete;
  TopicCards(TopicCards&&) = delete;
  TopicCards& operator=(TopicCards&&) = delete;

  int verseCount() const { return static_cast<int>(verseSource.size()); }
};

namespace PreachingContent {

enum class LoadResult : uint8_t { Ok, Unreadable, OutOfMemory, VersesUnavailable };

// Loop task only, never under a RenderLock: the stream takes the render lock itself when the
// question's HTML has to be inflated. `out` must be freshly constructed.
LoadResult loadQuestion(const PreachingSession& session, GfxRenderer& renderer, int question, TopicCards& out);
LoadResult loadTag(const PreachingSession& session, study::TagId tag, TopicCards& out);

// The toast for a failed load.
const char* failureMessage(LoadResult result);

}  // namespace PreachingContent
EOF_FILE
```
```bash
mkdir -p src/activities/preaching
cat > src/activities/preaching/PreachingContent.cpp <<'EOF_FILE'
#include "PreachingContent.h"

#include <I18n.h>
#include <Logging.h>
#include <Preaching/IntroSection.h>

#include <cstdio>

#include "PreachingSession.h"
#include "activities/reader/SpineHtmlStream.h"
#include "study/PsramJsonAllocator.h"

namespace PreachingContent {
namespace {

constexpr const char* MODULE = "PREACH";

bool feedParser(void* ctx, const char* chunk, const size_t length, const bool isFinal) {
  return static_cast<preaching::QuestionParser*>(ctx)->feed(chunk, length, isFinal);
}

preaching::CardKind cardKindOf(const preaching::BlockKind kind) {
  switch (kind) {
    case preaching::BlockKind::Heading:
      return preaching::CardKind::Heading;
    case preaching::BlockKind::Verse:
      return preaching::CardKind::Verse;
    case preaching::BlockKind::Text:
      return preaching::CardKind::Text;
  }
  return preaching::CardKind::Text;
}

void buildQuestionCards(const PreachingSession& session, TopicCards& out) {
  const auto& blocks = out.parsed.blocks;
  const int next = preaching::IntroSection::nextQuestion(out.question);
  out.cards.reserve(blocks.size() + 2);
  out.verseSource.reserve(blocks.size());
  if (!out.parsed.title.empty()) out.cards.push_back({preaching::CardKind::Title, out.parsed.title, {}});
  for (size_t index = 0; index < blocks.size(); ++index) {
    const preaching::Block& block = blocks[index];
    const preaching::CardKind kind = cardKindOf(block.kind);
    const std::string_view reference =
        kind == preaching::CardKind::Verse ? std::string_view(block.reference) : std::string_view();
    out.cards.push_back({kind, block.text, reference});
    if (kind == preaching::CardKind::Verse) out.verseSource.push_back(static_cast<uint16_t>(index));
  }
  if (next != 0) {
    out.cards.push_back({preaching::CardKind::NextVisit, tr(STR_PREACHING_NEXT_VISIT), session.questionLabel(next)});
  }
}

}  // namespace

LoadResult loadQuestion(const PreachingSession& session, GfxRenderer& renderer, const int question, TopicCards& out) {
  if (!session.sectionFound() || session.spine(question) < 0) {
    LOG_ERR(MODULE, "Question %d is not in this Bible", question);
    return LoadResult::Unreadable;
  }
  preaching::QuestionParser parser;
  if (!parser.valid()) return LoadResult::OutOfMemory;
  const bool streamed =
      SpineHtmlStream::stream(session.book(), session.spine(question), renderer, &feedParser, &parser);
  if (parser.outOfMemory()) return LoadResult::OutOfMemory;
  if (!streamed || !parser.take(out.parsed)) {
    LOG_ERR(MODULE, "Question %d did not read or parse", question);
    return LoadResult::Unreadable;
  }

  out.kind = PreachingOrigin::Kind::Question;
  out.question = question;
  char header[48];
  snprintf(header, sizeof(header), tr(STR_PREACHING_QUESTION_N), question);
  out.header = header;
  buildQuestionCards(session, out);
  return LoadResult::Ok;
}

LoadResult loadTag(const PreachingSession& session, const study::TagId tag, TopicCards& out) {
  const PreachingPassages::Status status =
      PreachingPassages::passagesWithTag(tag, PsramJsonAllocator::passageDoc(), out.passages);
  switch (status) {
    case PreachingPassages::Status::Ok:
      break;
    case PreachingPassages::Status::OutOfMemory:
      return LoadResult::OutOfMemory;
    case PreachingPassages::Status::Missing:
    case PreachingPassages::Status::Unreadable:
      return LoadResult::VersesUnavailable;
  }

  out.kind = PreachingOrigin::Kind::Tag;
  out.tag = tag;
  out.header = session.topicName(tag);
  out.cards.reserve(out.passages.size());
  out.verseSource.reserve(out.passages.size());
  for (size_t index = 0; index < out.passages.size(); ++index) {
    const PreachingPassages::Passage& passage = out.passages[index];
    out.cards.push_back({preaching::CardKind::Verse, passage.text.view(), passage.reference});
    out.verseSource.push_back(static_cast<uint16_t>(index));
  }
  return LoadResult::Ok;
}

const char* failureMessage(const LoadResult result) {
  return result == LoadResult::VersesUnavailable ? tr(STR_PREACHING_VERSES_UNREADABLE) : tr(STR_PREACHING_OPEN_FAILED);
}

}  // namespace PreachingContent
EOF_FILE
```

```bash
~/.platformio/penv/bin/pio run -e x4pro 2>&1 | tee /tmp/berean-o-16-pio.log | tail -3
grep -nE "^src/.*warning" /tmp/berean-o-16-pio.log   # prints nothing
```

```bash
git add src/activities/preaching/PreachingContent.h src/activities/preaching/PreachingContent.cpp
git commit -m "feat(preaching): load a question or tag topic as cards" -m "Claude-Session: https://claude.ai/code/session_01AQ3bc72oCQQ2JANTbFtX9Y"
```

## Task 8 — `PreachingTopicActivity` (5 min)

Cards are drawn with the renderer in `render()`; the screen function only registers hit rects
(ClockOffsetActivity's shape). `openShow` is T4's seam and only logs.

```bash
mkdir -p src/activities/preaching
cat > src/activities/preaching/PreachingTopicActivity.h <<'EOF_FILE'
#pragma once

#include <atomic>
#include <memory>

#include "PreachingCardLayout.h"
#include "PreachingCardNav.h"
#include "PreachingContent.h"
#include "PreachingOrigin.h"
#include "activities/Activity.h"
#include "components/UiAppHost.h"
#include "util/ButtonNavigator.h"

class PreachingSession;

// A question or a tag topic as paged cards. Left/Right select Verse cards, turning the page at its
// edge; a vertical swipe turns the page. Tapping a quote opens show mode, a reference opens the
// Bible, and "Para la revisita" replaces the question in place. Back returns to the list with a
// PreachingResult. Drawn with the renderer, with UiAppHost hit rects over the cards
// (ClockOffsetActivity's shape).
class PreachingTopicActivity final : public Activity, private UiAppHost {
 public:
  PreachingTopicActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, const PreachingSession& session,
                         std::unique_ptr<TopicCards> cards, int startVerse);

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  struct Geometry {
    int bodyTop = 0;
    int pageHeight = 0;
    int cardX = 0;
    int cardWidth = 0;
    int textX = 0;
    int textWidth = 0;
    int cardPadding = 0;
    int cardGap = 0;
    int cornerRadius = 0;
    int markWidth = 0;
  };

  // Show mode at `verse` (T4).
  void openShow(int verse);
  void openBible(int verse);
  void openNextQuestion();

  void computeGeometry();
  bool layOut(const TopicCards& cards, preaching::Layout& out) const;
  void moveTo(const preaching::CardCursor& next);
  void finishWithResult();
  PreachingOrigin originAt(int verse) const;
  void reportFailure(const char* message);

  void drawPage() const;
  void drawLine(const preaching::CardInput& card, const preaching::Line& line, int y) const;
  void buildHits(UiScreen& screen) const;
  static void topicScreen(UiScreen& screen, void* user);
  static void onQuoteTapped(const freeink::ui::ActionEvent& event, void* user);
  static void onReferenceTapped(const freeink::ui::ActionEvent& event, void* user);
  static void onNextVisitTapped(const freeink::ui::ActionEvent& event, void* user);

  const PreachingSession& session;
  std::unique_ptr<TopicCards> topic;
  std::unique_ptr<preaching::Layout> layout;
  preaching::CardCursor cursor;
  int startVerse;
  Geometry geometry;
  ButtonNavigator buttonNavigator;
  std::atomic<bool> halfRefreshPending{true};
};
EOF_FILE
```
```bash
mkdir -p src/activities/preaching
cat > src/activities/preaching/PreachingTopicActivity.cpp <<'EOF_FILE'
#include "PreachingTopicActivity.h"

#include <Arduino.h>
#include <EpdFontFamily.h>
#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>
#include <Preaching/IntroSection.h>

#include <algorithm>
#include <cstdio>
#include <string_view>
#include <utility>

#include "MappedInputManager.h"
#include "PreachingSession.h"
#include "activities/PostedMessage.h"
#include "components/UITheme.h"
#include "components/UiAppHelpers.h"
#include "fontIds.h"

namespace fui = freeink::ui;

namespace {

constexpr const char* MODULE = "PREACH";

constexpr fui::ActionId ACTION_QUOTE = 1;
constexpr fui::ActionId ACTION_REFERENCE = 2;
constexpr fui::ActionId ACTION_NEXT_VISIT = 3;

constexpr int CARD_BORDER = 1;
constexpr int CARD_BORDER_SELECTED = 3;
constexpr const char* REFERENCE_MARK = "›";
constexpr const char* REFERENCE_MARK_SPACED = "› ";

struct FaceFont {
  int fontId;
  EpdFontFamily::Style style;
};

// Indexed by preaching::Face.
constexpr FaceFont FACE_FONTS[preaching::FACE_COUNT] = {
    {NOTOSERIF_14_FONT_ID, EpdFontFamily::BOLD},     // Title
    {UI_10_FONT_ID, EpdFontFamily::BOLD},            // Heading
    {NOTOSERIF_12_FONT_ID, EpdFontFamily::ITALIC},   // Quote
    {NOTOSERIF_12_FONT_ID, EpdFontFamily::REGULAR},  // Body
    {NOTOSERIF_12_FONT_ID, EpdFontFamily::BOLD},     // Reference
    {UI_10_FONT_ID, EpdFontFamily::BOLD},            // Label
};

const FaceFont& faceFont(const preaching::Face face) { return FACE_FONTS[static_cast<size_t>(face)]; }

int measureFace(const void* ctx, const preaching::Face face, const char* text) {
  const FaceFont& font = faceFont(face);
  return static_cast<const GfxRenderer*>(ctx)->getTextWidth(font.fontId, text, font.style);
}

}  // namespace

PreachingTopicActivity::PreachingTopicActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                               const PreachingSession& session, std::unique_ptr<TopicCards> cards,
                                               const int startVerse)
    : Activity("PreachingTopic", renderer, mappedInput),
      UiAppHost(renderer),
      session(session),
      topic(std::move(cards)),
      startVerse(startVerse) {}

void PreachingTopicActivity::onEnter() {
  Activity::onEnter();
  computeGeometry();
  layout = makeUniqueNoThrow<preaching::Layout>();
  if (!layout || !layOut(*topic, *layout)) {
    LOG_ERR(MODULE, "Could not lay out the topic");
    layout.reset();
    PostedMessage::post(tr(STR_PREACHING_OPEN_FAILED));
    finishWithResult();
    return;
  }
  cursor = preaching::selectVerse(*layout, startVerse);

  resetUi();
  app.on(ACTION_QUOTE, &PreachingTopicActivity::onQuoteTapped, this);
  app.on(ACTION_REFERENCE, &PreachingTopicActivity::onReferenceTapped, this);
  app.on(ACTION_NEXT_VISIT, &PreachingTopicActivity::onNextVisitTapped, this);
  app.setScreen(&PreachingTopicActivity::topicScreen, this);
  requestUpdate();
}

void PreachingTopicActivity::computeGeometry() {
  const auto& metrics = UITheme::getInstance().getMetrics();
  geometry.bodyTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  geometry.pageHeight =
      renderer.getScreenHeight() - metrics.buttonHintsHeight - metrics.verticalSpacing - geometry.bodyTop;
  geometry.cardX = metrics.contentSidePadding;
  geometry.cardWidth = renderer.getScreenWidth() - 2 * metrics.contentSidePadding;
  geometry.textX = geometry.cardX + metrics.listSidePadding;
  geometry.textWidth = geometry.cardWidth - 2 * metrics.listSidePadding;
  geometry.cardPadding = metrics.verticalSpacing;
  geometry.cardGap = metrics.verticalSpacing;
  geometry.cornerRadius = metrics.listRowRadius;
  const FaceFont& reference = faceFont(preaching::Face::Reference);
  geometry.markWidth = renderer.getTextWidth(reference.fontId, REFERENCE_MARK_SPACED, reference.style);
}

bool PreachingTopicActivity::layOut(const TopicCards& cards, preaching::Layout& out) const {
  preaching::Metrics metrics;
  metrics.width = geometry.textWidth;
  metrics.pageHeight = geometry.pageHeight;
  metrics.cardPadding = geometry.cardPadding;
  metrics.cardGap = geometry.cardGap;
  metrics.referenceIndent = geometry.markWidth;
  for (size_t face = 0; face < preaching::FACE_COUNT; ++face) {
    metrics.lineHeight[face] = renderer.getLineHeight(FACE_FONTS[face].fontId);
  }
  return preaching::layoutCards(cards.cards.data(), cards.cards.size(), metrics, &measureFace, &renderer, out);
}

void PreachingTopicActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    finishWithResult();
    return;
  }

  const auto touch = routeTouch(mappedInput);
  if (touch.routed && app.invalidated()) requestUpdate();
  if (touch) return;
  if (!layout) return;

  const auto swipe = mappedInput.wasSwipe();
  if (swipe == MappedInputManager::SwipeDir::Up || swipe == MappedInputManager::SwipeDir::Down) {
    preaching::CardCursor next = cursor;
    if (preaching::turnPage(*layout, next, swipe == MappedInputManager::SwipeDir::Up ? 1 : -1)) moveTo(next);
    return;
  }

  buttonNavigator.onNextRelease([this] {
    preaching::CardCursor next = cursor;
    if (preaching::nextCard(*layout, next)) moveTo(next);
  });
  buttonNavigator.onPreviousRelease([this] {
    preaching::CardCursor next = cursor;
    if (preaching::previousCard(*layout, next)) moveTo(next);
  });
}

void PreachingTopicActivity::moveTo(const preaching::CardCursor& next) {
  {
    RenderLock lock(*this);
    cursor = next;
  }
  requestUpdate();
}

PreachingOrigin PreachingTopicActivity::originAt(const int verse) const {
  if (topic->kind == PreachingOrigin::Kind::Question) return PreachingOrigin::ofQuestion(topic->question, verse);
  if (topic->kind == PreachingOrigin::Kind::Tag) return PreachingOrigin::ofTag(study::toRaw(topic->tag), verse);
  return {};
}

void PreachingTopicActivity::finishWithResult() {
  setResult(PreachingResult{originAt(cursor.selected)});
  finish();
}

void PreachingTopicActivity::reportFailure(const char* message) {
  PostedMessage::post(message);
  requestUpdate();
}

void PreachingTopicActivity::openShow(const int verse) {
  LOG_INF(MODULE, "Show mode is not built yet (verse %d)", verse);
}

void PreachingTopicActivity::openBible(const int verse) {
  if (verse < 0 || verse >= topic->verseCount()) return;
  const size_t source = topic->verseSource[static_cast<size_t>(verse)];
  if (!session.hasBible()) {
    LOG_ERR(MODULE, "No Bible to open verse %d in", verse);
    reportFailure(tr(STR_PREACHING_VERSE_NOT_FOUND));
    return;
  }

  ReaderEntryIntent intent;
  if (topic->kind == PreachingOrigin::Kind::Question) {
    const std::string& href = topic->parsed.blocks[source].href;
    [[maybe_unused]] const unsigned long started = millis();
    const bool resolves = ReaderEntryIntent::openHref(href, originAt(verse), intent) &&
                          session.book()->resolveHrefToSpineIndex(href) >= 0;
    LOG_DBG(MODULE, "Reference check: %lu ms", millis() - started);
    if (!resolves) {
      LOG_ERR(MODULE, "Reference not opened: \"%s\"", href.c_str());
      reportFailure(tr(STR_PREACHING_VERSE_NOT_FOUND));
      return;
    }
  } else {
    const PreachingPassages::Passage& passage = topic->passages[source];
    intent = ReaderEntryIntent::of(ReaderEntryIntent::Kind::OpenAt);
    intent.unit = passage.start;
    intent.spineHint = passage.spine;
    intent.origin = originAt(verse);
  }
  app.clearTapFlash();
  activityManager.goToReader(session.biblePath(), false, intent);
}

void PreachingTopicActivity::openNextQuestion() {
  if (topic->kind != PreachingOrigin::Kind::Question) return;
  const int next = preaching::IntroSection::nextQuestion(topic->question);
  if (next == 0) return;

  auto cards = makeUniqueNoThrow<TopicCards>();
  auto nextLayout = makeUniqueNoThrow<preaching::Layout>();
  if (!cards || !nextLayout) {
    LOG_ERR(MODULE, "OOM: question %d", next);
    reportFailure(tr(STR_PREACHING_OPEN_FAILED));
    return;
  }
  const auto result = PreachingContent::loadQuestion(session, renderer, next, *cards);
  if (result != PreachingContent::LoadResult::Ok) {
    reportFailure(PreachingContent::failureMessage(result));
    return;
  }
  if (!layOut(*cards, *nextLayout)) {
    LOG_ERR(MODULE, "Could not lay out question %d", next);
    reportFailure(tr(STR_PREACHING_OPEN_FAILED));
    return;
  }
  {
    RenderLock lock(*this);
    std::swap(topic, cards);
    std::swap(layout, nextLayout);
    cursor = preaching::selectVerse(*layout, 0);
  }
  app.clearTapFlash();
  halfRefreshPending = true;
  requestUpdate();
}

void PreachingTopicActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const auto& metrics = UITheme::getInstance().getMetrics();
  char pageIndicator[24];
  snprintf(pageIndicator, sizeof(pageIndicator), "%d/%d", cursor.page + 1, layout ? layout->pageCount() : 1);
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, renderer.getScreenWidth(), metrics.headerHeight},
                 topic->header.c_str(), pageIndicator);

  if (topic->cards.empty()) {
    renderer.drawCenteredText(UI_10_FONT_ID, geometry.bodyTop + geometry.pageHeight / 2, tr(STR_PREACHING_TOPIC_EMPTY));
  } else if (layout) {
    drawPage();
  }

  renderUi();

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer(halfRefreshPending.exchange(false) ? HalDisplay::HALF_REFRESH : HalDisplay::FAST_REFRESH);
  PostedMessage::drawNext(renderer);
}

void PreachingTopicActivity::drawPage() const {
  const preaching::Layout& page = *layout;
  for (size_t s = page.pageFirstSegment[static_cast<size_t>(cursor.page)]; s < page.segmentsEnd(cursor.page); ++s) {
    const preaching::Segment& segment = page.segments[s];
    const preaching::CardInput& card = topic->cards[segment.card];
    const int top = geometry.bodyTop + segment.y;
    const bool boxed = preaching::isBoxed(card.kind);
    if (boxed) {
      const bool selected = page.cardVerse[segment.card] >= 0 && page.cardVerse[segment.card] == cursor.selected;
      renderer.drawRoundedRect(geometry.cardX, top, geometry.cardWidth, segment.height,
                               selected ? CARD_BORDER_SELECTED : CARD_BORDER, geometry.cornerRadius, true);
    }
    int y = top + (boxed && segment.opensCard ? geometry.cardPadding : 0);
    for (uint16_t i = 0; i < segment.lineCount; ++i) {
      const preaching::Line& line = page.lines[segment.firstLine + i];
      drawLine(card, line, y);
      y += renderer.getLineHeight(faceFont(line.face).fontId);
    }
  }
}

void PreachingTopicActivity::drawLine(const preaching::CardInput& card, const preaching::Line& line,
                                      const int y) const {
  const std::string_view source = line.reference ? card.reference : card.body;
  char text[preaching::LINE_BUFFER_BYTES];
  const size_t length = std::min<size_t>(line.length, sizeof(text) - 1);
  // Tabs and newlines in a passage's text are break points to the layout and spaces on screen.
  for (size_t i = 0; i < length; ++i) {
    const char c = source[line.begin + i];
    text[i] = static_cast<unsigned char>(c) < 0x20 ? ' ' : c;
  }
  text[length] = '\0';

  const FaceFont& font = faceFont(line.face);
  int x = geometry.textX;
  if (card.kind == preaching::CardKind::Verse && line.reference) {
    x = geometry.textX + geometry.textWidth - renderer.getTextWidth(font.fontId, text, font.style);
    if (line.firstReference)
      renderer.drawText(font.fontId, x - geometry.markWidth, y, REFERENCE_MARK, true, font.style);
  }
  renderer.drawText(font.fontId, x, y, text, true, font.style);
}

void PreachingTopicActivity::topicScreen(UiScreen& screen, void* user) {
  static_cast<const PreachingTopicActivity*>(user)->buildHits(screen);
}

void PreachingTopicActivity::buildHits(UiScreen& screen) const {
  // The cards are drawn in render(); this only registers touch rects over them.
  if (!mappedInput.hasTouch() || !layout) return;
  const preaching::Layout& page = *layout;
  for (size_t s = page.pageFirstSegment[static_cast<size_t>(cursor.page)]; s < page.segmentsEnd(cursor.page); ++s) {
    const preaching::Segment& segment = page.segments[s];
    const preaching::CardKind kind = topic->cards[segment.card].kind;
    const int top = geometry.bodyTop + segment.y;
    const int bottom = top + segment.height;
    if (kind == preaching::CardKind::NextVisit) {
      screen.frame().hit(fui::makeRect(geometry.cardX, top, geometry.cardWidth, segment.height), ACTION_NEXT_VISIT, 0,
                         fui::InputTouch);
      continue;
    }
    if (kind != preaching::CardKind::Verse) continue;

    const int16_t verse = page.cardVerse[segment.card];
    int y = top + (segment.opensCard ? geometry.cardPadding : 0);
    int referenceTop = -1;
    bool hasQuote = false;
    for (uint16_t i = 0; i < segment.lineCount; ++i) {
      const preaching::Line& line = page.lines[segment.firstLine + i];
      if (line.reference && referenceTop < 0) referenceTop = y;
      if (!line.reference) hasQuote = true;
      y += renderer.getLineHeight(faceFont(line.face).fontId);
    }
    const int split = referenceTop < 0 ? bottom : (hasQuote ? referenceTop : top);
    if (split > top) {
      screen.frame().hit(fui::makeRect(geometry.cardX, top, geometry.cardWidth, split - top), ACTION_QUOTE, verse,
                         fui::InputTouch);
    }
    if (split < bottom) {
      screen.frame().hit(fui::makeRect(geometry.cardX, split, geometry.cardWidth, bottom - split), ACTION_REFERENCE,
                         verse, fui::InputTouch);
    }
  }
}

void PreachingTopicActivity::onQuoteTapped(const fui::ActionEvent& event, void* user) {
  static_cast<PreachingTopicActivity*>(user)->openShow(event.value);
}

void PreachingTopicActivity::onReferenceTapped(const fui::ActionEvent& event, void* user) {
  static_cast<PreachingTopicActivity*>(user)->openBible(event.value);
}

void PreachingTopicActivity::onNextVisitTapped(const fui::ActionEvent&, void* user) {
  static_cast<PreachingTopicActivity*>(user)->openNextQuestion();
}
EOF_FILE
```

```bash
~/.platformio/penv/bin/pio run -e x4pro 2>&1 | tee /tmp/berean-o-16-pio.log | tail -3
grep -nE "^src/.*warning" /tmp/berean-o-16-pio.log   # prints nothing
```

```bash
git add src/activities/preaching/PreachingTopicActivity.h src/activities/preaching/PreachingTopicActivity.cpp
git commit -m "feat(preaching): show a topic as paged cards" -m "Claude-Session: https://claude.ai/code/session_01AQ3bc72oCQQ2JANTbFtX9Y"
```

## Task 9 — `PreachingActivity` and `goToPreaching` (5 min)

`openTopicPicker` is T5's seam and only logs; `onTopicsChanged` is ready for T5's result handler.

```bash
mkdir -p src/activities/preaching
cat > src/activities/preaching/PreachingActivity.h <<'EOF_FILE'
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "PreachingOrigin.h"
#include "PreachingSession.h"
#include "activities/UiTabListActivity.h"

struct TopicCards;

// Predicación: the Bible's twenty preaching questions (Preguntas) and the owner's picked tags
// (Mis temas). Opening one pushes PreachingTopicActivity. Opened from Home with no origin, or
// rebuilt from the reader's origin with the topic pushed again at its card.
class PreachingActivity final : public UiTabListActivity {
 public:
  PreachingActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, const PreachingOrigin& origin = {});

  void onEnter() override;
  void onExit() override;

 private:
  enum class Tab : uint8_t { Questions, Topics, Count };

  int listCount() const override;
  int tabCount() const override { return static_cast<int>(Tab::Count); }
  int activeTab() const override { return static_cast<int>(tab); }
  const char* tabLabel(int index) const override;
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  void onTabAction(int index) override;
  void stepTab(int direction) override;
  bool handleButtons() override;
  const char* headerTitle() const override;

  // Elegir temas (T5).
  void openTopicPicker();
  // Reads the picks again and recounts, for after Elegir temas saves.
  void onTopicsChanged();

  void showTab(Tab next);
  // The caller holds the RenderLock: buildScreen reads the rows on the render task.
  void rebuildRows();
  void seedSelection();
  void openQuestion(int question, int verse);
  void openTag(study::TagId tag, int verse);
  void pushTopic(std::unique_ptr<TopicCards> cards, int verse);
  void reportOpenFailure(const char* message);
  void onTopicResult(const ActivityResult& result);

  PreachingOrigin origin;
  PreachingSession session;
  Tab tab = Tab::Questions;
  std::string chooseTopicsLabel;
  std::vector<std::string> rowValues;
  std::vector<freeink::ui::ListItem> rowItems;
};
EOF_FILE
```
```bash
mkdir -p src/activities/preaching
cat > src/activities/preaching/PreachingActivity.cpp <<'EOF_FILE'
#include "PreachingActivity.h"

#include <I18n.h>
#include <Logging.h>
#include <Memory.h>
#include <Preaching/IntroSection.h>

#include <string>
#include <utility>
#include <variant>

#include "MappedInputManager.h"
#include "PreachingContent.h"
#include "PreachingTopicActivity.h"
#include "activities/PostedMessage.h"
#include "components/UITheme.h"

namespace fui = freeink::ui;

namespace {

constexpr const char* MODULE = "PREACH";

using preaching::IntroSection::QUESTION_COUNT;

}  // namespace

PreachingActivity::PreachingActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                     const PreachingOrigin& origin)
    : UiTabListActivity("Preaching", renderer, mappedInput), origin(origin) {}

void PreachingActivity::onEnter() {
  session.open(renderer);
  chooseTopicsLabel = std::string("+ ") + tr(STR_PREACHING_CHOOSE_TOPICS);
  if (origin.kind == PreachingOrigin::Kind::Tag) {
    // Only the picks: the counts wait until Mis temas is shown, so a return from the Bible reads
    // bible.json once, for the topic.
    session.ensureTopics();
    tab = Tab::Topics;
  }
  {
    RenderLock lock(*this);
    rebuildRows();
  }
  UiTabListActivity::onEnter();
  seedSelection();

  if (origin.kind == PreachingOrigin::Kind::Question) {
    openQuestion(origin.question, origin.verse);
  } else if (origin.kind == PreachingOrigin::Kind::Tag) {
    openTag(study::toTagId(origin.tag), origin.verse);
  }
}

void PreachingActivity::onExit() {
  session.close();
  UiTabListActivity::onExit();
}

const char* PreachingActivity::headerTitle() const { return tr(STR_PREACHING); }

const char* PreachingActivity::tabLabel(const int index) const {
  return static_cast<Tab>(index) == Tab::Questions ? tr(STR_PREACHING_QUESTIONS) : tr(STR_PREACHING_MY_TOPICS);
}

int PreachingActivity::listCount() const { return static_cast<int>(rowItems.size()); }

void PreachingActivity::rebuildRows() {
  rowItems.clear();
  rowValues.clear();
  if (tab == Tab::Questions) {
    if (!session.sectionFound()) return;
    rowItems.reserve(QUESTION_COUNT);
    for (int question = 1; question <= QUESTION_COUNT; ++question) {
      fui::ListItem item;
      item.label = session.questionLabel(question).c_str();
      item.actionValue = static_cast<int16_t>(question - 1);
      rowItems.push_back(item);
    }
    return;
  }

  const auto& topics = session.topics();
  rowValues.reserve(topics.size());
  rowItems.reserve(topics.size() + 1);
  for (size_t row = 0; row < topics.size(); ++row) {
    rowValues.push_back(session.hasCounts() ? std::to_string(session.count(row)) : std::string());
  }
  for (size_t row = 0; row < topics.size(); ++row) {
    fui::ListItem item;
    item.label = session.topicName(topics[row]).c_str();
    item.value = rowValues[row].empty() ? nullptr : rowValues[row].c_str();
    item.actionValue = static_cast<int16_t>(row);
    rowItems.push_back(item);
  }
  fui::ListItem choose;
  choose.label = chooseTopicsLabel.c_str();
  choose.actionValue = static_cast<int16_t>(topics.size());
  rowItems.push_back(choose);
}

void PreachingActivity::seedSelection() {
  int row = 0;
  if (origin.kind == PreachingOrigin::Kind::Question && origin.question >= 1 && origin.question <= QUESTION_COUNT) {
    row = origin.question - 1;
  } else if (origin.kind == PreachingOrigin::Kind::Tag) {
    const int tagRow = session.topicRow(study::toTagId(origin.tag));
    row = tagRow >= 0 ? tagRow : 0;
  }
  auto& nav = activeNav();
  nav.selected = listCount() > row ? row + 1 : 0;
  nav.followOnBuild = true;
}

void PreachingActivity::showTab(const Tab next) {
  if (next == Tab::Topics) session.ensureCounts();
  RenderLock lock(*this);
  tab = next;
  rebuildRows();
}

void PreachingActivity::onTabAction(const int index) {
  showTab(static_cast<Tab>(index));
  auto& nav = activeNav();
  nav.selected = 0;
  nav.followOnBuild = true;
  app.clearTapFlash();
  requestUpdate();
}

void PreachingActivity::stepTab(int) {
  const bool onTabBar = ringPos() == 0;
  showTab(tab == Tab::Questions ? Tab::Topics : Tab::Questions);
  auto& nav = activeNav();
  nav.selected = onTabBar || listCount() == 0 ? 0 : 1;
  nav.followOnBuild = true;
  requestUpdate();
}

bool PreachingActivity::handleButtons() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    finish();
    return true;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (ringPos() == 0) {
      stepTab(1);
    } else {
      activateIndex(ringPos() - 1);
    }
    return true;
  }
  return false;
}

void PreachingActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  screen.setContentMargin(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), 0,
                                      static_cast<int16_t>(metrics.buttonHintsHeight), 0});
  buildTabBar(screen);

  if (tab == Tab::Questions && !session.sectionFound()) {
    screen.centeredText(tr(STR_PREACHING_NO_SECTION), screen.theme().bodyText);
    return;
  }
  if (tab == Tab::Topics && session.topics().empty()) {
    fui::TextStyle style = screen.theme().bodyText;
    style.align = fui::TextAlign::Center;
    style.maxLines = 2;
    const int lineHeight = screen.target().lineHeight(style.font);
    const fui::Rect notice =
        screen.takeTop(static_cast<int16_t>(2 * lineHeight), static_cast<int16_t>(metrics.verticalSpacing));
    screen.target().text(notice, tr(STR_PREACHING_NO_TOPICS), style);
  }

  fui::ListProps props;
  props.items = rowItems.data();
  props.count = static_cast<uint16_t>(rowItems.size());
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;
  props.valueInset = 8;
  props.labelText = screen.theme().bodyText;
  props.labelText.maxLines = 2;
  syncTabListViewport(screen, props);
  screen.list(props);
}

void PreachingActivity::activateIndex(const int index) {
  app.clearTapFlash();
  if (tab == Tab::Questions) {
    openQuestion(index + 1, 0);
    return;
  }
  const auto& topics = session.topics();
  if (index == static_cast<int>(topics.size())) {
    openTopicPicker();
  } else if (index >= 0 && index < static_cast<int>(topics.size())) {
    openTag(topics[static_cast<size_t>(index)], 0);
  }
}

void PreachingActivity::openTopicPicker() { LOG_INF(MODULE, "Elegir temas is not built yet"); }

void PreachingActivity::onTopicsChanged() {
  session.reloadTopics();
  if (tab == Tab::Topics) session.ensureCounts();
  {
    RenderLock lock(*this);
    rebuildRows();
  }
  requestUpdate();
}

void PreachingActivity::reportOpenFailure(const char* message) {
  PostedMessage::post(message);
  requestUpdate();
}

void PreachingActivity::openQuestion(const int question, const int verse) {
  auto cards = makeUniqueNoThrow<TopicCards>();
  if (!cards) {
    LOG_ERR(MODULE, "OOM: question %d", question);
    reportOpenFailure(tr(STR_PREACHING_OPEN_FAILED));
    return;
  }
  const auto result = PreachingContent::loadQuestion(session, renderer, question, *cards);
  if (result != PreachingContent::LoadResult::Ok) {
    reportOpenFailure(PreachingContent::failureMessage(result));
    return;
  }
  pushTopic(std::move(cards), verse);
}

void PreachingActivity::openTag(const study::TagId tag, const int verse) {
  auto cards = makeUniqueNoThrow<TopicCards>();
  if (!cards) {
    LOG_ERR(MODULE, "OOM: tag %u", static_cast<unsigned>(study::toRaw(tag)));
    reportOpenFailure(tr(STR_PREACHING_OPEN_FAILED));
    return;
  }
  const auto result = PreachingContent::loadTag(session, tag, *cards);
  if (result != PreachingContent::LoadResult::Ok) {
    reportOpenFailure(PreachingContent::failureMessage(result));
    return;
  }
  pushTopic(std::move(cards), verse);
}

void PreachingActivity::pushTopic(std::unique_ptr<TopicCards> cards, const int verse) {
  auto topic = makeUniqueNoThrow<PreachingTopicActivity>(renderer, mappedInput, session, std::move(cards), verse);
  if (!topic) {
    LOG_ERR(MODULE, "OOM: topic activity");
    reportOpenFailure(tr(STR_PREACHING_OPEN_FAILED));
    return;
  }
  startActivityForResult(std::move(topic), [this](const ActivityResult& result) { onTopicResult(result); });
}

void PreachingActivity::onTopicResult(const ActivityResult& result) {
  const auto* returned = std::get_if<PreachingResult>(&result.data);
  if (!returned) return;
  const PreachingOrigin& at = returned->at;
  if (at.kind == PreachingOrigin::Kind::Question && session.sectionFound() && at.question >= 1 &&
      at.question <= QUESTION_COUNT) {
    showTab(Tab::Questions);
    moveRingTo(at.question);
  } else if (at.kind == PreachingOrigin::Kind::Tag) {
    showTab(Tab::Topics);
    const int row = session.topicRow(study::toTagId(at.tag));
    moveRingTo(row >= 0 ? row + 1 : (listCount() > 0 ? 1 : 0));
  }
}
EOF_FILE
```
Apply this patch to `src/activities/ActivityManager.h` (run from the worktree root):

```bash
git apply <<'EOF_PATCH'
diff --git a/src/activities/ActivityManager.h b/src/activities/ActivityManager.h
index 1a3b75af..b33aa3e0 100644
--- a/src/activities/ActivityManager.h
+++ b/src/activities/ActivityManager.h
@@ -84,6 +84,8 @@ class ActivityManager {
   void goToSettings();
   void goToFileBrowser(std::string path = {});
   void goToReader(std::string path, bool allowFastInitialRefresh = false, const ReaderEntryIntent& intent = {});
+  // Predicación's list, or, with an origin, the list with that question or tag topic open on top.
+  void goToPreaching(const PreachingOrigin& origin = {});
   void goToSleep(bool fromTimeout = false);
   void goToBoot();
   void goToFullScreenMessage(std::string message, EpdFontFamily::Style style = EpdFontFamily::REGULAR);
EOF_PATCH
```
Apply this patch to `src/activities/ActivityManager.cpp` (run from the worktree root):

```bash
git apply <<'EOF_PATCH'
diff --git a/src/activities/ActivityManager.cpp b/src/activities/ActivityManager.cpp
index fea4e332..56e18cce 100644
--- a/src/activities/ActivityManager.cpp
+++ b/src/activities/ActivityManager.cpp
@@ -15,6 +15,7 @@
 #include "home/FileBrowserActivity.h"
 #include "launcher/LauncherActivity.h"
 #include "network/CrossPointWebServerActivity.h"
+#include "preaching/PreachingActivity.h"
 #include "reader/ReaderActivity.h"
 #include "settings/SettingsActivity.h"
 #include "util/BmpViewerActivity.h"
@@ -228,6 +229,15 @@ void ActivityManager::goToReader(std::string path, const bool allowFastInitialRe
   }
 }
 
+void ActivityManager::goToPreaching(const PreachingOrigin& origin) {
+  auto activity = makeUniqueNoThrow<PreachingActivity>(renderer, mappedInput, origin);
+  if (!activity) {
+    LOG_ERR("ACT", "OOM: preaching activity");
+    return;
+  }
+  replaceActivity(std::move(activity));
+}
+
 void ActivityManager::goToSleep(bool fromTimeout) {
   replaceActivity(std::make_unique<SleepActivity>(renderer, mappedInput, fromTimeout));
   loop();  // Important: sleep screen must be rendered immediately, the caller will go to sleep right after this returns
EOF_PATCH
```

```bash
~/.platformio/penv/bin/pio run -e x4pro 2>&1 | tee /tmp/berean-o-16-pio.log | tail -3
grep -nE "^src/.*warning" /tmp/berean-o-16-pio.log   # prints nothing
```

```bash
git add src/activities/preaching/PreachingActivity.h src/activities/preaching/PreachingActivity.cpp src/activities/ActivityManager.h src/activities/ActivityManager.cpp
git commit -m "feat(preaching): add the Predicación tabs and goToPreaching" -m "Claude-Session: https://claude.ai/code/session_01AQ3bc72oCQQ2JANTbFtX9Y"
```

## Task 10 — Final checks (5 min)

```bash
git status --short                      # clean: every new file was added in its task
./bin/clang-format-fix                  # whole tree, as CI runs it; exits 0
git status --short                      # still clean; if not, commit the formatting:
# git add -u && git commit -m "style: format the Predicación screens" -m "<trailer>"
cmake -S test -B build/test >/dev/null && cmake --build build/test -j8 2>&1 | grep -E "error|warning: .*src/" ; \
  ctest --test-dir build/test --output-on-failure -j8 | tail -3       # 100% tests passed
~/.platformio/penv/bin/pio run -e x4pro 2>&1 | tee /tmp/berean-o-16-pio.log | tail -3
grep -nE "^src/.*warning" /tmp/berean-o-16-pio.log                    # prints nothing
git log --oneline origin/batch/20261015-predicaci-n-193x..HEAD        # the task commits
```

Stop here. The push and `hpipe submit` are not part of this plan.

## For the hand-off (not steps)

- **Seams:** `PreachingTopicActivity::openShow(int verse)` (T4), `PreachingActivity::openTopicPicker()`
  plus the ready `onTopicsChanged()` (T5), `ActivityManager::goToPreaching(origin)` (T6, T7), and
  `ReaderEntryIntent::origin`, `Kind::OpenHref`, `href`, `Route::Href` (T7; replace the placeholder
  case in `EpubReaderActivity::onBookLoaded`).
- **Widened files:** `src/activities/ActivityResult.h` (`PreachingResult`, spec A16) and the
  one-case `EpubReaderActivity.cpp` change (spec A21).
- **Device checks for the owner** (spec §Testing): Preguntas shows 20 titles in the Bible's
  language; a question pages without cutting text (Q6 headings, Q19 prose, the revisita card); a
  reference opens the Bible with `PARK: Reusing the parked book` on serial and logs `Reference check:
  … ms`; free heap with a warm slot is equal across Home → Predicación → topic → back → Home; the
  `PREACH: Counts: … ms` line. Until T6 there is no Home entry: call `activityManager.goToPreaching()`
  from a temporary local hook to test.
