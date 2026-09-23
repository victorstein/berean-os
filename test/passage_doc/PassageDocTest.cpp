#include <ArduinoJson.h>
#include <gtest/gtest.h>

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

TEST(PassageDocUnlabelled, StoresAPassageWithNoTagsAsUnlabelled) {
  study::TaggedPassage p = samplePassage();
  p.tags.clear();
  study::PassageDoc doc;
  ASSERT_TRUE(doc.add(p)) << "marking a passage and labelling it are separate acts";
  EXPECT_EQ(doc.passages()[0].tags, (std::vector<study::TagId>{study::UNLABELLED}));
}

TEST(PassageDocUnlabelled, ARealTagReplacesUnlabelled) {
  study::TaggedPassage p = samplePassage();
  p.tags = {study::UNLABELLED, study::toTagId(4), study::UNLABELLED};
  study::PassageDoc doc;
  ASSERT_TRUE(doc.add(p));
  EXPECT_EQ(doc.passages()[0].tags, (std::vector<study::TagId>{study::toTagId(4)}));
}

TEST(PassageDocUnlabelled, UnlabelledDoesNotCountTowardsTheTagCap) {
  study::TaggedPassage p = samplePassage();
  p.tags = {study::UNLABELLED};
  for (uint16_t raw = 1; raw <= study::PassageDoc::MAX_TAGS_PER_PASSAGE; ++raw) p.tags.push_back(study::toTagId(raw));
  study::PassageDoc doc;
  ASSERT_TRUE(doc.add(p));
  EXPECT_EQ(doc.passages()[0].tags.size(), study::PassageDoc::MAX_TAGS_PER_PASSAGE);
  EXPECT_EQ(doc.passages()[0].tags.back(), study::toTagId(study::PassageDoc::MAX_TAGS_PER_PASSAGE));
}

TEST(PassageDocUnlabelled, SerialisesAsTheEmptyTagArrayEveryV1BuildAlreadyReads) {
  study::TaggedPassage p = samplePassage();
  p.tags.clear();
  study::PassageDoc doc;
  ASSERT_TRUE(doc.add(p));

  JsonDocument json;
  doc.toJson(json);
  EXPECT_EQ(json["v"].as<int>(), 1) << "unlabelled needs no new format: v1 already stores a tagless passage";
  ASSERT_TRUE(json["p"][0]["t"].is<JsonArray>());
  EXPECT_EQ(json["p"][0]["t"].size(), 0u) << "the reserved id never reaches the card";

  study::PassageDoc back;
  ASSERT_TRUE(back.fromJson(json.as<JsonVariantConst>()));
  ASSERT_EQ(back.passages().size(), 1u);
  EXPECT_EQ(back.passages()[0].tags, (std::vector<study::TagId>{study::UNLABELLED}));
}

TEST(PassageDocUnlabelled, LoadsAStoredZeroIdAsUnlabelled) {
  JsonDocument json;
  json["v"] = study::PassageDoc::FORMAT_VERSION;
  const auto rows = json["p"].to<JsonArray>();
  const auto onlyZero = rows.add<JsonObject>();
  onlyZero["u"] = "v:19:119:145:0";
  onlyZero["t"].to<JsonArray>().add(0);
  const auto zeroAndReal = rows.add<JsonObject>();
  zeroAndReal["u"] = "v:19:119:146:0";
  const auto tags = zeroAndReal["t"].to<JsonArray>();
  tags.add(0);
  tags.add(5);

  study::PassageDoc doc;
  ASSERT_TRUE(doc.fromJson(json.as<JsonVariantConst>()));
  ASSERT_EQ(doc.passages().size(), 2u);
  EXPECT_EQ(doc.passages()[0].tags, (std::vector<study::TagId>{study::UNLABELLED}));
  EXPECT_EQ(doc.passages()[1].tags, (std::vector<study::TagId>{study::toTagId(5)}));
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
  EXPECT_EQ(doc.passages()[0].tags, (std::vector<study::TagId>{study::UNLABELLED}));
  EXPECT_EQ(doc.unlabelledCount(), 1u);
}

