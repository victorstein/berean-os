#include <ArduinoJson.h>
#include <gtest/gtest.h>

#include <string>

#include "StudyStore/ChapterCompletion.h"

namespace {

constexpr uint8_t GENESIS = 1;
constexpr uint8_t PSALMS = 19;
constexpr uint8_t JOHN = 43;
constexpr uint8_t REVELATION = 66;

study::DocumentUnits verseDocument(const uint8_t book, std::initializer_list<uint16_t> chapters) {
  study::DocumentUnits units;
  units.kind = study::UnitKind::Verse;
  units.book = book;
  uint32_t offset = 0;
  for (const uint16_t chapter : chapters) {
    units.anchors.push_back({offset, chapter, 1});
    units.anchors.push_back({offset + 10, chapter, 2});
    offset += 100;
  }
  return units;
}

// ---- The canon table -------------------------------------------------------

TEST(CanonicalChapters, SumToTheProtestantCanonsTotal) {
  unsigned total = 0;
  for (uint8_t book = 1; book <= study::BIBLE_BOOK_COUNT; ++book) total += study::canonicalChapterCount(book);
  EXPECT_EQ(total, study::CANONICAL_CHAPTER_TOTAL);
  EXPECT_EQ(study::CANONICAL_CHAPTER_TOTAL, 1189u);
}

TEST(CanonicalChapters, KnowsTheLongestAndTheSingleChapterBooks) {
  EXPECT_EQ(study::canonicalChapterCount(PSALMS), 150u);
  EXPECT_EQ(study::canonicalChapterCount(31), 1u);  // Obadiah
  EXPECT_EQ(study::canonicalChapterCount(REVELATION), 22u);
  EXPECT_EQ(study::canonicalChapterCount(0), 0u);
  EXPECT_EQ(study::canonicalChapterCount(67), 0u);
}

// ---- The bitmap ------------------------------------------------------------

TEST(ChapterCompletionBitmap, MarksOnlyTheChapterNamed) {
  study::ChapterCompletion record;
  EXPECT_TRUE(record.markRead(JOHN, 3));
  EXPECT_TRUE(record.isRead(JOHN, 3));
  EXPECT_FALSE(record.isRead(JOHN, 2));
  EXPECT_FALSE(record.isRead(JOHN, 4));
  EXPECT_FALSE(record.isRead(GENESIS, 3));
  EXPECT_EQ(record.readCount(), 1u);
}

TEST(ChapterCompletionBitmap, ReportsARepeatMarkAsNothingNew) {
  study::ChapterCompletion record;
  EXPECT_TRUE(record.markRead(PSALMS, 119));
  EXPECT_FALSE(record.markRead(PSALMS, 119));
  EXPECT_EQ(record.readCount(), 1u);
}

TEST(ChapterCompletionBitmap, RefusesAddressesOutsideTheCanon) {
  study::ChapterCompletion record;
  EXPECT_FALSE(record.markRead(0, 1));
  EXPECT_FALSE(record.markRead(67, 1));
  EXPECT_FALSE(record.markRead(GENESIS, 0));
  EXPECT_FALSE(record.markRead(GENESIS, 51));
  EXPECT_FALSE(record.markRead(PSALMS, 151));
  EXPECT_EQ(record.readCount(), 0u);
  EXPECT_FALSE(record.isRead(GENESIS, 51));
}

// Neighbouring books share bytes in the dense layout, so the last chapter of one
// and the first of the next are where an off-by-one would show.
TEST(ChapterCompletionBitmap, KeepsBookBoundariesApart) {
  study::ChapterCompletion record;
  record.markRead(GENESIS, 50);
  record.markRead(2, 1);
  record.markRead(REVELATION, 22);
  EXPECT_EQ(record.readCountInBook(GENESIS), 1u);
  EXPECT_EQ(record.readCountInBook(2), 1u);
  EXPECT_EQ(record.readCountInBook(REVELATION), 1u);
  EXPECT_EQ(record.readCountInBook(3), 0u);
  EXPECT_EQ(record.readCount(), 3u);
}

TEST(ChapterCompletionBitmap, CanHoldTheWholeCanon) {
  study::ChapterCompletion record;
  for (uint8_t book = 1; book <= study::BIBLE_BOOK_COUNT; ++book) {
    for (uint16_t chapter = 1; chapter <= study::canonicalChapterCount(book); ++chapter) {
      ASSERT_TRUE(record.markRead(book, chapter));
    }
  }
  EXPECT_EQ(record.readCount(), study::CANONICAL_CHAPTER_TOTAL);
  EXPECT_EQ(record.readCountInBook(PSALMS), 150u);
}

// ---- The trigger -----------------------------------------------------------

TEST(ChapterCompletionTrigger, OnlyAForwardTurnFromTheLastBuiltPageLeavesTheDocument) {
  EXPECT_FALSE(study::forwardTurnLeavesDocument(0, 5, false));
  EXPECT_FALSE(study::forwardTurnLeavesDocument(3, 5, false));
  EXPECT_TRUE(study::forwardTurnLeavesDocument(4, 5, false));
  EXPECT_TRUE(study::forwardTurnLeavesDocument(0, 1, false));
}

// While the section is still being laid out the reader stays in it and waits for
// more pages, so "the last page so far" is not the chapter's end.
TEST(ChapterCompletionTrigger, DoesNotFireWhileTheSectionIsStillBuilding) {
  EXPECT_FALSE(study::forwardTurnLeavesDocument(4, 5, true));
}

TEST(ChapterCompletionTrigger, ResolvesTheChapterFromTheDocumentsVerseMarkers) {
  study::ChapterCompletion record;
  EXPECT_TRUE(study::markDocumentChapters(record, verseDocument(JOHN, {3})));
  EXPECT_TRUE(record.isRead(JOHN, 3));
  EXPECT_EQ(record.readCount(), 1u);
}

TEST(ChapterCompletionTrigger, MarksEveryChapterADocumentCarries) {
  study::ChapterCompletion record;
  EXPECT_TRUE(study::markDocumentChapters(record, verseDocument(PSALMS, {1, 2})));
  EXPECT_TRUE(record.isRead(PSALMS, 1));
  EXPECT_TRUE(record.isRead(PSALMS, 2));
}

TEST(ChapterCompletionTrigger, IgnoresDocumentsThatAreNotBibleChapters) {
  study::ChapterCompletion record;

  study::DocumentUnits paragraphs;
  paragraphs.kind = study::UnitKind::Paragraph;
  paragraphs.anchors.push_back({0, 0, 40});
  EXPECT_FALSE(study::markDocumentChapters(record, paragraphs));

  EXPECT_FALSE(study::markDocumentChapters(record, study::DocumentUnits{}));

  // A Verse document whose book was never resolved has no canonical address.
  EXPECT_FALSE(study::markDocumentChapters(record, verseDocument(0, {3})));
  EXPECT_EQ(record.readCount(), 0u);
}

// ---- The save policy -------------------------------------------------------

struct FakeSaver {
  static inline int calls = 0;
  static inline bool succeed = true;
  static inline study::ChapterCompletion lastSaved;

