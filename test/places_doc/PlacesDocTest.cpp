// Host coverage for /.berean/places.json's format and list rules. PlacesDoc is free of
// <Arduino.h> for exactly this; the storage shell is covered by test/places_store.

#include <ArduinoJson.h>
#include <SaveBudget.h>
#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <vector>

#include "util/PlacesDoc.h"

namespace {

using study::Unit;
using study::UnitKind;

Place makePlace(const uint8_t book, const uint16_t chapter, const uint16_t verse, const char* reference,
                const bool chapterOnly = false) {
  Place p;
  p.unit = Unit{UnitKind::Verse, book, chapter, verse, 0};
  p.reference = reference;
  p.chapterOnly = chapterOnly;
  p.spineIndex = 100;
  p.visibleTextOffset = 0;
  return p;
}

bool parse(const char* json, std::vector<Place>& places, bool& needsResave) {
  JsonDocument doc;
  EXPECT_FALSE(deserializeJson(doc, json)) << json;
  return PlacesDoc::fromJson(doc.as<JsonVariantConst>(), places, needsResave);
}

study::DocumentUnits genesisOne() {
  study::DocumentUnits units;
  units.kind = UnitKind::Verse;
  units.book = 1;
  units.anchors = {{50, 1, 1}, {200, 1, 2}};
  return units;
}

}  // namespace

// ---- budget ----

TEST(PlacesDocBudget, OneEntryMeasuresTheNamedOverheads) {
  Place place = makePlace(1, 1, 1, "");
  place.spineIndex = 0;
  JsonDocument doc;
  PlacesDoc::toJson({place}, doc);
  // "v:1:1:1:0" is 9 bytes, false 5, the zero spine and offset one each.
  EXPECT_EQ(measureJson(doc), PlacesDoc::DOC_WRAPPER_BYTES + PlacesDoc::ENTRY_OVERHEAD_BYTES + 9 + 5 + 1 + 1);
  EXPECT_EQ(measureJson(doc), 65u);
}

TEST(PlacesDocBudget, AWorstCaseDocumentMeasuresExactlyTheBudget) {
  std::vector<Place> places;
  for (size_t i = 0; i < PlacesDoc::MAX_PLACES; ++i) {
    Place p;
    p.unit = Unit{UnitKind::Verse, 255, 65535, 65535, UINT32_MAX};
    p.reference = std::string(PlacesDoc::MAX_REFERENCE_BYTES, '"');  // every byte escapes to two
    p.chapterOnly = false;
    p.spineIndex = UINT16_MAX;
    p.visibleTextOffset = UINT32_MAX;
    places.push_back(p);
  }
  JsonDocument doc;
  PlacesDoc::toJson(places, doc);
  EXPECT_EQ(measureJson(doc), PlacesDoc::SAVE_BUDGET);
  EXPECT_EQ(PlacesDoc::SAVE_BUDGET, 2118u) << "derived from the field caps; recompute, do not tidy";
}

TEST(PlacesDocBudget, StaysWithinTheIssuesCapAndUnderTheDefault) {
  EXPECT_LE(PlacesDoc::SAVE_BUDGET, 4096u);
  EXPECT_LT(PlacesDoc::SAVE_BUDGET, persist::DEFAULT_SAVE_BUDGET);
}

// ---- version ----

TEST(PlacesDocVersion, ToJsonStampsTheCurrentVersion) {
  JsonDocument doc;
  PlacesDoc::toJson({}, doc);
  EXPECT_EQ(doc["v"].as<int>(), PlacesDoc::FORMAT_VERSION);
}

TEST(PlacesDocVersion, AFutureVersionIsRefusedAndLeavesTheListUntouched) {
  std::vector<Place> places{makePlace(1, 1, 1, "Genesis 1", true)};
  bool needsResave = false;
  EXPECT_FALSE(parse(R"({"v":2,"places":[]})", places, needsResave));
  ASSERT_EQ(places.size(), 1u);
  EXPECT_EQ(places[0].reference, "Genesis 1");
}

