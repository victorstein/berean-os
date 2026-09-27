#include <gtest/gtest.h>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "activities/boot_sleep/StudySleepPick.h"

using study_sleep::passageKey;

TEST(StudySleepKey, MatchesTheFnv1a32ReferenceVectors) {
  EXPECT_EQ(study_sleep::fnv1a32(study_sleep::FNV_OFFSET_BASIS, ""), 0x811c9dc5u);
  EXPECT_EQ(study_sleep::fnv1a32(study_sleep::FNV_OFFSET_BASIS, "a"), 0xe40c292cu);
}

TEST(StudySleepKey, SeparatesPublicationFromUnit) {
  EXPECT_NE(passageKey("ab", "c", "c"), passageKey("a", "bc", "c"));
}

TEST(StudySleepKey, PassagesSharingAStartButNotAnEndDiffer) {
  EXPECT_NE(passageKey("bible", "v:43:3:16", "v:43:3:16"), passageKey("bible", "v:43:3:16", "v:43:3:17"));
}

TEST(StudySleepKey, IsStableForTheSamePassage) {
  EXPECT_EQ(passageKey("w-202607-S", "p:12", "p:12"), passageKey("w-202607-S", "p:12", "p:12"));
}

TEST(StudySleepKey, AnAbsentOrUnreadableEndKeysAsTheStart) {
  EXPECT_EQ(study_sleep::endUnitOrStart("p:12", "", false), "p:12");
  EXPECT_EQ(study_sleep::endUnitOrStart("p:12", "garbage", false), "p:12");
  EXPECT_EQ(study_sleep::endUnitOrStart("p:12", "p:14", true), "p:14");
  EXPECT_EQ(passageKey("w", "p:12", study_sleep::endUnitOrStart("p:12", "", false)), passageKey("w", "p:12", "p:12"));
}

TEST(StudySleepFileName, APassageFileYieldsItsPubKey) {
  EXPECT_EQ(study_sleep::pubKeyFromFileName("bible.json"), std::optional<std::string_view>("bible"));
  EXPECT_EQ(study_sleep::pubKeyFromFileName("w-202607-S.json"), std::optional<std::string_view>("w-202607-S"));
}

TEST(StudySleepFileName, SkipsTempHiddenAndForeignFiles) {
  EXPECT_EQ(study_sleep::pubKeyFromFileName("bible.json.tmp"), std::nullopt);
  EXPECT_EQ(study_sleep::pubKeyFromFileName(".bible.json"), std::nullopt);
  EXPECT_EQ(study_sleep::pubKeyFromFileName(".json"), std::nullopt);
  EXPECT_EQ(study_sleep::pubKeyFromFileName("notes.txt"), std::nullopt);
  EXPECT_EQ(study_sleep::pubKeyFromFileName(""), std::nullopt);
}

namespace {

constexpr uint8_t RING = 16;

study_sleep::RingView view(const uint32_t* keys, const uint8_t pos, const uint8_t fill) {
  return study_sleep::RingView{keys, RING, pos, fill};
}

}  // namespace

TEST(StudySleepRing, AnEmptyRingHoldsNothing) {
  const uint32_t keys[RING] = {};
  EXPECT_EQ(study_sleep::ageOf(view(keys, 0, 0), 0u), std::nullopt);
}

TEST(StudySleepRing, AgeCountsBackFromTheNewest) {
  uint32_t keys[RING] = {};
  keys[0] = 10;
  keys[1] = 20;
  const auto ring = view(keys, 2, 2);
  EXPECT_EQ(study_sleep::ageOf(ring, 20u), std::optional<uint8_t>(0));
  EXPECT_EQ(study_sleep::ageOf(ring, 10u), std::optional<uint8_t>(1));
}

TEST(StudySleepRing, SlotsBeyondFillAreIgnored) {
  const uint32_t keys[RING] = {10};
  EXPECT_EQ(study_sleep::ageOf(view(keys, 1, 1), 0u), std::nullopt);
}

TEST(StudySleepRing, WrapsAroundThePosition) {
  uint32_t keys[RING] = {};
  keys[0] = 7;
  keys[RING - 1] = 8;
  const auto ring = view(keys, 1, RING);
  EXPECT_EQ(study_sleep::ageOf(ring, 7u), std::optional<uint8_t>(0));
  EXPECT_EQ(study_sleep::ageOf(ring, 8u), std::optional<uint8_t>(1));
}

TEST(StudySleepRing, AKeyHeldTwiceReportsItsNewestShowing) {
  uint32_t keys[RING] = {};
  keys[0] = 5;
  keys[1] = 5;
  keys[2] = 9;
  EXPECT_EQ(study_sleep::ageOf(view(keys, 3, 3), 5u), std::optional<uint8_t>(1));
}