TEST(PassageDocRemove, RemovingUnlabelledEverywhereIsANoOp) {
  study::TaggedPassage p = samplePassage();
  p.tags.clear();
  study::PassageDoc doc;
  ASSERT_TRUE(doc.add(p));
  doc.removeTagEverywhere(study::UNLABELLED);
  EXPECT_EQ(doc.passages()[0].tags, (std::vector<study::TagId>{study::UNLABELLED}));
}

TEST(PassageDocSetTags, UntaggingToZeroLeavesThePassageUnlabelled) {
  study::PassageDoc doc;
  ASSERT_TRUE(doc.add(samplePassage()));
  EXPECT_TRUE(doc.setTags(0, {}));
  ASSERT_EQ(doc.passages().size(), 1u) << "untagging is not deleting; the passage stays marked";
  EXPECT_EQ(doc.passages()[0].tags, (std::vector<study::TagId>{study::UNLABELLED}));
}

TEST(PassageDocSetTags, RefusesAnOutOfRangeIndex) {
  study::PassageDoc doc;
  ASSERT_TRUE(doc.add(samplePassage()));
  EXPECT_FALSE(doc.setTags(1, {study::toTagId(9)}));
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

study::TaggedPassage passageAt(const uint16_t chapter, const uint16_t verse, std::string reference) {
  study::TaggedPassage p = samplePassage();
  p.start = study::Unit{study::UnitKind::Verse, 19, chapter, verse, 0};
  p.end = p.start;
  p.reference = std::move(reference);
  return p;
}

TEST(PassageDocLinks, LinksTheSourceToTheTargetsStartUnitNotItsIndex) {
  study::PassageDoc doc;
  ASSERT_TRUE(doc.add(passageAt(119, 145, "Salmos 119:145")));
  ASSERT_TRUE(doc.add(passageAt(23, 1, "Salmos 23:1")));

  EXPECT_EQ(doc.linkPassages(0, 1), study::PassageDoc::LinkResult::Linked);
  ASSERT_EQ(doc.passages()[0].links.size(), 1u);
  const auto& link = doc.passages()[0].links[0];
  EXPECT_EQ(link.target, doc.passages()[1].start);
  EXPECT_EQ(link.targetSpine, doc.passages()[1].documentSpine);
  EXPECT_EQ(link.label, "Salmos 23:1");
  EXPECT_TRUE(doc.passages()[1].links.empty()) << "links are directed: the target is not touched";
}

TEST(PassageDocLinks, SurvivesTheTargetBeingDeletedAndReAdded) {
  study::PassageDoc doc;
  ASSERT_TRUE(doc.add(passageAt(119, 145, "Salmos 119:145")));
  ASSERT_TRUE(doc.add(passageAt(23, 1, "Salmos 23:1")));
  ASSERT_EQ(doc.linkPassages(0, 1), study::PassageDoc::LinkResult::Linked);

  ASSERT_TRUE(doc.remove(1));
  ASSERT_EQ(doc.passages()[0].links.size(), 1u) << "a dangling link is kept: it may resolve again";
  ASSERT_TRUE(doc.add(passageAt(23, 1, "Salmos 23:1")));
  EXPECT_EQ(doc.passages()[0].links[0].target, doc.passages()[1].start);
}

TEST(PassageDocLinks, LabelsATargetWithNoReferenceByItsSnippet) {
  study::PassageDoc doc;
  study::TaggedPassage target = samplePassage();
  target.start = study::Unit{study::UnitKind::Paragraph, 0, 0, 40, 0};
  target.reference.clear();
  target.snippet = std::string(200, 'a');
  ASSERT_TRUE(doc.add(samplePassage()));
  ASSERT_TRUE(doc.add(target));

  ASSERT_EQ(doc.linkPassages(0, 1), study::PassageDoc::LinkResult::Linked);
  EXPECT_EQ(doc.passages()[0].links[0].label, std::string(study::PassageDoc::MAX_REFERENCE_BYTES, 'a'));
}

TEST(PassageDocLinks, RefusesALinkToItself) {
  study::PassageDoc doc;
  ASSERT_TRUE(doc.add(passageAt(119, 145, "Salmos 119:145")));
  ASSERT_TRUE(doc.add(passageAt(119, 145, "Salmos 119:145")));
  EXPECT_EQ(doc.linkPassages(0, 0), study::PassageDoc::LinkResult::SelfLink);
  EXPECT_EQ(doc.linkPassages(0, 1), study::PassageDoc::LinkResult::SelfLink)
      << "a second mark at the same word is the same place";
  EXPECT_TRUE(doc.passages()[0].links.empty());
}

TEST(PassageDocLinks, ReportsAnExistingLinkWithoutDuplicatingIt) {
  study::PassageDoc doc;
  ASSERT_TRUE(doc.add(passageAt(119, 145, "Salmos 119:145")));
  ASSERT_TRUE(doc.add(passageAt(23, 1, "Salmos 23:1")));
  ASSERT_EQ(doc.linkPassages(0, 1), study::PassageDoc::LinkResult::Linked);
  EXPECT_EQ(doc.linkPassages(0, 1), study::PassageDoc::LinkResult::AlreadyLinked);
  EXPECT_EQ(doc.passages()[0].links.size(), 1u);
}

TEST(PassageDocLinks, RefusesAtTheCap) {
  study::PassageDoc doc;
  ASSERT_TRUE(doc.add(passageAt(119, 145, "Salmos 119:145")));
  for (uint16_t v = 1; v <= study::PassageDoc::MAX_LINKS_PER_PASSAGE + 1; ++v) {
    ASSERT_TRUE(doc.add(passageAt(23, v, "Salmos 23")));
  }
  for (size_t i = 1; i <= study::PassageDoc::MAX_LINKS_PER_PASSAGE; ++i) {
    ASSERT_EQ(doc.linkPassages(0, i), study::PassageDoc::LinkResult::Linked);
  }
  EXPECT_EQ(doc.linkPassages(0, study::PassageDoc::MAX_LINKS_PER_PASSAGE + 1), study::PassageDoc::LinkResult::AtCap);
  EXPECT_EQ(doc.passages()[0].links.size(), study::PassageDoc::MAX_LINKS_PER_PASSAGE);
}

TEST(PassageDocLinks, RefusesAnOutOfRangeIndex) {
  study::PassageDoc doc;
  ASSERT_TRUE(doc.add(samplePassage()));
  EXPECT_EQ(doc.linkPassages(0, 1), study::PassageDoc::LinkResult::NoSuchPassage);
  EXPECT_EQ(doc.linkPassages(1, 0), study::PassageDoc::LinkResult::NoSuchPassage);
}

TEST(PassageDocLinks, RemovesOneLinkAndKeepsTheRest) {
  study::PassageDoc doc;
  ASSERT_TRUE(doc.add(passageAt(119, 145, "Salmos 119:145")));
  ASSERT_TRUE(doc.add(passageAt(23, 1, "Salmos 23:1")));
  ASSERT_TRUE(doc.add(passageAt(23, 2, "Salmos 23:2")));
  ASSERT_EQ(doc.linkPassages(0, 1), study::PassageDoc::LinkResult::Linked);
  ASSERT_EQ(doc.linkPassages(0, 2), study::PassageDoc::LinkResult::Linked);

  EXPECT_TRUE(doc.removeLink(0, 0));
  ASSERT_EQ(doc.passages()[0].links.size(), 1u);
  EXPECT_EQ(doc.passages()[0].links[0].label, "Salmos 23:2");
  EXPECT_FALSE(doc.removeLink(0, 1));
  EXPECT_FALSE(doc.removeLink(3, 0));
}

TEST(PassageDocLinks, SetLinksRestoresTheOriginalOrder) {
  study::PassageDoc doc;
  ASSERT_TRUE(doc.add(passageAt(119, 145, "Salmos 119:145")));
  ASSERT_TRUE(doc.add(passageAt(23, 1, "Salmos 23:1")));
  ASSERT_TRUE(doc.add(passageAt(23, 2, "Salmos 23:2")));
  ASSERT_EQ(doc.linkPassages(0, 1), study::PassageDoc::LinkResult::Linked);
  ASSERT_EQ(doc.linkPassages(0, 2), study::PassageDoc::LinkResult::Linked);
  const std::vector<study::PassageLink> backup = doc.passages()[0].links;

  ASSERT_TRUE(doc.removeLink(0, 0));
  ASSERT_TRUE(doc.setLinks(0, backup));
  EXPECT_EQ(doc.passages()[0].links, backup);
  EXPECT_FALSE(doc.setLinks(3, backup));
}

TEST(PassageDocLinks, RoundTripsLinksUnderTheNewFormatVersion) {
  study::PassageDoc doc;
  ASSERT_TRUE(doc.add(passageAt(119, 145, "Salmos 119:145")));
  ASSERT_TRUE(doc.add(passageAt(23, 1, "Salmos 23:1")));
  ASSERT_EQ(doc.linkPassages(0, 1), study::PassageDoc::LinkResult::Linked);

  JsonDocument json;
  doc.toJson(json);
  EXPECT_EQ(json["v"].as<int>(), 2);

  study::PassageDoc back;
  ASSERT_TRUE(back.fromJson(json.as<JsonVariantConst>()));
  ASSERT_EQ(back.passages()[0].links.size(), 1u);
  EXPECT_EQ(back.passages()[0].links[0], doc.passages()[0].links[0]);
  EXPECT_TRUE(back.passages()[1].links.empty());
}

TEST(PassageDocLinks, WritesVersionOneWhileNoPassageCarriesALink) {
  study::PassageDoc doc;
  ASSERT_TRUE(doc.add(samplePassage()));
  JsonDocument json;
  doc.toJson(json);
  EXPECT_EQ(json["v"].as<int>(), 1) << "a file with nothing a v1 build would drop stays readable by one";
  EXPECT_FALSE(json["p"][0]["k"].is<JsonArray>());
}

// What a v1 build does with a file this build wrote: it must refuse the file
// outright. Reading it would ignore "k", and its next save would erase every link.
TEST(PassageDocLinks, AVersionOneReaderRefusesAFileCarryingLinks) {
  study::PassageDoc doc;
  ASSERT_TRUE(doc.add(passageAt(119, 145, "Salmos 119:145")));
  ASSERT_TRUE(doc.add(passageAt(23, 1, "Salmos 23:1")));
  ASSERT_EQ(doc.linkPassages(0, 1), study::PassageDoc::LinkResult::Linked);
  JsonDocument json;
  doc.toJson(json);

  constexpr int V1_FORMAT_VERSION = 1;
  const int version = json["v"] | 0;
  EXPECT_TRUE(version <= 0 || version > V1_FORMAT_VERSION) << "PassageDoc::fromJson's v1 guard must reject this";
}

TEST(PassageDocLinks, LoadCapsDedupesAndSkipsUnparseableLinks) {
  JsonDocument json;
  json["v"] = study::PassageDoc::FORMAT_VERSION;
  const auto row = json["p"].to<JsonArray>().add<JsonObject>();
  row["u"] = "v:19:119:145:0";
  const auto links = row["k"].to<JsonArray>();
  const auto bad = links.add<JsonObject>();
  bad["u"] = "nonsense";
  for (int i = 0; i < 2; ++i) {
    const auto dup = links.add<JsonObject>();
    dup["u"] = "v:19:23:1:0";
    dup["s"] = 7;
    dup["r"] = "Salmos 23:1";
  }
  for (uint16_t v = 2; v < 20; ++v) {
    const auto link = links.add<JsonObject>();
    link["u"] = study::unitToCompact(study::Unit{study::UnitKind::Verse, 19, 23, v, 0});
  }

  study::PassageDoc doc;
  ASSERT_TRUE(doc.fromJson(json.as<JsonVariantConst>()));
  const auto& loaded = doc.passages()[0].links;
  ASSERT_EQ(loaded.size(), study::PassageDoc::MAX_LINKS_PER_PASSAGE);
  EXPECT_EQ(loaded[0].target, (study::Unit{study::UnitKind::Verse, 19, 23, 1, 0}));
  EXPECT_EQ(loaded[0].targetSpine, 7u);
  EXPECT_EQ(loaded[1].target.minor, 2u);
}

TEST(PassageDocLinks, ReAddingABackupKeepsItsLinks) {
  study::PassageDoc doc;
  ASSERT_TRUE(doc.add(passageAt(119, 145, "Salmos 119:145")));
  ASSERT_TRUE(doc.add(passageAt(23, 1, "Salmos 23:1")));
  ASSERT_EQ(doc.linkPassages(0, 1), study::PassageDoc::LinkResult::Linked);
  const study::TaggedPassage backup = doc.passages()[0];
  ASSERT_TRUE(doc.remove(0));
  ASSERT_TRUE(doc.add(backup)) << "StudyStore::removePassage rolls back a failed save this way";
  EXPECT_EQ(doc.passages().back().links, backup.links);
}

// The budget argument for the cap: even a passage with every field at its
// maximum -- every string made of quotes, so each byte serialises as two -- and
// every link slot full is small enough that the write budget holds well over the
// user's real store (63 passages) of them.
TEST(PassageDocBudget, AFullyLinkedWorstCasePassageCannotExhaustTheBudgetInOrdinaryUse) {
  study::TaggedPassage worst;
  worst.start = study::Unit{study::UnitKind::Verse, 66, UINT16_MAX, UINT16_MAX, UINT32_MAX};
  worst.end = worst.start;
  worst.fingerprint = study::Fingerprint{UINT16_MAX, UINT32_MAX};
  worst.endFingerprint = worst.fingerprint;
  worst.document = std::string(64, 'd');
  worst.documentSpine = UINT16_MAX;
  worst.snippet = std::string(study::PassageDoc::MAX_SNIPPET_BYTES, '"');
  worst.reference = std::string(study::PassageDoc::MAX_REFERENCE_BYTES, '"');
  worst.pendingUpgrade = true;
  for (uint16_t t = 1; t <= study::PassageDoc::MAX_TAGS_PER_PASSAGE; ++t) {
    worst.tags.push_back(study::toTagId(static_cast<uint16_t>(UINT16_MAX - t)));
  }
  for (uint32_t l = 0; l < study::PassageDoc::MAX_LINKS_PER_PASSAGE; ++l) {
    study::PassageLink link;
    link.target = study::Unit{study::UnitKind::Verse, 66, UINT16_MAX, UINT16_MAX, UINT32_MAX - l};
    link.targetSpine = UINT16_MAX;
    link.label = std::string(study::PassageDoc::MAX_REFERENCE_BYTES, '"');
    worst.links.push_back(link);
  }

  study::PassageDoc doc;
  ASSERT_TRUE(doc.add(worst));
  const size_t worstBytes = doc.measureBytes();
  EXPECT_LT(worstBytes, 1700u);
  EXPECT_GE(study::PassageDoc::SAVE_BYTE_BUDGET / worstBytes, 100u);
}

TEST(PassageDocBudget, TheUsersRealStoreFullyLinkedUsesUnderAQuarterOfTheBudget) {
  study::PassageDoc doc;
  for (uint16_t v = 1; v <= 63; ++v) ASSERT_TRUE(doc.add(passageAt(119, v, "Salmos 119:145")));
  for (size_t source = 0; source < 63; ++source) {
    for (size_t k = 1; k <= study::PassageDoc::MAX_LINKS_PER_PASSAGE; ++k) {
      ASSERT_EQ(doc.linkPassages(source, (source + k) % 63), study::PassageDoc::LinkResult::Linked);
    }
  }
  EXPECT_LT(doc.measureBytes(), study::PassageDoc::SAVE_BYTE_BUDGET / 4);
}

}  // namespace
