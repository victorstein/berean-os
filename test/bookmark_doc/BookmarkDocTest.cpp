// Host coverage for the bookmark file's format rules. BookmarkDoc is
// deliberately free of <Arduino.h> so this is possible; its storage shell,
// BookmarkFile.cpp, is not and is device-verified only.

#include <gtest/gtest.h>

#include <ArduinoJson.h>
#include <SaveBudget.h>

#include <string>
#include <vector>

#include "util/BookmarkDoc.h"

namespace {

BookmarkEntry makeEntry(const char* xpath, const char* summary, const bool withOffset) {
  BookmarkEntry e;
  e.xpath = xpath;
  e.summary = summary;
  e.percentage = 0.4831f;
  e.computedSpineIndex = 26;
  e.computedChapterPageCount = 31;
  e.computedChapterProgress = 14;
  if (withOffset) {
    e.hasVisibleTextOffset = true;
    e.visibleTextOffset = 41233;
  }
  return e;
}

}  // namespace

TEST(BookmarkDoc, RoundTripsEveryField) {
  std::vector<BookmarkEntry> in{
      makeEntry("/body/DocFragment[27]/body/div[1]/p[14]/text()[1].117", "In the beginning God created", true),
      makeEntry("/body/DocFragment[3]/body", "Now the earth was formless", false),
  };

  JsonDocument doc;
  BookmarkDoc::toJson(in, doc);

  std::vector<BookmarkEntry> out;
  ASSERT_TRUE(BookmarkDoc::fromJson(doc.as<JsonVariantConst>(), out));
  ASSERT_EQ(out.size(), 2u);

  EXPECT_EQ(out[0].xpath, in[0].xpath);
  EXPECT_EQ(out[0].summary, in[0].summary);
  EXPECT_FLOAT_EQ(out[0].percentage, in[0].percentage);
  EXPECT_EQ(out[0].computedSpineIndex, 26);
  EXPECT_EQ(out[0].computedChapterPageCount, 31);
  EXPECT_EQ(out[0].computedChapterProgress, 14);
  EXPECT_TRUE(out[0].hasVisibleTextOffset);
  EXPECT_EQ(out[0].visibleTextOffset, 41233u);

  EXPECT_EQ(out[1].xpath, in[1].xpath);
  EXPECT_FALSE(out[1].hasVisibleTextOffset) << "an absent vo must not become a present zero";
  EXPECT_EQ(out[1].visibleTextOffset, 0u);
}

TEST(BookmarkDoc, FromJsonClearsTheTargetFirst) {
  std::vector<BookmarkEntry> out{makeEntry("/stale", "stale", false)};
  JsonDocument doc;
  BookmarkDoc::toJson({}, doc);
  ASSERT_TRUE(BookmarkDoc::fromJson(doc.as<JsonVariantConst>(), out));
  EXPECT_TRUE(out.empty());
}

TEST(BookmarkDoc, RejectsANonObject) {
  JsonDocument doc;
  doc.to<JsonArray>();
  std::vector<BookmarkEntry> out;
  EXPECT_FALSE(BookmarkDoc::fromJson(doc.as<JsonVariantConst>(), out));
}

TEST(BookmarkDocVersion, ToJsonStampsTheCurrentVersion) {
  JsonDocument doc;
  BookmarkDoc::toJson({makeEntry("/body/DocFragment[1]/body", "x", false)}, doc);
  EXPECT_EQ(doc["v"] | 0, BookmarkDoc::FORMAT_VERSION);
}

TEST(BookmarkDocVersion, AnAbsentVersionIsReadAsOneWithEveryEntry) {
  // A file written by CrossPoint, before this field existed.
  JsonDocument doc;
  JsonArray arr = doc["bookmarks"].to<JsonArray>();
  for (int i = 0; i < 3; ++i) {
    JsonObject obj = arr.add<JsonObject>();
    obj["xpath"] = "/body/DocFragment[1]/body";
    obj["percentage"] = 0.5f;
    obj["summary"] = "legacy";
    obj["si"] = 0;
    obj["pc"] = 1;
    obj["pp"] = 0;
  }

  std::vector<BookmarkEntry> out;
  ASSERT_TRUE(BookmarkDoc::fromJson(doc.as<JsonVariantConst>(), out))
      << "a pre-versioning bookmark file must still load";
  EXPECT_EQ(out.size(), 3u);
}

TEST(BookmarkDocVersion, APresentZeroIsRefused) {
  JsonDocument doc;
  doc["v"] = 0;
  doc["bookmarks"].to<JsonArray>();
  std::vector<BookmarkEntry> out;
  EXPECT_FALSE(BookmarkDoc::fromJson(doc.as<JsonVariantConst>(), out))
      << "absent is legacy; a written 0 is a version this build does not know";
}

TEST(BookmarkDocVersion, AFutureVersionIsRefusedRatherThanReinterpreted) {
  JsonDocument doc;
  doc["v"] = BookmarkDoc::FORMAT_VERSION + 1;
  doc["bookmarks"].to<JsonArray>();
  std::vector<BookmarkEntry> out;
  EXPECT_FALSE(BookmarkDoc::fromJson(doc.as<JsonVariantConst>(), out));
}