namespace {

struct ScriptedRandom {
  std::vector<uint32_t> values;
  size_t next = 0;
  std::vector<uint32_t> bounds;
};

uint32_t scripted(void* ctx, const uint32_t bound) {
  auto* script = static_cast<ScriptedRandom*>(ctx);
  script->bounds.push_back(bound);
  const uint32_t value = script->next < script->values.size() ? script->values[script->next++] : 0;
  return value % bound;
}

std::string pickFrom(ScriptedRandom& script, const std::vector<std::pair<std::string, std::optional<uint8_t>>>& offers) {
  study_sleep::Sampler sampler(&scripted, &script);
  uint32_t key = 1;
  for (const auto& [snippet, age] : offers) sampler.offer(snippet, "", 0, key++, age);
  const auto* chosen = sampler.result();
  return chosen ? std::string(chosen->snippet) : std::string("<none>");
}

}  // namespace

TEST(StudySleepSampler, ReplacesThePickWithProbabilityOneInK) {
  const std::vector<std::pair<std::string, std::optional<uint8_t>>> four = {
      {"A", std::nullopt}, {"B", std::nullopt}, {"C", std::nullopt}, {"D", std::nullopt}};
  ScriptedRandom keepFirst{{0, 1, 1, 1}, 0, {}};
  EXPECT_EQ(pickFrom(keepFirst, four), "A");
  EXPECT_EQ(keepFirst.bounds, (std::vector<uint32_t>{1, 2, 3, 4}));
  ScriptedRandom takeSecond{{0, 0, 1, 1}, 0, {}};
  EXPECT_EQ(pickFrom(takeSecond, four), "B");
  ScriptedRandom takeThird{{0, 1, 0, 1}, 0, {}};
  EXPECT_EQ(pickFrom(takeThird, four), "C");
  ScriptedRandom takeLast{{0, 1, 1, 0}, 0, {}};
  EXPECT_EQ(pickFrom(takeLast, four), "D");
}

TEST(StudySleepSampler, NeverPicksARecentPassageWhileAnotherExists) {
  for (uint32_t roll = 0; roll < 4; ++roll) {
    ScriptedRandom script{{roll, roll, roll}, 0, {}};
    EXPECT_EQ(pickFrom(script, {{"shown", 0}, {"fresh", std::nullopt}}), "fresh");
  }
}

TEST(StudySleepSampler, ShowsTheOnlyPassageAgain) {
  ScriptedRandom script;
  EXPECT_EQ(pickFrom(script, {{"only", 0}}), "only");
}

TEST(StudySleepSampler, WhenAllAreRecentTheLeastRecentlyShownWins) {
  ScriptedRandom script;
  EXPECT_EQ(pickFrom(script, {{"newest", 0}, {"oldest", 3}, {"middle", 1}}), "oldest");
  ScriptedRandom reversed;
  EXPECT_EQ(pickFrom(reversed, {{"middle", 1}, {"oldest", 3}, {"newest", 0}}), "oldest");
}

TEST(StudySleepSampler, AnEqualAgeTieKeepsTheFirstSeen) {
  ScriptedRandom script;
  EXPECT_EQ(pickFrom(script, {{"first", 2}, {"second", 2}}), "first");
}

TEST(StudySleepSampler, NothingOfferedPicksNothing) {
  ScriptedRandom script;
  EXPECT_EQ(pickFrom(script, {}), "<none>");
}

TEST(StudySleepSampler, KeepsTheWinnersFields) {
  ScriptedRandom script;
  study_sleep::Sampler sampler(&scripted, &script);
  sampler.offer("In the beginning", "Genesis 1:1", 7, 0xABCDu, std::nullopt);
  const auto* chosen = sampler.result();
  ASSERT_NE(chosen, nullptr);
  EXPECT_STREQ(chosen->snippet, "In the beginning");
  EXPECT_STREQ(chosen->reference, "Genesis 1:1");
  EXPECT_EQ(chosen->tag, 7);
  EXPECT_EQ(chosen->key, 0xABCDu);
}

TEST(StudySleepSampler, TruncatesAnOverlongSnippetToItsCapacity) {
  ScriptedRandom script;
  study_sleep::Sampler sampler(&scripted, &script);
  const std::string longSnippet(study_sleep::SNIPPET_CAPACITY + 20, 'x');
  sampler.offer(longSnippet, "", 0, 1, std::nullopt);
  EXPECT_EQ(std::string(sampler.result()->snippet).size(), study_sleep::SNIPPET_CAPACITY - 1);
}
