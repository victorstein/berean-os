#include <ArduinoJson.h>
#include <gtest/gtest.h>

#include <cstdint>
#include <initializer_list>
#include <string>
#include <vector>

#include "StudyStore/TagPalette.h"

namespace {

struct Row {
  uint16_t id;
  const char* name;
  bool retired = false;
};

JsonDocument paletteJson(std::initializer_list<Row> rows) {
  JsonDocument doc;
  doc["v"] = study::TagPalette::FORMAT_VERSION;
  const auto tags = doc["t"].to<JsonArray>();
  for (const Row& r : rows) {
    const auto row = tags.add<JsonObject>();
    row["i"] = r.id;
    row["n"] = r.name;
    if (r.retired) row["r"] = true;
  }
  return doc;
}

study::TagPalette loaded(std::initializer_list<Row> rows) {
  const JsonDocument doc = paletteJson(rows);
  study::TagPalette p;
  EXPECT_TRUE(p.fromJson(doc.as<JsonVariantConst>()));
  return p;
}

std::vector<uint16_t> rawIds(const std::vector<study::TagId>& ids) {
  std::vector<uint16_t> out;
  out.reserve(ids.size());
  for (const study::TagId id : ids) out.push_back(study::toRaw(id));
  return out;
}

std::vector<std::string> namesInDisplayOrder(const study::TagPalette& p) {
  std::vector<std::string> out;
  for (const study::TagId id : p.activeIdsByName()) out.push_back(p.name(id));
  return out;
}

using Names = std::vector<std::string>;

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

TEST(TagPaletteOrder, IgnoresCase) {
  study::TagPalette p;
  p.add("beta");
  p.add("alfa");
  p.add("Alfa");
  EXPECT_EQ(rawIds(p.activeIdsByName()), (std::vector<uint16_t>{3, 2, 1}))
      << "equal folded keys tie on the raw bytes, so 'Alfa' precedes 'alfa' though allocated later";
}

TEST(TagPaletteOrder, IgnoresAccents) {
  study::TagPalette p;
  p.add("éxodo");
  p.add("esperanza");
  p.add("Ezequiel");
  EXPECT_EQ(namesInDisplayOrder(p), (Names{"esperanza", "éxodo", "Ezequiel"}));
}

TEST(TagPaletteOrder, FoldsEnyeIntoN) {
  study::TagPalette p;
  p.add("anzuelo");
  p.add("año");
  EXPECT_EQ(namesInDisplayOrder(p), (Names{"año", "anzuelo"}));

  study::TagPalette q;
  q.add("nube");
  q.add("ñu");
  EXPECT_EQ(namesInDisplayOrder(q), (Names{"ñu", "nube"}));
}

TEST(TagPaletteOrder, SkipsOneLeadingInvertedMark) {
  study::TagPalette p;
  p.add("reino");
  p.add("¿Por qué?");
  p.add("paz");
  p.add("¡Ánimo!");
  EXPECT_EQ(namesInDisplayOrder(p), (Names{"¡Ánimo!", "paz", "¿Por qué?", "reino"}))
      << "unskipped, both marks pass through the fold and sort after z";
}

TEST(TagPaletteOrder, SkipsOnlyTheFirstMark) {
  study::TagPalette p;
  p.add("¿¡Hola");
  p.add("zeta");
  EXPECT_EQ(namesInDisplayOrder(p), (Names{"zeta", "¿¡Hola"}));
}

// Accepted with D2: digits sort as text.
TEST(TagPaletteOrder, SortsDigitsAsText) {
  study::TagPalette p;
  p.add("2 Reyes");
  p.add("10 mandamientos");
  EXPECT_EQ(namesInDisplayOrder(p), (Names{"10 mandamientos", "2 Reyes"}));
}

// fromJson dedupes by id only, so a file can hold two entries with one name.
TEST(TagPaletteOrder, BreaksANameTieOnTheId) {
  const study::TagPalette p = loaded({{7, "fe"}, {3, "fe"}});
  EXPECT_EQ(rawIds(p.activeIdsByName()), (std::vector<uint16_t>{3, 7}));
}

TEST(TagPaletteOrder, BreaksATruncatedNameTieOnTheId) {
  const study::TagPalette p = loaded({{9, "abcdefghijklmnopqrstuvwxBBBBBB"}, {4, "abcdefghijklmnopqrstuvwxAAAAAA"}});
  EXPECT_EQ(rawIds(p.activeIdsByName()), (std::vector<uint16_t>{4, 9}))
      << "fromJson cuts both names to the same 24 bytes";
}

TEST(TagPaletteOrder, ExcludesRetiredTags) {
  study::TagPalette p;
  p.add("gamma");
  const auto beta = p.add("beta");
  p.add("alfa");
  p.retire(*beta);
  EXPECT_EQ(namesInDisplayOrder(p), (Names{"alfa", "gamma"}));
}

TEST(TagPaletteOrder, LeavesActiveIdsInAllocationOrder) {
  study::TagPalette p;
  p.add("zeta");
  p.add("alfa");
  EXPECT_EQ(rawIds(p.activeIds()), (std::vector<uint16_t>{1, 2}));
  EXPECT_EQ(rawIds(p.activeIdsByName()), (std::vector<uint16_t>{2, 1}));
}

TEST(TagPaletteOrder, IsEmptyForAnEmptyPalette) {
  EXPECT_TRUE(study::TagPalette{}.activeIdsByName().empty());
  EXPECT_TRUE(loaded({}).activeIdsByName().empty());
}

TEST(TagPaletteRank, OrdersRetiredIds) {
  study::TagPalette p;
  const auto gamma = p.add("gamma");
  const auto beta = p.add("beta");
  const auto alfa = p.add("alfa");
  p.retire(*beta);
  EXPECT_LT(p.nameRank(*alfa), p.nameRank(*beta));
  EXPECT_LT(p.nameRank(*beta), p.nameRank(*gamma)) << "a passage still carrying a retired tag lists it in place";
}

TEST(TagPaletteRank, IsMaxForUnlabelledAndUnknownIds) {
  study::TagPalette p;
  p.add("alfa");
  EXPECT_EQ(p.nameRank(study::UNLABELLED), UINT16_MAX);
  EXPECT_EQ(p.nameRank(study::toTagId(999)), UINT16_MAX);
}

TEST(TagPaletteRank, IsRebuiltByAnAppendingAdd) {
  study::TagPalette p;
  p.add("mu");
  p.add("alfa");
  EXPECT_EQ(namesInDisplayOrder(p), (Names{"alfa", "mu"}));
}

TEST(TagPaletteRank, IsRebuiltByFromJson) {
  study::TagPalette p;
  p.add("mu");
  const JsonDocument doc = paletteJson({{1, "zeta"}, {2, "alfa"}});
  ASSERT_TRUE(p.fromJson(doc.as<JsonVariantConst>()));
  EXPECT_EQ(namesInDisplayOrder(p), (Names{"alfa", "zeta"}));
}

TEST(TagPaletteRank, SurvivesACopy) {
  study::TagPalette p;
  const auto zeta = p.add("zeta");
  const auto mu = p.add("mu");
  const auto alfa = p.add("alfa");
  p.retire(*mu);

  const study::TagPalette q = p;
  EXPECT_EQ(rawIds(q.activeIdsByName()), rawIds(p.activeIdsByName()));
  for (const auto id : {*zeta, *mu, *alfa}) EXPECT_EQ(q.nameRank(id), p.nameRank(id));
}

TEST(TagPaletteRank, RetireAndReviveLeaveRanksUnchanged) {
  study::TagPalette p;
  const auto gamma = p.add("gamma");
  const auto alfa = p.add("alfa");
  const auto beta = p.add("beta");
  const std::vector<uint16_t> before{p.nameRank(*gamma), p.nameRank(*alfa), p.nameRank(*beta)};

  p.retire(*alfa);
  EXPECT_EQ((std::vector<uint16_t>{p.nameRank(*gamma), p.nameRank(*alfa), p.nameRank(*beta)}), before);

  EXPECT_EQ(p.add("alfa"), alfa);
  EXPECT_EQ((std::vector<uint16_t>{p.nameRank(*gamma), p.nameRank(*alfa), p.nameRank(*beta)}), before);
  EXPECT_EQ(namesInDisplayOrder(p), (Names{"alfa", "beta", "gamma"}));
}

}  // namespace