TEST(BookmarkDocSummary, AnOverlongSummaryInTheFileIsReBoundedOnACodepointBoundary) {
  // 40 three-byte codepoints = 120 bytes, over MAX_SUMMARY_BYTES. A raw byte
  // cut would land mid-sequence and produce invalid UTF-8 that the next save
  // would serialise.
  std::string wide;
  for (int i = 0; i < 40; ++i) wide += "世";  // CJK, 3 bytes each

  JsonDocument doc;
  doc["v"] = BookmarkDoc::FORMAT_VERSION;
  JsonObject obj = doc["bookmarks"].to<JsonArray>().add<JsonObject>();
  obj["xpath"] = "/body/DocFragment[1]/body";
  obj["percentage"] = 0.5f;
  obj["summary"] = wide;
  obj["si"] = 0;
  obj["pc"] = 1;
  obj["pp"] = 0;

  std::vector<BookmarkEntry> out;
  ASSERT_TRUE(BookmarkDoc::fromJson(doc.as<JsonVariantConst>(), out));
  ASSERT_EQ(out.size(), 1u);
  EXPECT_LE(out[0].summary.size(), BookmarkDoc::MAX_SUMMARY_BYTES);
  EXPECT_EQ(out[0].summary.size() % 3, 0u) << "cut on a codepoint boundary, not a byte one";
}

TEST(BookmarkDocBudget, OneWorstCaseRecordStaysUnderThePinnedCeiling) {
  // Realistic worst case: a deep xpath with four-digit indices, a summary made
  // entirely of characters JSON escapes to two bytes, and vo present.
  std::string deep = "/body/DocFragment[1189]/body";
  for (int i = 0; i < 8; ++i) deep += "/div[4127]";
  deep += "/p[2718]/span[2]/text()[3].24816";

  std::vector<BookmarkEntry> one{makeEntry(deep.c_str(), std::string(72, '"').c_str(), true)};
  JsonDocument doc;
  BookmarkDoc::toJson(one, doc);
  // The whole document: the record plus the 22-byte {"v":1,"bookmarks":[...]}
  // wrapper. MAX_RECORD_BYTES is named for the same quantity.
  const size_t oneRecordDoc = measureJson(doc);

  EXPECT_LT(oneRecordDoc, BookmarkDoc::MAX_RECORD_BYTES)
      << "a worst-case bookmark document measured " << oneRecordDoc << " bytes; adding a field to "
      << "BookmarkEntry lowers how many bookmarks fit in SAVE_BYTE_BUDGET, so raise the ceiling "
      << "deliberately rather than letting it absorb the change";
}

TEST(BookmarkDocBudget, AnOverBudgetDocumentLoadsInFullRatherThanFailing) {
  // A-5, and the one place BookmarkDoc deliberately diverges from PassageDoc:
  // "too big to write back" is NOT "unreadable". PassageDoc::fromJson ends
  // `return measureBytes() <= SAVE_BYTE_BUDGET;` (PassageDoc.cpp:131), which
  // here would report Failed, latch saving off for the session, and freeze
  // exactly the 45,000-50,000 byte legacy file the shrink exception exists to
  // rescue. The divergence is by omission -- fromJson never measures -- so
  // without this test nothing stops the next person reintroducing it.
  std::vector<BookmarkEntry> many;
  many.reserve(300);
  for (int i = 0; i < 300; ++i) {
    many.push_back(makeEntry("/body/DocFragment[27]/body/div[1]/p[14]/text()[1].117",
                             "In the beginning God created the heavens", true));
  }
  JsonDocument doc;
  BookmarkDoc::toJson(many, doc);
  ASSERT_GT(measureJson(doc), persist::SD_READ_TRUNCATION_CAP) << "fixture must be over the read cap";

  std::vector<BookmarkEntry> out;
  ASSERT_TRUE(BookmarkDoc::fromJson(doc.as<JsonVariantConst>(), out));
  EXPECT_EQ(out.size(), 300u) << "not one entry may be dropped for size";
}

TEST(BookmarkDocBudget, TheBudgetStillHoldsTheRecordCountTheNoCapDecisionRestsOn) {
  // The figure the "no record cap" decision rests on: the budget must remain a
  // generous ceiling for a whole-Bible EPUB, not a limit users meet in practice.
  std::vector<BookmarkEntry> two{
      makeEntry("/body/DocFragment[27]/body/div[1]/p[14]/text()[1].117", "In the beginning God created the heavens",
                true),
      makeEntry("/body/DocFragment[27]/body/div[1]/p[14]/text()[1].117", "In the beginning God created the heavens",
                true),
  };
  std::vector<BookmarkEntry> one{two[0]};

  JsonDocument oneDoc;
  BookmarkDoc::toJson(one, oneDoc);
  JsonDocument twoDoc;
  BookmarkDoc::toJson(two, twoDoc);
  const size_t perRecord = measureJson(twoDoc) - measureJson(oneDoc);
  ASSERT_GT(perRecord, 0u);

  // 218 is the figure A-2 rests on: a real summary is capped at 72 bytes, which
  // makes a full record ~206 bytes and the budget's ceiling ~218. This
  // fixture's shorter summary measures less per record and so clears 218 with
  // margin -- the assertion pins the decision, not the fixture.
  EXPECT_GE(BookmarkDoc::SAVE_BYTE_BUDGET / perRecord, 218u)
      << "this record measured " << perRecord << " bytes, so the budget holds "
      << (BookmarkDoc::SAVE_BYTE_BUDGET / perRecord) << " bookmarks";
}