  static void reset(const bool willSucceed) {
    calls = 0;
    succeed = willSucceed;
    lastSaved = study::ChapterCompletion{};
  }
  static bool save(const study::ChapterCompletion& record) {
    ++calls;
    if (succeed) lastSaved = record;
    return succeed;
  }
};

TEST(ChapterCompletionSave, WritesOnceWhenANewChapterIsMarked) {
  FakeSaver::reset(true);
  study::ChapterCompletion record;
  EXPECT_EQ(study::recordDocumentRead(record, verseDocument(JOHN, {3}), false, FakeSaver::save),
            study::CompletionMarkResult::Saved);
  EXPECT_EQ(FakeSaver::calls, 1);
  EXPECT_TRUE(FakeSaver::lastSaved.isRead(JOHN, 3));
}

// The debounce: re-reading a chapter already recorded costs no write at all.
TEST(ChapterCompletionSave, DoesNotWriteWhenNothingIsNew) {
  FakeSaver::reset(true);
  study::ChapterCompletion record;
  record.markRead(JOHN, 3);
  EXPECT_EQ(study::recordDocumentRead(record, verseDocument(JOHN, {3}), false, FakeSaver::save),
            study::CompletionMarkResult::NothingNew);
  EXPECT_EQ(study::recordDocumentRead(record, study::DocumentUnits{}, false, FakeSaver::save),
            study::CompletionMarkResult::NothingNew);
  EXPECT_EQ(FakeSaver::calls, 0);
}

TEST(ChapterCompletionSave, RollsBackWhenTheWriteFails) {
  FakeSaver::reset(false);
  study::ChapterCompletion record;
  record.markRead(GENESIS, 1);
  EXPECT_EQ(study::recordDocumentRead(record, verseDocument(JOHN, {3}), false, FakeSaver::save),
            study::CompletionMarkResult::SaveFailed);
  EXPECT_FALSE(record.isRead(JOHN, 3));
  EXPECT_TRUE(record.isRead(GENESIS, 1));
}

// A record that failed to load may still hold the user's history on the card.
// Saving the in-memory record -- which starts empty -- would replace it.
TEST(ChapterCompletionSave, NeverWritesOverARecordThatFailedToLoad) {
  FakeSaver::reset(true);
  study::ChapterCompletion record;
  EXPECT_EQ(study::recordDocumentRead(record, verseDocument(JOHN, {3}), true, FakeSaver::save),
            study::CompletionMarkResult::SaveRefused);
  EXPECT_EQ(FakeSaver::calls, 0);
  EXPECT_EQ(record.readCount(), 0u);
}

// ---- Serialisation ---------------------------------------------------------

TEST(ChapterCompletionJson, RoundTripsEveryMark) {
  study::ChapterCompletion record;
  record.markRead(GENESIS, 1);
  record.markRead(GENESIS, 50);
  record.markRead(PSALMS, 119);
  record.markRead(PSALMS, 150);
  record.markRead(REVELATION, 22);

  JsonDocument json;
  ASSERT_TRUE(record.toJsonWithinBudget(json));

  study::ChapterCompletion restored;
  ASSERT_TRUE(restored.fromJson(json.as<JsonVariantConst>()));
  EXPECT_EQ(restored, record);
  EXPECT_EQ(restored.readCount(), 5u);
}

TEST(ChapterCompletionJson, WritesTheDocumentedShape) {
  study::ChapterCompletion record;
  record.markRead(GENESIS, 1);
  record.markRead(GENESIS, 10);

  JsonDocument json;
  ASSERT_TRUE(record.toJsonWithinBudget(json));
  std::string text;
  serializeJson(json, text);
  EXPECT_EQ(text, R"({"v":1,"b":{"1":"0102"}})");
}

TEST(ChapterCompletionJson, OmitsBooksWithNothingRead) {
  JsonDocument json;
  ASSERT_TRUE(study::ChapterCompletion{}.toJsonWithinBudget(json));
  std::string text;
  serializeJson(json, text);
  EXPECT_EQ(text, R"({"v":1,"b":{}})");
}

TEST(ChapterCompletionJson, AWholeCanonFitsTheBudgetWithRoomToSpare) {
  study::ChapterCompletion record;
  for (uint8_t book = 1; book <= study::BIBLE_BOOK_COUNT; ++book) {
    for (uint16_t chapter = 1; chapter <= study::canonicalChapterCount(book); ++chapter) record.markRead(book, chapter);
  }
  EXPECT_LE(record.measureBytes() * 2, study::ChapterCompletion::SAVE_BYTE_BUDGET);
}

TEST(ChapterCompletionJson, RefusesToSerialiseOverBudget) {
  study::ChapterCompletion record;
  record.markRead(PSALMS, 1);
  JsonDocument json;
  EXPECT_FALSE(record.toJsonWithinBudget(json, 8));
  EXPECT_TRUE(record.toJsonWithinBudget(json, study::ChapterCompletion::SAVE_BYTE_BUDGET));
}

bool parses(const char* text, study::ChapterCompletion& out) {
  JsonDocument json;
  if (deserializeJson(json, text)) return false;
  return out.fromJson(json.as<JsonVariantConst>());
}

TEST(ChapterCompletionJson, RefusesAFutureFormatVersion) {
  study::ChapterCompletion record;
  EXPECT_FALSE(parses(R"({"v":2,"b":{"1":"01"}})", record));
}

TEST(ChapterCompletionJson, RefusesAMissingVersion) {
  study::ChapterCompletion record;
  EXPECT_FALSE(parses(R"({"b":{"1":"01"}})", record));
}

TEST(ChapterCompletionJson, RefusesAPresentZeroVersion) {
  study::ChapterCompletion record;
  EXPECT_FALSE(parses(R"({"v":0,"b":{"1":"01"}})", record));
}

TEST(ChapterCompletionJson, RefusesANegativeVersion) {
  study::ChapterCompletion record;
  EXPECT_FALSE(parses(R"({"v":-1,"b":{"1":"01"}})", record));
}

// Each of these is a file this firmware never writes. Reporting failure is what
// latches saving off, so the file is left on the card rather than replaced.
TEST(ChapterCompletionJson, RefusesACorruptRecordRatherThanLoadingPartOfIt) {
  const char* corrupt[] = {
      R"([1,2,3])",                    // not an object
      R"({"v":1,"b":[]})",             // books not an object
      R"({"v":1,"b":{"1":"0g"}})",     // not hex
      R"({"v":1,"b":{"1":"012"}})",    // odd length
      R"({"v":1,"b":{"1":7}})",        // not a string
      R"({"v":1,"b":{"0":"01"}})",     // book 0
      R"({"v":1,"b":{"67":"01"}})",    // past the canon
      R"({"v":1,"b":{"x":"01"}})",     // not a number
      R"({"v":1,"b":{"31":"02"}})",    // Obadiah 2 does not exist
      R"({"v":1,"b":{"31":"0100"}})",  // longer than the book
  };
  for (const char* text : corrupt) {
    study::ChapterCompletion record;
    record.markRead(JOHN, 3);
    EXPECT_FALSE(parses(text, record)) << text;
  }
}

TEST(ChapterCompletionJson, ALoadReplacesWhateverWasHeldBefore) {
  study::ChapterCompletion record;
  record.markRead(JOHN, 3);
  ASSERT_TRUE(parses(R"({"v":1,"b":{"19":"01"}})", record));
  EXPECT_FALSE(record.isRead(JOHN, 3));
  EXPECT_TRUE(record.isRead(PSALMS, 1));
  EXPECT_EQ(record.readCount(), 1u);
}

}  // namespace
