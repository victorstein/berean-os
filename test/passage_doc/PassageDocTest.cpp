#include <gtest/gtest.h>

#include <ArduinoJson.h>

#include <algorithm>

#include "StudyStore/PassageDoc.h"

namespace {

study::TaggedPassage samplePassage() {
  study::TaggedPassage p;
  p.start = study::Unit{study::UnitKind::Verse, 19, 119, 145, 0};
  p.end = study::Unit{study::UnitKind::Verse, 19, 119, 145, 108};
  p.fingerprint = study::Fingerprint{114, 0xa1b2c3d4};
  p.document = "1001061130-split10.xhtml";
  p.documentSpine = 198;
  p.snippet = "Te he llamado con todo el corazon";
  p.reference = "Salmos 119:145";
  p.tags = {study::toTagId(3), study::toTagId(17)};
  return p;
}

TEST(PassageDocRoundTrip, PreservesEveryFieldOfAPassage) {
  study::PassageDoc doc;
  ASSERT_TRUE(doc.add(samplePassage()));

  JsonDocument json;
  doc.toJson(json);

  study::PassageDoc back;
  ASSERT_TRUE(back.fromJson(json.as<JsonVariantConst>()));
  ASSERT_EQ(back.passages().size(), 1u);
  const auto& p = back.passages()[0];
  const auto o = samplePassage();
  EXPECT_EQ(p.start, o.start);
  EXPECT_EQ(p.end, o.end);
  EXPECT_EQ(p.fingerprint, o.fingerprint);
  EXPECT_EQ(p.document, o.document);
  EXPECT_EQ(p.documentSpine, o.documentSpine);
  EXPECT_EQ(p.snippet, o.snippet);
  EXPECT_EQ(p.reference, o.reference);
  EXPECT_EQ(p.tags, o.tags);
}

TEST(PassageDocRoundTrip, RejectsAFutureFormatVersion) {
  JsonDocument json;
  json["v"] = study::PassageDoc::FORMAT_VERSION + 1;
  study::PassageDoc doc;
  EXPECT_FALSE(doc.fromJson(json.as<JsonVariantConst>()));
}

TEST(PassageDocRoundTrip, DropsAPassageWhoseStartUnitIsUnparseable) {
  JsonDocument json;
  json["v"] = study::PassageDoc::FORMAT_VERSION;
  const auto rows = json["p"].to<JsonArray>();
  const auto row = rows.add<JsonObject>();
  row["u"] = "nonsense";
  row["e"] = "v:19:119:145:0";

  study::PassageDoc doc;
  ASSERT_TRUE(doc.fromJson(json.as<JsonVariantConst>()));
  EXPECT_TRUE(doc.passages().empty()) << "an unaddressable passage cannot be painted or listed";
}

TEST(PassageDocValidation, TruncatesAnOverlongSnippetWithoutSplittingACodepoint) {
  study::TaggedPassage p = samplePassage();
  // Accented Spanish, so a raw byte cut lands mid-sequence. An ASCII fixture
  // here cannot fail and so proves nothing.
  std::string accented;
  while (accented.size() < 400) accented += "transformación ";
  p.snippet = accented;
  p.reference = std::string("Génesis 1:1 ") + accented;

  study::PassageDoc doc;
  ASSERT_TRUE(doc.add(p));
  const auto& stored = doc.passages()[0];
  EXPECT_LE(stored.snippet.size(), study::PassageDoc::MAX_SNIPPET_BYTES);
  EXPECT_LE(stored.reference.size(), study::PassageDoc::MAX_REFERENCE_BYTES);

  for (const std::string& text : {stored.snippet, stored.reference}) {
    ASSERT_FALSE(text.empty());
    const unsigned char last = static_cast<unsigned char>(text.back());
    EXPECT_FALSE((last & 0xC0u) == 0x80u) << "truncation left a dangling continuation byte";
    EXPECT_FALSE((last & 0xE0u) == 0xC0u) << "truncation left a lead byte with no continuation";
  }
}

TEST(PassageDocValidation, DedupesAndCapsTags) {
  study::TaggedPassage p = samplePassage();
  p.tags.clear();
  for (const uint16_t raw : {5, 5, 5, 1, 2, 3, 4, 6, 7, 8, 9, 10, 11, 12}) p.tags.push_back(study::toTagId(raw));
  study::PassageDoc doc;
  ASSERT_TRUE(doc.add(p));
  const auto& tags = doc.passages()[0].tags;
  EXPECT_LE(tags.size(), study::PassageDoc::MAX_TAGS_PER_PASSAGE);
  EXPECT_EQ(std::count(tags.begin(), tags.end(), study::toTagId(5)), 1);
}

TEST(PassageDocValidation, RefusesAPassageWithNoTags) {
  study::TaggedPassage p = samplePassage();
  p.tags.clear();
  study::PassageDoc doc;
  EXPECT_FALSE(doc.add(p)) << "a highlight exists only to carry tags; all 63 of the user's do";
}

TEST(PassageDocRemove, RemovingTheLastTagKeepsThePassage) {
  study::PassageDoc doc;
  ASSERT_TRUE(doc.add(samplePassage()));  // tags {3, 17}

  doc.removeTagEverywhere(study::toTagId(3));
  ASSERT_EQ(doc.passages().size(), 1u);
  EXPECT_EQ(doc.passages()[0].tags, (std::vector<study::TagId>{study::toTagId(17)}));

  doc.removeTagEverywhere(study::toTagId(17));
  ASSERT_EQ(doc.passages().size(), 1u)
      << "a palette edit must never destroy a passage: TagFilterActivity deletes a tag on a long-press";
  EXPECT_EQ(doc.untaggedCount(), 1u);
}

TEST(PassageDocSetTags, LeavesThePassageUntouchedWhenTheNewListIsEmpty) {
  study::PassageDoc doc;
  ASSERT_TRUE(doc.add(samplePassage()));
  EXPECT_FALSE(doc.setTags(0, {}));
  ASSERT_EQ(doc.passages().size(), 1u) << "a refused setTags must not consume the passage";
  EXPECT_EQ(doc.passages()[0].tags.size(), 2u);
}

TEST(PassageDocSetTags, DoesNotReorderTheDocument) {
  study::PassageDoc doc;
  study::TaggedPassage second = samplePassage();
  second.reference = "Salmos 119:146";
  ASSERT_TRUE(doc.add(samplePassage()));
  ASSERT_TRUE(doc.add(second));

  ASSERT_TRUE(doc.setTags(0, {study::toTagId(9)}));
  EXPECT_EQ(doc.passages()[0].reference, "Salmos 119:145")
      << "the UI and every stored index address passages positionally";
}

TEST(PassageDocBudget, RefusesAnAddThatWouldExceedTheWriteBudget) {
  study::PassageDoc doc;
  size_t added = 0;
  while (doc.add(samplePassage())) ++added;
  ASSERT_GT(added, 0u);
  EXPECT_LE(doc.measureBytes(), study::PassageDoc::SAVE_BYTE_BUDGET);
}

TEST(PassageDocLoad, ReportsFailureRatherThanTruncatingAnOversizeDocument) {
  JsonDocument json;
  json["v"] = study::PassageDoc::FORMAT_VERSION;
  const auto rows = json["p"].to<JsonArray>();
  for (size_t i = 0; i < 4000; ++i) {
    const auto row = rows.add<JsonObject>();
    row["u"] = "v:19:119:145:0";
    row["e"] = "v:19:119:145:108";
    row["x"] = std::string(100, 'x');
    const auto tags = row["t"].to<JsonArray>();
    tags.add(3);
  }

  study::PassageDoc doc;
  EXPECT_FALSE(doc.fromJson(json.as<JsonVariantConst>()))
      << "an oversize document is a load failure the caller must refuse to save over";
}

TEST(PassageDocBudget, HoldsTheUsersRealStoreWithRoomToSpare) {
  study::PassageDoc doc;
  for (int i = 0; i < 63; ++i) ASSERT_TRUE(doc.add(samplePassage()));
  EXPECT_LT(doc.measureBytes(), 30000u) << "63 passages is the user's real store today";
}

TEST(PassageDocFind, LocatesPassagesByDocument) {
  study::PassageDoc doc;
  study::TaggedPassage other = samplePassage();
  other.document = "1001061131-split1.xhtml";
  ASSERT_TRUE(doc.add(samplePassage()));
  ASSERT_TRUE(doc.add(other));

  const auto hits = doc.findByDocument("1001061130-split10.xhtml");
  ASSERT_EQ(hits.size(), 1u);
  EXPECT_EQ(hits[0], 0u);
}

}  // namespace