TEST(PlacesDocVersion, AnAbsentVersionIsRefused) {
  std::vector<Place> places;
  bool needsResave = false;
  EXPECT_FALSE(parse(R"({"places":[]})", places, needsResave));
}

TEST(PlacesDocVersion, AZeroVersionIsRefused) {
  std::vector<Place> places;
  bool needsResave = false;
  EXPECT_FALSE(parse(R"({"v":0,"places":[]})", places, needsResave));
}

// ---- load ----

TEST(PlacesDoc, RoundTripsEveryField) {
  Place revelation = makePlace(66, 21, 4, "Revelation 21:4");
  revelation.unit.offset = 17;
  revelation.spineIndex = 1201;
  revelation.visibleTextOffset = 2310;
  const std::vector<Place> original{revelation, makePlace(1, 1, 1, "Genesis 1", true)};

  JsonDocument doc;
  PlacesDoc::toJson(original, doc);
  std::vector<Place> loaded;
  bool needsResave = true;
  ASSERT_TRUE(PlacesDoc::fromJson(doc.as<JsonVariantConst>(), loaded, needsResave));
  EXPECT_FALSE(needsResave);
  EXPECT_EQ(loaded, original);
}

TEST(PlacesDoc, DropsEntriesThatAreNotBiblePlaces) {
  std::vector<Place> places;
  bool needsResave = false;
  ASSERT_TRUE(parse(R"({"v":1,"places":[
      {"u":"v:66:21:4:0","r":"Revelation 21:4"},
      {"u":"p:0:0:40:0","r":"paragraph"},
      {"u":"v:0:1:1:0","r":"book zero"},
      {"u":"v:67:1:1:0","r":"book 67"},
      {"u":"v:1:0:1:0","r":"chapter zero"},
      {"u":"garbage","r":"garbage"},
      {"r":"no unit"},
      {"u":"v:66:21:9:0","r":"Revelation 21:9, a second entry for the chapter"}]})",
                    places, needsResave));
  ASSERT_EQ(places.size(), 1u);
  EXPECT_EQ(places[0].reference, "Revelation 21:4");
  EXPECT_TRUE(needsResave);
}

TEST(PlacesDoc, CapsTheCountAtTwelve) {
  std::string json = R"({"v":1,"places":[)";
  for (int chapter = 1; chapter <= 14; ++chapter) {
    if (chapter > 1) json += ',';
    json += R"({"u":"v:19:)" + std::to_string(chapter) + R"(:1:0","r":"Psalm"})";
  }
  json += "]}";
  std::vector<Place> places;
  bool needsResave = false;
  ASSERT_TRUE(parse(json.c_str(), places, needsResave));
  EXPECT_EQ(places.size(), PlacesDoc::MAX_PLACES);
  EXPECT_EQ(places.front().unit.major, 1);
  EXPECT_TRUE(needsResave);
}

TEST(PlacesDoc, AnOutOfRangeSpineHintReadsAsZero) {
  std::vector<Place> places;
  bool needsResave = false;
  ASSERT_TRUE(parse(R"({"v":1,"places":[{"u":"v:1:1:1:0","r":"Genesis 1","s":70000}]})", places, needsResave));
  ASSERT_EQ(places.size(), 1u);
  EXPECT_EQ(places[0].spineIndex, 0);
  EXPECT_TRUE(needsResave);
}

TEST(PlacesDoc, ReBoundsAnOverlongReferenceOnACodepointBoundary) {
  std::string longReference;
  for (int i = 0; i < 30; ++i) longReference += "\xc3\xa9";  // é, 60 bytes
  const std::string json = R"({"v":1,"places":[{"u":"v:1:1:1:0","r":")" + longReference + R"("}]})";
  std::vector<Place> places;
  bool needsResave = false;
  ASSERT_TRUE(parse(json.c_str(), places, needsResave));
  ASSERT_EQ(places.size(), 1u);
  EXPECT_LE(places[0].reference.size(), PlacesDoc::MAX_REFERENCE_BYTES);
  EXPECT_EQ(places[0].reference.size() % 2, 0u) << "cut inside a two-byte sequence";
  EXPECT_TRUE(needsResave);
}

