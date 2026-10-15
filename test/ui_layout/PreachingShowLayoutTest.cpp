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
