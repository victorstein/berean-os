#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <string>

#include "activities/boot_sleep/StudySleepFit.h"

namespace {

// Four sizes, largest first, each a fixed number of pixels per byte.
struct PerByte {
  int px[4];
};

int measure(const void* ctx, const uint8_t sizeIndex, const char* text) {
  return static_cast<int>(strlen(text)) * static_cast<const PerByte*>(ctx)->px[sizeIndex];
}

const PerByte WIDTHS{{10, 8, 6, 4}};
const study_sleep::FitSize SIZES[] = {{50}, {40}, {30}, {20}};
constexpr uint8_t SIZE_COUNT = 4;
constexpr int WIDTH = 100;
const std::string TEN_WORDS = "a000 a001 a002 a003 a004 a005 a006 a007 a008 a009";

study_sleep::FitResult fit(const std::string& text, const int maxHeight) {
  return study_sleep::fitPassage(text, SIZES, SIZE_COUNT, WIDTH, maxHeight, &measure, &WIDTHS);
}

TEST(StudySleepFit, AShortTextKeepsTheLargestSize) {
  const auto result = fit("uno dos", 200);
  EXPECT_EQ(result.sizeIndex, 0);
  ASSERT_EQ(result.lines.size(), 1u);
  EXPECT_EQ(result.lines[0], "uno dos");
  EXPECT_FALSE(result.ellipsized);
}

// 10 px/B: 2 words a line, 5 lines x 50 = 250. 8 px/B: 2 a line, 5 x 40 = 200.
// 6 px/B: 3 a line, 4 x 30 = 120. 4 px/B: 5 a line, 2 x 20 = 40.
TEST(StudySleepFit, PicksTheLargestSizeThatFitsNotTheSmallest) {
  const auto result = fit(TEN_WORDS, 150);
  EXPECT_EQ(result.sizeIndex, 2);
  ASSERT_EQ(result.lines.size(), 4u);
  EXPECT_EQ(result.lines[0], "a000 a001 a002");
  EXPECT_EQ(result.lines[3], "a009");
  EXPECT_FALSE(result.ellipsized);
}

TEST(StudySleepFit, NoEllipsisWhenTheTextJustFits) {
  const auto result = fit("abcd abcd", 50);
  EXPECT_EQ(result.sizeIndex, 0);
  EXPECT_FALSE(result.ellipsized);
}

// At 4 px/B only one 20 px line fits in 25 px. "a000 a001 a002 a003 a004" + the
// 3-byte ellipsis is 27 B = 108 px, so the last word is dropped: 22 B = 88 px.
TEST(StudySleepFit, OnlyTheSmallestSizeEllipsizesAndOnAWordBoundary) {
  const auto result = fit(TEN_WORDS, 25);
  EXPECT_EQ(result.sizeIndex, SIZE_COUNT - 1);
  ASSERT_EQ(result.lines.size(), 1u);
  EXPECT_EQ(result.lines[0], "a000 a001 a002 a003\xE2\x80\xA6");
  EXPECT_TRUE(result.ellipsized);
}

TEST(StudySleepFit, AnEllipsizedTextStartsAtItsFirstWordAndKeepsWholeWords) {
  const auto result = fit(TEN_WORDS, 45);
  ASSERT_EQ(result.lines.size(), 2u);
  EXPECT_EQ(result.lines[0].rfind("a000", 0), 0u);
  std::string joined = result.lines[0] + " " + result.lines[1];
  joined.resize(joined.size() - strlen(study_sleep::FIT_ELLIPSIS));
  EXPECT_EQ(TEN_WORDS.rfind(joined, 0), 0u) << "every kept word is whole and in order";
}

TEST(StudySleepFit, AWordTooWideForALargerSizeMovesToASmallerOne) {
  const auto result = fit("abcdefghijklmnopqrst", 200);  // 20 B: 200, 160, 120, 80 px
  EXPECT_EQ(result.sizeIndex, 3);
  ASSERT_EQ(result.lines.size(), 1u);
  EXPECT_EQ(result.lines[0], "abcdefghijklmnopqrst");
}

TEST(StudySleepFit, AWordTooWideForEverySizeStaysWhole) {
  const auto result = fit("abcdefghijklmnopqrstuvwxyz", 200);  // 104 px even at 4 px/B
  EXPECT_EQ(result.sizeIndex, 3);
  ASSERT_EQ(result.lines.size(), 1u);
  EXPECT_EQ(result.lines[0], "abcdefghijklmnopqrstuvwxyz");
  EXPECT_FALSE(result.ellipsized);
}

TEST(StudySleepFit, NoRoomStillShowsOneLine) {
  const auto shortText = fit("a000 a001", 0);
  ASSERT_EQ(shortText.lines.size(), 1u);
  EXPECT_FALSE(shortText.ellipsized);

  const auto longText = fit(TEN_WORDS, 0);
  ASSERT_EQ(longText.lines.size(), 1u);
  EXPECT_TRUE(longText.ellipsized);
}

TEST(StudySleepFit, BreaksOnlyOnAsciiSpace) {
  const auto result =
      fit("a\xE2\x80\xAF"
          "b c",
          200);  // U+202F joins a and b
  ASSERT_EQ(result.lines.size(), 1u);
  EXPECT_EQ(result.lines[0],
            "a\xE2\x80\xAF"
            "b c");
}

TEST(StudySleepFit, AnEmptyTextHasNoLines) {
  EXPECT_TRUE(fit("", 200).lines.empty());
  EXPECT_TRUE(fit("   ", 200).lines.empty());
}

}  // namespace
