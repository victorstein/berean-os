#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

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
constexpr int LINE_HEIGHTS[] = {50, 40, 30, 20};
constexpr int WIDTH = 100;
const std::string TEN_WORDS = "a000 a001 a002 a003 a004 a005 a006 a007 a008 a009";

std::vector<study_sleep::FitRung> ladder(const int maxHeight) {
  std::vector<study_sleep::FitRung> rungs;
  for (uint8_t size = 0; size < 4; ++size) rungs.push_back({size, LINE_HEIGHTS[size], maxHeight});
  return rungs;
}

study_sleep::FitResult fit(const std::string& text, const int maxHeight) {
  const auto rungs = ladder(maxHeight);
  return study_sleep::fitPassage(text, rungs.data(), static_cast<uint8_t>(rungs.size()), WIDTH, &measure, &WIDTHS);
}

TEST(StudySleepFit, AShortTextKeepsTheFirstRung) {
  const auto result = fit("uno dos", 200);
  ASSERT_TRUE(result.fits);
  EXPECT_EQ(result.rung, 0);
  ASSERT_EQ(result.lines.size(), 1u);
  EXPECT_EQ(result.lines[0], "uno dos");
}

// 10 px/B: 2 words a line, 5 lines x 50 = 250. 8 px/B: 2 a line, 5 x 40 = 200.
// 6 px/B: 3 a line, 4 x 30 = 120. 4 px/B: 5 a line, 2 x 20 = 40.
TEST(StudySleepFit, PicksTheFirstRungThatFitsNotTheLast) {
  const auto result = fit(TEN_WORDS, 150);
  ASSERT_TRUE(result.fits);
  EXPECT_EQ(result.rung, 2);
  ASSERT_EQ(result.lines.size(), 4u);
  EXPECT_EQ(result.lines[0], "a000 a001 a002");
  EXPECT_EQ(result.lines[3], "a009");
}

TEST(StudySleepFit, ATextThatFitsNoRungDoesNotFitAndHasNoLines) {
  const auto result = fit(TEN_WORDS, 25);
  EXPECT_FALSE(result.fits);
  EXPECT_TRUE(result.lines.empty()) << "never a cut passage";
}

TEST(StudySleepFit, NeverEndsOnAnEllipsisAndAlwaysKeepsEveryWord) {
  for (int height = 0; height <= 300; height += 5) {
    const auto result = fit(TEN_WORDS, height);
    if (!result.fits) continue;
    std::string joined;
    for (const auto& line : result.lines) {
      EXPECT_EQ(line.find("\xE2\x80\xA6"), std::string::npos);
      if (!joined.empty()) joined += ' ';
      joined += line;
    }
    EXPECT_EQ(joined, TEN_WORDS) << "at height " << height;
  }
}

TEST(StudySleepFit, ALaterRungWithMoreRoomWins) {
  // Same 4 px/B size twice: shedding chrome gives the second rung twice the height.
  const study_sleep::FitRung rungs[] = {{3, 20, 20}, {3, 20, 40}};
  const auto result = study_sleep::fitPassage(TEN_WORDS, rungs, 2, WIDTH, &measure, &WIDTHS);
  ASSERT_TRUE(result.fits);
  EXPECT_EQ(result.rung, 1);
  EXPECT_EQ(result.lines.size(), 2u);
}

TEST(StudySleepFit, TheFloorRungAloneDecidesWhetherAPassageCanBeShown) {
  const study_sleep::FitRung floor{3, 20, 40};
  EXPECT_TRUE(study_sleep::fitPassage(TEN_WORDS, &floor, 1, WIDTH, &measure, &WIDTHS).fits);
  const study_sleep::FitRung cramped{3, 20, 20};
  EXPECT_FALSE(study_sleep::fitPassage(TEN_WORDS, &cramped, 1, WIDTH, &measure, &WIDTHS).fits);
}

TEST(StudySleepFit, AWordTooWideForALargerSizeMovesToASmallerOne) {
  const auto result = fit("abcdefghijklmnopqrst", 200);  // 20 B: 200, 160, 120, 80 px
  ASSERT_TRUE(result.fits);
  EXPECT_EQ(result.rung, 3);
  ASSERT_EQ(result.lines.size(), 1u);
  EXPECT_EQ(result.lines[0], "abcdefghijklmnopqrst");
}

TEST(StudySleepFit, AWordTooWideForEverySizeDoesNotFit) {
  EXPECT_FALSE(fit("abcdefghijklmnopqrstuvwxyz", 200).fits);  // 104 px even at 4 px/B
}

TEST(StudySleepFit, NoRoomFitsNothing) { EXPECT_FALSE(fit("a000 a001", 0).fits); }

TEST(StudySleepFit, BreaksOnlyOnAsciiSpace) {
  const auto result =
      fit("a\xE2\x80\xAF"
          "b c",
          200);  // U+202F joins a and b
  ASSERT_TRUE(result.fits);
  ASSERT_EQ(result.lines.size(), 1u);
  EXPECT_EQ(result.lines[0],
            "a\xE2\x80\xAF"
            "b c");
}

TEST(StudySleepFit, AnEmptyTextDoesNotFit) {
  EXPECT_FALSE(fit("", 200).fits);
  EXPECT_FALSE(fit("   ", 200).fits);
}

}  // namespace
