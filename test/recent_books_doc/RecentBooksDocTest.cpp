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

TEST(RecentBooksDocNormalise, CapsTitleOnACodepointBoundary) {
  // 60 three-byte codepoints = 180 bytes, over MAX_TITLE_BYTES. A raw byte cut
  // would land mid-sequence and produce invalid UTF-8 that the next save
  // serialises. 128 is not a multiple of 3, so a correct cut lands at 126.
  std::string wide;
  for (int i = 0; i < 60; ++i) wide += "\xe4\xb8\x96";  // U+4E16

  RecentBook book = makeBook("/books/b.epub", wide.c_str(), "", "");
  EXPECT_TRUE(RecentBooksDoc::normalise(book));
  EXPECT_LE(book.title.size(), RecentBooksDoc::MAX_TITLE_BYTES);
  EXPECT_EQ(book.title.size() % 3, 0u) << "cut on a codepoint boundary, not a byte one";
  EXPECT_EQ(book.title.size(), 126u);
}

TEST(RecentBooksDocNormalise, CapsAuthorOnACodepointBoundary) {
  std::string wide;
  for (int i = 0; i < 60; ++i) wide += "\xe4\xb8\x96";

  RecentBook book = makeBook("/books/b.epub", "", wide.c_str(), "");
  EXPECT_TRUE(RecentBooksDoc::normalise(book));
  EXPECT_LE(book.author.size(), RecentBooksDoc::MAX_AUTHOR_BYTES);
  EXPECT_EQ(book.author.size() % 3, 0u);
}

// path is the store's key. Truncating it would make pruneMissing() delete the
// entry on the next boot, so "bound the strings" must never be extended to it.
TEST(RecentBooksDocNormalise, NeverTouchesPathOrCoverBmpPath) {
  const std::string longPath = "/" + std::string(599, 'p');
  const std::string longCover = "/" + std::string(599, 'c');

  RecentBook book = makeBook(longPath.c_str(), "t", "a", longCover.c_str());
  RecentBooksDoc::normalise(book);
  EXPECT_EQ(book.path, longPath);
  EXPECT_EQ(book.coverBmpPath, longCover);
}

// The return value is what gates the load-side resave: rewriting the file every
// time nothing changed would be wrong.
TEST(RecentBooksDocNormalise, ReportsWhetherItChangedAnything) {
  RecentBook shortBook = makeBook("/books/b.epub", "La Atalaya", "Watch Tower", "/c.bmp");
  EXPECT_FALSE(RecentBooksDoc::normalise(shortBook));

  RecentBook longBook = makeBook("/books/b.epub", std::string(400, 'x').c_str(), "a", "/c.bmp");
  EXPECT_TRUE(RecentBooksDoc::normalise(longBook));
  EXPECT_EQ(longBook.title.size(), RecentBooksDoc::MAX_TITLE_BYTES);
}

// The caps are a display decision (spec A1/A2). These are the strings they were
// chosen against: if a cap is tightened carelessly, this fails before anything
// reaches a device.
TEST(RecentBooksDocNormalise, LeavesRealPublicationStringsUnchanged) {
  const char* titles[] = {
      "Traducción del Nuevo Mundo de las Santas Escrituras (revisión de 2019)",  // 72 bytes
      "Guía de actividades para la reunión Vida y Ministerio Cristianos",        // 66
      "La Atalaya anunciando el Reino de Jehová (edición de estudio)",           // 63
      "New World Translation of the Holy Scriptures (2013 Revision)",            // 60
      "The Watchtower Announcing Jehovah's Kingdom (Study Edition)",             // 59
      "¿Qué nos enseña realmente la Biblia?",                                    // 39
  };
  for (const char* title : titles) {
    RecentBook book = makeBook("/books/b.epub", title, "", "");
    EXPECT_FALSE(RecentBooksDoc::normalise(book)) << "a real title must survive untouched: " << title;
    EXPECT_EQ(book.title, title);
  }

  const char* authors[] = {
      "Watchtower Bible and Tract Society of New York, Inc.",  // 52 bytes
      "Watch Tower Bible and Tract Society of Pennsylvania",   // 51
      "Asociación de los Testigos de Jehová",                  // 38
  };
  for (const char* author : authors) {
    RecentBook book = makeBook("/books/b.epub", "", author, "");
    EXPECT_FALSE(RecentBooksDoc::normalise(book)) << "a real author must survive untouched: " << author;
    EXPECT_EQ(book.author, author);
  }
}

// title is also the Bible tile's selector: LauncherActivity.cpp:96-99 opens
// whichever recent book's title contains "Nuevo Mundo" or "New World". Both the
// cap and utf8SafeSummary's whitespace collapse rewrite that input, so a cap
// lowered below the marker's position silently breaks the tile — and the test
// above would still pass, because it only checks whole strings that fit.
TEST(RecentBooksDocNormalise, KeepsTheBibleHeuristicsMarkers) {
  const char* titles[] = {
      "Traducción del Nuevo Mundo de las Santas Escrituras (revisión de 2019)",
      "Traducción del Nuevo  Mundo de las Santas Escrituras",  // interior double space
      "New World Translation of the Holy Scriptures (2013 Revision)",
  };
  for (const char* title : titles) {
    RecentBook book = makeBook("/books/nwt.epub", title, "", "");
    RecentBooksDoc::normalise(book);
    const bool matches =
        book.title.find("Nuevo Mundo") != std::string::npos || book.title.find("New World") != std::string::npos;
    EXPECT_TRUE(matches) << "the Bible tile can no longer find this title: " << book.title;
  }
}

TEST(RecentBooksDoc, FromJsonReBoundsAnOverlongTitleFromTheCard) {
  // A file 1.9.10 was able to write: under the old 45,000-byte budget, over the
  // new one. It must shrink on load, not be refused on the next save.
  JsonDocument doc;
  JsonObject obj = doc["books"].to<JsonArray>().add<JsonObject>();
  obj["path"] = "/books/b.epub";
  obj["title"] = std::string(1800, 'x');
  obj["author"] = std::string(1800, 'y');
  obj["coverBmpPath"] = "/.crosspoint/epub_1/thumb_[HEIGHT].bmp";

  std::vector<RecentBook> out;
  bool needsResave = false;
  ASSERT_TRUE(RecentBooksDoc::fromJson(doc.as<JsonVariantConst>(), out, needsResave));
  ASSERT_EQ(out.size(), 1u);
  EXPECT_EQ(out[0].title.size(), RecentBooksDoc::MAX_TITLE_BYTES);
  EXPECT_EQ(out[0].author.size(), RecentBooksDoc::MAX_AUTHOR_BYTES);
  EXPECT_EQ(out[0].path, "/books/b.epub") << "the key must survive a load-side re-bound";
  EXPECT_TRUE(needsResave) << "the shrunken entries have to reach the card";
}
