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
