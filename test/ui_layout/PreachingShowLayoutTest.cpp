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
      {CardKind::Title, "Title", ""},     {CardKind::Verse, "first", "Ref 1"},
      {CardKind::Heading, "Heading", ""}, {CardKind::Verse, "second", "Ref 2"},
      {CardKind::Text, "Text", ""},       {CardKind::NextVisit, "Next", "Pregunta 2"},
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

TEST(PreachingShowFit, AReferenceWordWiderThanTheColumnAlsoStepsDown) {
  // The quote fits rung 0; the 16-byte reference word would split there (224 px), not at rung 1.
  const auto layout = layOut("one", "Abcdefghijklmnop", 500);
  EXPECT_EQ(layout.rung, 1);
  ASSERT_EQ(layout.reference.size(), 1u);
  EXPECT_EQ(layout.reference[0].length, 16);
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

TEST(PreachingShowPaging, AShortQuoteIsOnePage) { EXPECT_EQ(layOut("one two three", "Jn 3:16", 100).pageCount(), 1); }
