// The Home verse's daily pick: the date seeds the sampler, so one day always
// yields the same passage and different days yield independent ones (issue
// #203, decision d1). Also that a pick carries where it opens.

#include <gtest/gtest.h>

#include <cstdint>
#include <cstdio>
#include <map>
#include <string>

#include "activities/boot_sleep/StudySleepPick.h"
#include "util/CivilDate.h"

namespace {

constexpr int OFFERS = 10;

std::string pickFor(uint32_t seed) {
  study_sleep::Sampler sampler(&study_sleep::splitmixDraw, &seed);
  for (int i = 0; i < OFFERS; ++i) {
    char text[8];
    snprintf(text, sizeof(text), "p%d", i);
    sampler.offer(text, "", 0, static_cast<uint32_t>(i + 1), std::nullopt, true);
  }
  const auto* chosen = sampler.result();
  return chosen ? chosen->text : std::string("<none>");
}

CivilDate date(const int year, const int month, const int day) {
  CivilDate d;
  d.year = static_cast<uint16_t>(year);
  d.month = static_cast<uint8_t>(month);
  d.day = static_cast<uint8_t>(day);
  return d;
}

}  // namespace

TEST(StudySleepSeed, TheSeedIsTheHashOfTheEightDigits) {
  EXPECT_EQ(study_sleep::dailySeed(date(2026, 9, 30)), study_sleep::fnv1a32(study_sleep::FNV_OFFSET_BASIS, "20260930"));
  EXPECT_EQ(study_sleep::dailySeed(date(2027, 1, 5)), study_sleep::fnv1a32(study_sleep::FNV_OFFSET_BASIS, "20270105"));
}

TEST(StudySleepSeed, TheSameDateGivesTheSamePick) {
  const uint32_t seed = study_sleep::dailySeed(date(2026, 9, 30));
  EXPECT_EQ(pickFor(seed), pickFor(seed));
}

TEST(StudySleepSeed, DifferentDatesGiveIndependentPicks) {
  const int64_t first = daysFromCivil(2026, 9, 1);
  std::map<std::string, int> timesPicked;
  std::string previous;
  int adjacentDiffer = 0;
  constexpr int DAYS = 30;
  for (int i = 0; i < DAYS; ++i) {
    const std::string pick = pickFor(study_sleep::dailySeed(civilFromDays(first + i)));
    ++timesPicked[pick];
    if (i > 0 && pick != previous) ++adjacentDiffer;
    previous = pick;
  }
  for (const auto& [pick, count] : timesPicked) EXPECT_LE(count, DAYS / 2) << pick << " dominates the month";
  EXPECT_GT(adjacentDiffer, (DAYS - 1) / 2) << "consecutive days repeat too often";
}

TEST(StudySleepSeed, TheDrawStaysInBounds) {
  uint32_t state = study_sleep::dailySeed(date(2026, 9, 30));
  for (uint32_t bound = 1; bound <= 1000; ++bound) EXPECT_LT(study_sleep::splitmixDraw(&state, bound), bound);
}

TEST(StudySleepSeed, TheDrawAdvancesItsState) {
  uint32_t state = 42;
  study_sleep::splitmixDraw(&state, 10);
  EXPECT_NE(state, 42u);
}

TEST(StudySleepSampler, KeepsWhereThePickOpens) {
  uint32_t seed = 1;
  study_sleep::Sampler sampler(&study_sleep::splitmixDraw, &seed);
  study::Unit start;
  start.kind = study::UnitKind::Paragraph;
  start.minor = 40;
  sampler.offer("A study note", "Study note", 0, 7, std::nullopt, true, start, 1234);
  const auto* chosen = sampler.result();
  ASSERT_NE(chosen, nullptr);
  EXPECT_EQ(chosen->start, start);
  EXPECT_EQ(chosen->spine, 1234);
}

TEST(StudySleepSampler, AnOfferWithoutAStartKeepsTheDefaults) {
  uint32_t seed = 1;
  study_sleep::Sampler sampler(&study_sleep::splitmixDraw, &seed);
  sampler.offer("text", "", 0, 7, std::nullopt, true);
  ASSERT_NE(sampler.result(), nullptr);
  EXPECT_EQ(sampler.result()->start, study::Unit{});
  EXPECT_EQ(sampler.result()->spine, 0);
}

TEST(StudySleepRowText, APrefilterLimitCanBeNarrowed) {
  EXPECT_TRUE(study_sleep::withinPrefilter(std::string(512, 'a'), 512));
  EXPECT_FALSE(study_sleep::withinPrefilter(std::string(513, 'a'), 512));
}
