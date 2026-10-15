// The read-only bible.json passes behind Predicación's Mis temas.

#include <ArduinoJson.h>
#include <HalStorage.h>
#include <gtest/gtest.h>

#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

#include "HalStorageFake.h"
#include "PreachingPassages.h"
#include "StudyStore/PassageDoc.h"
#include "StudyStore/PassageText.h"
#include "StudyStore/TagPalette.h"
#include "StudyStore/Unit.h"

namespace {

using PreachingPassages::Status;

const std::string BIBLE = "/.berean/passages/bible.json";

void putBible(const std::string& rows) { storage_fake::putFile(BIBLE, R"({"v":4,"p":[)" + rows + "]}"); }

study::TagId tag(const uint16_t raw) { return study::toTagId(raw); }

struct CountdownJsonAllocator : ArduinoJson::Allocator {
  int remaining = 1 << 30;
  void* allocate(const size_t size) override {
    if (remaining <= 0) return nullptr;
    --remaining;
    return malloc(size);
  }
  void deallocate(void* pointer) override { free(pointer); }
  void* reallocate(void* pointer, const size_t size) override {
    if (remaining <= 0) return nullptr;
    --remaining;
    return realloc(pointer, size);
  }
};

class PreachingPassagesIo : public ::testing::Test {
 protected:
  void SetUp() override { storage_fake::reset(); }
};

TEST_F(PreachingPassagesIo, CountsEveryPickedTagIncludingThoseWithNoPassages) {
  putBible(R"({"u":"v:1:1:1:0","s":10,"r":"R1","t":[3]},)"
           R"({"u":"v:1:1:2:0","s":10,"r":"R2","t":[3,4]},)"
           R"({"u":"v:1:1:3:0","s":10,"r":"R3","t":[4]})");
  std::vector<PreachingPassages::TagCount> counts;
  ASSERT_EQ(PreachingPassages::countByTag({tag(3), tag(4), tag(5)}, nullptr, counts), Status::Ok);
  ASSERT_EQ(counts.size(), 3u);
  EXPECT_EQ(counts[0].tag, tag(3));
  EXPECT_EQ(counts[0].passages, 2);
  EXPECT_EQ(counts[1].tag, tag(4));
  EXPECT_EQ(counts[1].passages, 2);
  EXPECT_EQ(counts[2].tag, tag(5));
  EXPECT_EQ(counts[2].passages, 0);
}

TEST_F(PreachingPassagesIo, CountsSkipUnaddressableRowsAndRepeatedTags) {
  putBible(R"({"u":"garbage","s":10,"r":"R1","t":[3]},)"
           R"({"u":"v:1:1:2:0","s":10,"r":"R2","t":[3,3]})");
  std::vector<PreachingPassages::TagCount> counts;
  ASSERT_EQ(PreachingPassages::countByTag({tag(3)}, nullptr, counts), Status::Ok);
  ASSERT_EQ(counts.size(), 1u);
  EXPECT_EQ(counts[0].passages, 1);
}

TEST_F(PreachingPassagesIo, AMissingFileIsMissingNotZero) {
  std::vector<PreachingPassages::TagCount> counts;
  EXPECT_EQ(PreachingPassages::countByTag({tag(3)}, nullptr, counts), Status::Missing);
  EXPECT_TRUE(counts.empty());
  EXPECT_FALSE(PreachingPassages::isAvailable(Status::Missing));
}

TEST_F(PreachingPassagesIo, UnreadableFilesAreUnreadable) {
  for (const char* contents : {"", R"({"v":5,"p":[]})", R"({"v":4})", R"({"p":[]})", R"({"v":4,"p":[{"u":)"}) {
    storage_fake::putFile(BIBLE, contents);
    std::vector<PreachingPassages::TagCount> counts;
    EXPECT_EQ(PreachingPassages::countByTag({tag(3)}, nullptr, counts), Status::Unreadable) << contents;
    EXPECT_TRUE(counts.empty()) << contents;
  }

  std::vector<PreachingPassages::TagCount> counts;
  storage_fake::putFile(BIBLE, std::string(study::PassageDoc::SAVE_BYTE_BUDGET + 4096 + 1, ' '));
  EXPECT_EQ(PreachingPassages::countByTag({tag(3)}, nullptr, counts), Status::Unreadable);

  putBible(R"({"u":"v:1:1:1:0","s":10,"r":"R1","t":[3]})");
  storage_fake::failReadsOf(BIBLE);
  EXPECT_EQ(PreachingPassages::countByTag({tag(3)}, nullptr, counts), Status::Unreadable);
}

TEST_F(PreachingPassagesIo, CountsOutOfMemoryIsReported) {
  putBible(R"({"u":"v:1:1:1:0","s":10,"r":"R1","t":[3]})");
  CountdownJsonAllocator exhausted;
  exhausted.remaining = 0;
  std::vector<PreachingPassages::TagCount> counts;
  EXPECT_EQ(PreachingPassages::countByTag({tag(3)}, &exhausted, counts), Status::OutOfMemory);
  EXPECT_TRUE(counts.empty());
}

TEST_F(PreachingPassagesIo, CountsNeverWriteOrAdoptATemp) {
  const std::string tmp = BIBLE + ".tmp";
  storage_fake::putFile(tmp, R"({"v":4,"p":[]})");
  std::vector<PreachingPassages::TagCount> counts;
  EXPECT_EQ(PreachingPassages::countByTag({tag(3)}, nullptr, counts), Status::Missing);
  EXPECT_FALSE(storage_fake::fileBytes(BIBLE).has_value());
  EXPECT_TRUE(storage_fake::fileBytes(tmp).has_value());

  putBible(R"({"u":"v:1:1:1:0","s":10,"r":"R1","t":[3]})");
  const auto before = storage_fake::fileBytes(BIBLE);
  EXPECT_EQ(PreachingPassages::countByTag({tag(3)}, nullptr, counts), Status::Ok);
  EXPECT_EQ(storage_fake::fileBytes(BIBLE), before);
}

void* failingTextAllocate(size_t) { return nullptr; }

// "Ref 1" is Matthew at spine 5: spine, not book, puts it before Genesis. The two
// "Tie" rows share a key and must keep file order.
TEST_F(PreachingPassagesIo, ATagsPassagesComeInBibleOrder) {
  putBible(R"({"u":"v:40:1:2:0","s":40,"r":"Ref 4","w":"fourth","t":[7]},)"
           R"({"u":"v:50:1:1:0","s":60,"r":"Tie A","w":"a","t":[7]},)"
           R"({"u":"v:40:5:1:0","s":5,"r":"Ref 1","w":"first","t":[7]},)"
           R"({"u":"v:1:1:1:0","s":12,"r":"Ref 2","w":"second","t":[7]},)"
           R"({"u":"v:50:1:1:0","s":60,"r":"Tie B","w":"b","t":[7]},)"
           R"({"u":"v:40:1:1:0","s":40,"r":"Ref 3","w":"third","t":[7]})");
  std::vector<PreachingPassages::Passage> passages;
  ASSERT_EQ(PreachingPassages::passagesWithTag(tag(7), study::PassageDoc::Allocators{}, passages), Status::Ok);
  ASSERT_EQ(passages.size(), 6u);
  EXPECT_EQ(passages[0].reference, "Ref 1");
  EXPECT_EQ(passages[1].reference, "Ref 2");
  EXPECT_EQ(passages[2].reference, "Ref 3");
  EXPECT_EQ(passages[3].reference, "Ref 4");
  EXPECT_EQ(passages[4].reference, "Tie A");
  EXPECT_EQ(passages[5].reference, "Tie B");
  EXPECT_EQ(passages[1].spine, 12);
  EXPECT_EQ(passages[1].start, (study::Unit{study::UnitKind::Verse, 1, 1, 1, 0}));
  EXPECT_EQ(passages[3].text.view(), "fourth");
}

TEST_F(PreachingPassagesIo, ATextFallsBackToTheSnippet) {
  putBible(R"({"u":"v:1:1:1:0","s":12,"r":"Whole","x":"short","w":"the whole text","t":[7]},)"
           R"({"u":"v:1:1:2:0","s":12,"r":"Legacy","x":"snippet only","t":[7]},)"
           R"({"u":"v:1:1:3:0","s":12,"r":"Bare","t":[7]})");
  std::vector<PreachingPassages::Passage> passages;
  ASSERT_EQ(PreachingPassages::passagesWithTag(tag(7), study::PassageDoc::Allocators{}, passages), Status::Ok);
  ASSERT_EQ(passages.size(), 3u);
  EXPECT_EQ(passages[0].text.view(), "the whole text");
  EXPECT_EQ(passages[1].text.view(), "snippet only");
  EXPECT_EQ(passages[2].text.view(), "");
  EXPECT_EQ(passages[2].reference, "Bare");
}

TEST_F(PreachingPassagesIo, OnlyPassagesCarryingTheTagAreReturned) {
  putBible(R"({"u":"v:1:1:1:0","s":12,"r":"Mine","w":"a","t":[3,7]},)"
           R"({"u":"v:1:1:2:0","s":12,"r":"Other","w":"b","t":[3]},)"
           R"({"u":"garbage","s":12,"r":"Unaddressable","w":"c","t":[7]})");
  std::vector<PreachingPassages::Passage> passages;
  ASSERT_EQ(PreachingPassages::passagesWithTag(tag(7), study::PassageDoc::Allocators{}, passages), Status::Ok);
  ASSERT_EQ(passages.size(), 1u);
  EXPECT_EQ(passages[0].reference, "Mine");

  ASSERT_EQ(PreachingPassages::passagesWithTag(tag(9), study::PassageDoc::Allocators{}, passages), Status::Ok);
  EXPECT_TRUE(passages.empty());
}

TEST_F(PreachingPassagesIo, ATagsPassagesReportMissingAndUnreadable) {
  std::vector<PreachingPassages::Passage> passages;
  EXPECT_EQ(PreachingPassages::passagesWithTag(tag(7), study::PassageDoc::Allocators{}, passages), Status::Missing);
  EXPECT_TRUE(passages.empty());

  storage_fake::putFile(BIBLE, R"({"v":5,"p":[]})");
  EXPECT_EQ(PreachingPassages::passagesWithTag(tag(7), study::PassageDoc::Allocators{}, passages), Status::Unreadable);
  EXPECT_TRUE(passages.empty());
}

TEST_F(PreachingPassagesIo, ATagsPassagesOutOfMemoryIsReported) {
  putBible(R"({"u":"v:1:1:1:0","s":12,"r":"R","w":"text","t":[7]})");
  std::vector<PreachingPassages::Passage> passages;

  CountdownJsonAllocator exhausted;
  exhausted.remaining = 0;
  const study::PassageDoc::Allocators noJson{study::defaultTextAllocator(), &exhausted};
  EXPECT_EQ(PreachingPassages::passagesWithTag(tag(7), noJson, passages), Status::OutOfMemory);
  EXPECT_TRUE(passages.empty());

  const study::PassageDoc::Allocators noText{study::TextAllocator{failingTextAllocate, study::freeText}, nullptr};
  EXPECT_EQ(PreachingPassages::passagesWithTag(tag(7), noText, passages), Status::OutOfMemory);
  EXPECT_TRUE(passages.empty());
}

TEST_F(PreachingPassagesIo, ATagsPassagesNeverWriteTheFile) {
  putBible(R"({"u":"v:1:1:1:0","s":12,"r":"R","w":"text","t":[7]})");
  const auto before = storage_fake::fileBytes(BIBLE);
  std::vector<PreachingPassages::Passage> passages;
  EXPECT_EQ(PreachingPassages::passagesWithTag(tag(7), study::PassageDoc::Allocators{}, passages), Status::Ok);
  EXPECT_EQ(storage_fake::fileBytes(BIBLE), before);
  EXPECT_FALSE(storage_fake::fileBytes(BIBLE + ".tmp").has_value());
}

}  // namespace
