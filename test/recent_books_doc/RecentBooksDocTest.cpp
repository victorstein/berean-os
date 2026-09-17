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
