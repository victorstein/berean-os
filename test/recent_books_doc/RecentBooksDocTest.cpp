// Host coverage for /.crosspoint/recent.json's format rules. RecentBooksDoc is
// deliberately free of <Arduino.h> so this is possible; its storage shell,
// RecentBooksStore.cpp, is not (it reaches Arduino.h through PersistableStore.h,
// and also includes Epub.h and HalStorage.h) and is device-verified only.

#include <ArduinoJson.h>
#include <SaveBudget.h>
#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "util/RecentBooksDoc.h"

namespace {

RecentBook makeBook(const char* path, const char* title, const char* author, const char* cover) {
  RecentBook b;
  b.path = path;
  b.title = title;
  b.author = author;
  b.coverBmpPath = cover;
  return b;
}

}  // namespace

// The wrapper and per-entry overhead are not arithmetic on arithmetic: they are
// what ArduinoJson actually emits, and worstCaseBytes() is built on them. A
// renamed key or a new field moves this and must be a deliberate act.
TEST(RecentBooksDocBudget, DocumentOverheadMatchesTheMeasuredConstants) {
  std::vector<RecentBook> books;
  for (size_t i = 0; i < RecentBooksDoc::MAX_RECENT_BOOKS; ++i) books.push_back(makeBook("", "", "", ""));

  JsonDocument doc;
  RecentBooksDoc::toJson(books, doc);

  const size_t expected = RecentBooksDoc::DOC_WRAPPER_BYTES + (RecentBooksDoc::MAX_RECENT_BOOKS - 1) +
                          RecentBooksDoc::MAX_RECENT_BOOKS * RecentBooksDoc::ENTRY_OVERHEAD_BYTES;
  EXPECT_EQ(measureJson(doc), expected) << "ten empty entries measured " << measureJson(doc)
                                        << "; DOC_WRAPPER_BYTES/ENTRY_OVERHEAD_BYTES no longer describe the shape";
  EXPECT_EQ(expected, 541u);
}

// The whole point of issue #40's second half: the budget is now a real figure,
// tighter than the shared default and well clear of the silent read truncation.
TEST(RecentBooksDocBudget, IsTighterThanTheDefaultAndClearOfTheReadCap) {
  EXPECT_LT(RecentBooksDoc::SAVE_BUDGET, persist::DEFAULT_SAVE_BUDGET);
  EXPECT_LT(persist::DEFAULT_SAVE_BUDGET, persist::SD_READ_TRUNCATION_CAP);
  EXPECT_EQ(RecentBooksDoc::SAVE_BUDGET, 11421u) << "derived from the field caps; recompute, do not tidy";
}

TEST(RecentBooksDoc, RoundTripsEveryField) {
  std::vector<RecentBook> in{
      makeBook("/books/w_S_202601.epub", "La Atalaya", "Watch Tower", "/.crosspoint/epub_1/thumb_[HEIGHT].bmp"),
      makeBook("/books/mwb_S_202601.epub", "Guía de actividades", "", ""),
  };

  JsonDocument doc;
  RecentBooksDoc::toJson(in, doc);

  std::vector<RecentBook> out;
  bool needsResave = true;
  ASSERT_TRUE(RecentBooksDoc::fromJson(doc.as<JsonVariantConst>(), out, needsResave));
  ASSERT_EQ(out.size(), 2u);

  EXPECT_EQ(out[0].path, in[0].path);
  EXPECT_EQ(out[0].title, in[0].title);
  EXPECT_EQ(out[0].author, in[0].author);
  EXPECT_EQ(out[0].coverBmpPath, in[0].coverBmpPath);
  EXPECT_EQ(out[1].path, in[1].path);
  EXPECT_EQ(out[1].author, "");
  EXPECT_FALSE(needsResave) << "nothing needed shortening, so nothing should be rewritten";
}

// Preserves what RecentBooksStore::fromJson did before the split: a file holding
// more than the cap is read up to the cap, not rejected.
TEST(RecentBooksDoc, FromJsonCapsTheEntryCount) {
  std::vector<RecentBook> in;
  for (int i = 0; i < 25; ++i) in.push_back(makeBook("/books/b.epub", "t", "a", "c"));

  JsonDocument doc;
  RecentBooksDoc::toJson(in, doc);

  std::vector<RecentBook> out;
  bool needsResave = false;
  ASSERT_TRUE(RecentBooksDoc::fromJson(doc.as<JsonVariantConst>(), out, needsResave));
  EXPECT_EQ(out.size(), RecentBooksDoc::MAX_RECENT_BOOKS);
}

// A missing or wrong-typed "books" key is an empty list, not a failure: only a
// JSON parse error is fatal, and that is caught upstream.
TEST(RecentBooksDoc, FromJsonToleratesAMissingBooksKey) {
  JsonDocument doc;
  doc["something_else"] = 1;

  std::vector<RecentBook> out{makeBook("/stale", "stale", "", "")};
  bool needsResave = true;
  EXPECT_TRUE(RecentBooksDoc::fromJson(doc.as<JsonVariantConst>(), out, needsResave));
  EXPECT_TRUE(out.empty()) << "fromJson must clear the target first";
  EXPECT_FALSE(needsResave);
}
