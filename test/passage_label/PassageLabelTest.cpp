#include <gtest/gtest.h>

#include <cstring>
#include <string>
#include <vector>

#include "activities/reader/PassageLabel.h"

namespace {

void feed(passage_label::Builder& label, const std::vector<std::string>& words) {
  for (const auto& word : words) label.addWord(word.c_str(), word.size());
}

TEST(PassageLabel, JoinsWordsWithSingleSpaces) {
  passage_label::Builder label(384);
  feed(label, {"Ustedes,", "los", "que"});
  EXPECT_EQ(label.text(), "Ustedes, los que");
  EXPECT_FALSE(label.truncated());
}

TEST(PassageLabel, SkipsEmptyWords) {
  passage_label::Builder label(384);
  feed(label, {"ley,", "", "están"});
  EXPECT_EQ(label.text(), "ley, están");
}

// PassageSelectActivity feeds the anchor's page from the anchor to its end before
// a page turn discards it, then the final page from its top to the end word.
TEST(PassageLabel, ASelectionAcrossAPageTurnKeepsItsStart) {
  const std::vector<std::string> anchorPage = {"Cristo",   "nos", "libertó.",   "Ustedes,", "los",      "que",
                                               "intentan", "ser", "declarados", "justos",   "mediante", "la"};
  const std::vector<std::string> finalPage = {"ley,", "están", "separados", "de", "Cristo.", "Se", "han"};
  const size_t anchorIndex = 3;
  const size_t endIndex = 4;

  passage_label::Builder label(384);
  feed(label, std::vector<std::string>(anchorPage.begin() + anchorIndex, anchorPage.end()));
  feed(label, std::vector<std::string>(finalPage.begin(), finalPage.begin() + endIndex + 1));

  EXPECT_EQ(label.text(),
            "Ustedes, los que intentan ser declarados justos mediante la ley, están separados de Cristo.");
}

TEST(PassageLabel, StopsOnAWordBoundaryAndEndsTheLastWordWithAnEllipsis) {
  passage_label::Builder label(20);
  feed(label, {"Ustedes", "los", "que", "intentan", "ser"});
  EXPECT_TRUE(label.truncated());
  EXPECT_EQ(label.text(), "Ustedes los que\xE2\x80\xA6");
  EXPECT_LE(label.text().size(), 20u);
}

TEST(PassageLabel, IgnoresEveryWordAfterTheCut) {
  passage_label::Builder label(20);
  feed(label, {"Ustedes", "los", "que", "intentan", "a"});
  EXPECT_EQ(label.text(), "Ustedes los que\xE2\x80\xA6") << "a short word after the cut must not be appended";
}

TEST(PassageLabel, NeverExceedsTheCapacity) {
  passage_label::Builder label(384);
  for (int i = 0; i < 200; ++i) label.addWord("transformación", strlen("transformación"));
  EXPECT_TRUE(label.truncated());
  EXPECT_LE(label.text().size(), 384u);
}

TEST(PassageLabel, ResetStartsAFreshLabel) {
  passage_label::Builder label(20);
  feed(label, {"Ustedes", "los", "que", "intentan"});
  label.reset();
  feed(label, {"ley,"});
  EXPECT_EQ(label.text(), "ley,");
  EXPECT_FALSE(label.truncated());
}

}  // namespace
