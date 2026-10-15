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