// ---- record ----

TEST(PlacesDocRecord, ANewPlaceGoesToTheFront) {
  std::vector<Place> places{makePlace(1, 1, 1, "Genesis 1", true)};
  EXPECT_TRUE(PlacesDoc::record(places, makePlace(66, 21, 4, "Revelation 21:4")));
  ASSERT_EQ(places.size(), 2u);
  EXPECT_EQ(places[0].reference, "Revelation 21:4");
  EXPECT_EQ(places[1].reference, "Genesis 1");
}

TEST(PlacesDocRecord, AnotherVerseInTheSameChapterReplacesAndMovesToFront) {
  std::vector<Place> places{makePlace(1, 1, 1, "Genesis 1", true), makePlace(66, 21, 4, "Revelation 21:4")};
  EXPECT_TRUE(PlacesDoc::record(places, makePlace(66, 21, 9, "Revelation 21:9")));
  ASSERT_EQ(places.size(), 2u);
  EXPECT_EQ(places[0].reference, "Revelation 21:9");
  EXPECT_EQ(places[1].reference, "Genesis 1");
}

TEST(PlacesDocRecord, EvictsTheOldestPastTwelve) {
  std::vector<Place> places;
  for (uint16_t chapter = 1; chapter <= 13; ++chapter) {
    PlacesDoc::record(places, makePlace(19, chapter, 1, "Psalm"));
  }
  ASSERT_EQ(places.size(), PlacesDoc::MAX_PLACES);
  EXPECT_EQ(places.front().unit.major, 13);
  EXPECT_EQ(places.back().unit.major, 2);
}

TEST(PlacesDocRecord, RecordingTheHeadAgainChangesNothing) {
  std::vector<Place> places{makePlace(66, 21, 4, "Revelation 21:4"), makePlace(1, 1, 1, "Genesis 1", true)};
  EXPECT_FALSE(PlacesDoc::record(places, makePlace(66, 21, 4, "Revelation 21:4")));
  EXPECT_EQ(places[0].reference, "Revelation 21:4");
  EXPECT_EQ(places.size(), 2u);
}

TEST(PlacesDocRecord, NormalisesTheReference) {
  std::vector<Place> places;
  Place withNul = makePlace(1, 1, 1, "");
  withNul.reference = std::string("Gene\0sis 1", 10);
  EXPECT_TRUE(PlacesDoc::record(places, withNul));
  EXPECT_EQ(places[0].reference, "Genesis 1");
}

// ---- placeUnit ----

TEST(PlacesDocPlaceUnit, APageInsideAVerseIsThatVerse) {
  const auto place = PlacesDoc::placeUnit(genesisOne(), 210);
  ASSERT_TRUE(place.has_value());
  EXPECT_EQ(place->unit, (Unit{UnitKind::Verse, 1, 1, 2, 10}));
  EXPECT_FALSE(place->chapterOnly);
}

TEST(PlacesDocPlaceUnit, APageBeforeTheFirstVerseIsTheChapter) {
  const auto place = PlacesDoc::placeUnit(genesisOne(), 10);
  ASSERT_TRUE(place.has_value());
  EXPECT_EQ(place->unit, (Unit{UnitKind::Verse, 1, 1, 1, 0}));
  EXPECT_TRUE(place->chapterOnly);
}

TEST(PlacesDocPlaceUnit, OnlyAnIndexedVerseDocumentIsAPlace) {
  study::DocumentUnits paragraphs = genesisOne();
  paragraphs.kind = UnitKind::Paragraph;
  EXPECT_FALSE(PlacesDoc::placeUnit(paragraphs, 210).has_value());

  study::DocumentUnits noBook = genesisOne();
  noBook.book = 0;
  EXPECT_FALSE(PlacesDoc::placeUnit(noBook, 210).has_value());

  study::DocumentUnits noAnchors = genesisOne();
  noAnchors.anchors.clear();
  EXPECT_FALSE(PlacesDoc::placeUnit(noAnchors, 210).has_value());

  EXPECT_FALSE(PlacesDoc::placeUnit(study::DocumentUnits{}, 210).has_value());
}

