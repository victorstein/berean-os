#include <ArduinoJson.h>
#include <gtest/gtest.h>

#include "StudyStore/TagPalette.h"

namespace {

TEST(TagPaletteAdd, AllocatesAscendingIdsAndReturnsTheExistingIdForARepeat) {
  study::TagPalette p;
  const auto a = p.add("oracion");
  const auto b = p.add("perdon");
  ASSERT_TRUE(a.has_value());
  ASSERT_TRUE(b.has_value());
  EXPECT_LT(study::toRaw(*a), study::toRaw(*b));
  EXPECT_EQ(p.add("oracion"), a);
  EXPECT_EQ(p.activeCount(), 2u);
}

// The user's only accented tag name is "transformación". Matching must not fold
// accents: "transformacion" and "transformación" are different words, and
// merging them silently would rewrite a name the user chose.
TEST(TagPaletteAdd, DoesNotFoldAccents) {
  study::TagPalette p;
  const auto a = p.add("transformación");
  const auto b = p.add("transformacion");
  ASSERT_TRUE(a.has_value());
  ASSERT_TRUE(b.has_value());
  EXPECT_NE(a, b);
}

TEST(TagPaletteAdd, MatchesExactlyIncludingCase) {
  study::TagPalette p;
  EXPECT_NE(p.add("Amor"), p.add("amor"));
}

TEST(TagPaletteAdd, RejectsAnEmptyName) { EXPECT_FALSE(study::TagPalette{}.add("").has_value()); }

TEST(TagPaletteRetire, KeepsTheIdOutOfCirculationForever) {
  study::TagPalette p;
  const auto a = p.add("odio");
  ASSERT_TRUE(a.has_value());
  p.retire(*a);
  EXPECT_EQ(p.activeCount(), 0u);
  EXPECT_FALSE(p.isActive(*a));

  // Re-adding the same NAME revives its own id rather than allocating a new one.
  EXPECT_EQ(p.add("odio"), a);

  // A different name must never receive the retired id.
  study::TagPalette q;
  const auto gone = q.add("odio");
  q.retire(*gone);
  const auto fresh = q.add("amor");
  ASSERT_TRUE(fresh.has_value());
  EXPECT_NE(fresh, gone) << "reusing a retired id rebinds every passage that still carries it";
}

TEST(TagPaletteRetire, StillResolvesARetiredNameForDisplay) {
  study::TagPalette p;
  const auto a = p.add("odio");
  p.retire(*a);
  EXPECT_EQ(p.name(*a), "odio") << "a passage still carrying it must not render as a blank chip";
}

TEST(TagPaletteRoundTrip, PreservesIdsNamesTombstonesAndNextId) {
  study::TagPalette p;
  const auto keep = p.add("fe");
  const auto gone = p.add("temporal");
  p.retire(*gone);
  const auto after = p.add("esperanza");

  JsonDocument doc;
  p.toJson(doc);

  study::TagPalette q;
  ASSERT_TRUE(q.fromJson(doc.as<JsonVariantConst>()));
  EXPECT_EQ(q.name(*keep), "fe");
  EXPECT_EQ(q.name(*gone), "temporal");
  EXPECT_FALSE(q.isActive(*gone));
  EXPECT_EQ(q.name(*after), "esperanza");

  const auto next = q.add("nueva");
  ASSERT_TRUE(next.has_value());
  EXPECT_EQ(study::toRaw(*next), static_cast<uint16_t>(study::toRaw(*after) + 1))
      << "nextTagId must survive the round trip";
}

TEST(TagPaletteRoundTrip, RejectsAFutureFormatVersion) {
  JsonDocument doc;
  doc["v"] = study::TagPalette::FORMAT_VERSION + 1;
  study::TagPalette q;
  EXPECT_FALSE(q.fromJson(doc.as<JsonVariantConst>()))
      << "reinterpreting a newer file is how an OTA rollback destroys data";
}

TEST(TagPaletteVersion, AnAbsentVersionIsRefused) {
  JsonDocument doc;
  doc["t"].to<JsonArray>();
  study::TagPalette q;
  EXPECT_FALSE(q.fromJson(doc.as<JsonVariantConst>())) << "the palette has always required \"v\"";
}

TEST(TagPaletteVersion, APresentZeroIsRefused) {
  JsonDocument doc;
  doc["v"] = 0;
  doc["t"].to<JsonArray>();
  study::TagPalette q;
  EXPECT_FALSE(q.fromJson(doc.as<JsonVariantConst>()));
}

TEST(TagPaletteVersion, ANegativeVersionIsRefused) {
  JsonDocument doc;
  doc["v"] = -1;
  doc["t"].to<JsonArray>();
  study::TagPalette q;
  EXPECT_FALSE(q.fromJson(doc.as<JsonVariantConst>()));
}

TEST(TagPaletteRoundTrip, RecoversNextIdFromTheHighestSeenWhenTheFieldIsMissing) {
  JsonDocument doc;
  doc["v"] = study::TagPalette::FORMAT_VERSION;
  const auto tags = doc["t"].to<JsonArray>();
  const auto row = tags.add<JsonObject>();
  row["i"] = 40;
  row["n"] = "ley";

  study::TagPalette q;
  ASSERT_TRUE(q.fromJson(doc.as<JsonVariantConst>()));
  const auto next = q.add("nueva");
  ASSERT_TRUE(next.has_value());
  EXPECT_GT(study::toRaw(*next), 40) << "a hand-edited file must not be able to hand out a live id";
}

TEST(TagPaletteBudget, RefusesBeyondTheTagCeiling) {
  study::TagPalette p;
  for (size_t i = 0; i < study::TagPalette::MAX_ACTIVE_TAGS; ++i) {
    ASSERT_TRUE(p.add("t" + std::to_string(i)).has_value()) << "at i=" << i;
  }
  EXPECT_FALSE(p.add("one too many").has_value());
}

TEST(TagPaletteRoundTrip, HoldsTheUsersRealPaletteSize) {
  study::TagPalette p;
  for (int i = 0; i < 48; ++i) ASSERT_TRUE(p.add("etiqueta" + std::to_string(i)).has_value());
  JsonDocument doc;
  p.toJson(doc);
  EXPECT_LT(measureJson(doc), 4096u) << "the palette must stay comfortably inside any store budget";
}

}  // namespace
