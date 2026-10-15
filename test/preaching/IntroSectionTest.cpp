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

// The design's rule, written independently of the implementation's table:
// the next question, except where the design sends a return visit elsewhere.
constexpr int expectedFollowUp(const int question) {
  switch (question) {
    case 7:
      return 10;
    case 12:
    case 16:
    case 17:
      return 18;
    case 18:
      return 20;
    case 20:
      return 0;
    default:
      return question + 1;
  }
}

constexpr bool followUpsMatchTheDesign() {
  for (int question = 1; question <= preaching::IntroSection::QUESTION_COUNT; question++) {
    if (nextQuestion(question) != expectedFollowUp(question)) return false;
  }
  return true;
}
static_assert(followUpsMatchTheDesign());

TEST(IntroSection, FollowUpTableMatchesTheDesign) {
  for (int question = 1; question <= preaching::IntroSection::QUESTION_COUNT; question++) {
    EXPECT_EQ(nextQuestion(question), expectedFollowUp(question)) << "question " << question;
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