// ---- pickRecent ----

TEST(PlacesDocPickRecent, SkipsEveryVerseOfTheChapterOnScreenAndKeepsOrder) {
  const std::vector<Place> places{makePlace(66, 21, 4, "Revelation 21:4"), makePlace(1, 1, 1, "Genesis 1", true),
                                  makePlace(19, 83, 18, "Psalm 83:18")};
  Place out[3];
  const size_t n = PlacesDoc::pickRecent(places, Unit{UnitKind::Verse, 1, 1, 5, 0}, out, 3);
  ASSERT_EQ(n, 2u);
  EXPECT_EQ(out[0].reference, "Revelation 21:4");
  EXPECT_EQ(out[1].reference, "Psalm 83:18");
}

TEST(PlacesDocPickRecent, TheCapAppliesAfterTheSkip) {
  const std::vector<Place> places{makePlace(1, 1, 1, "Genesis 1", true), makePlace(66, 21, 4, "Revelation 21:4"),
                                  makePlace(19, 83, 18, "Psalm 83:18"), makePlace(23, 40, 31, "Isaiah 40:31")};
  Place out[2];
  const size_t n = PlacesDoc::pickRecent(places, Unit{UnitKind::Verse, 1, 1, 1, 0}, out, 2);
  ASSERT_EQ(n, 2u);
  EXPECT_EQ(out[0].reference, "Revelation 21:4");
  EXPECT_EQ(out[1].reference, "Psalm 83:18");
}

TEST(PlacesDocPickRecent, WithNothingOnScreenTakesTheNewest) {
  const std::vector<Place> places{makePlace(66, 21, 4, "Revelation 21:4"), makePlace(1, 1, 1, "Genesis 1", true)};
  Place out[3];
  EXPECT_EQ(PlacesDoc::pickRecent(places, std::nullopt, out, 3), 2u);
  EXPECT_EQ(out[0].reference, "Revelation 21:4");
}

// ---- display ----

TEST(PlacesDocDisplay, AVerseReferenceCarriesTheVerse) {
  EXPECT_EQ(PlacesDoc::formatReference("Revelation", Unit{UnitKind::Verse, 66, 21, 4, 0}, false), "Revelation 21:4");
}

TEST(PlacesDocDisplay, AChapterReferenceDropsTheVerse) {
  EXPECT_EQ(PlacesDoc::formatReference("Genesis", Unit{UnitKind::Verse, 1, 1, 1, 0}, true), "Genesis 1");
}

TEST(PlacesDocDisplay, ChipLabelsUseTheAbbreviation) {
  char label[PlacesDoc::MAX_REFERENCE_BYTES + 1];
  PlacesDoc::formatChipLabel("Rev.", makePlace(66, 21, 4, "Revelation 21:4"), label, sizeof(label));
  EXPECT_STREQ(label, "Rev. 21:4");
  PlacesDoc::formatChipLabel("G\xc3\xa9n.", makePlace(1, 1, 1, "G\xc3\xa9nesis 1", true), label, sizeof(label));
  EXPECT_STREQ(label, "G\xc3\xa9n. 1");
}

TEST(PlacesDocDisplay, AChipWithoutAnAbbreviationShowsTheReference) {
  char label[PlacesDoc::MAX_REFERENCE_BYTES + 1];
  PlacesDoc::formatChipLabel("", makePlace(66, 21, 4, "Revelation 21:4"), label, sizeof(label));
  EXPECT_STREQ(label, "Revelation 21:4");
}

TEST(PlacesDocDisplay, DescribeJoinsTheReferencesNewestFirst) {
  EXPECT_EQ(PlacesDoc::describe({makePlace(1, 1, 1, "Genesis 1", true), makePlace(66, 21, 4, "Revelation 21:4")}),
            "Genesis 1 | Revelation 21:4");
}
