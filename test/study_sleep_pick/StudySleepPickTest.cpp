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
