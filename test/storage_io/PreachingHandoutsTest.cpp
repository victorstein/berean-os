// The read-only /preaching-cards.json passes behind Predicación's Tarjetas.

#include <ArduinoJson.h>
#include <HalStorage.h>
#include <gtest/gtest.h>

#include <cstddef>
#include <cstdlib>
#include <string>
#include <vector>

#include "HalStorageFake.h"
#include "PreachingHandouts.h"
#include "StudyStore/PassageDoc.h"
#include "StudyStore/PassageText.h"

namespace {

using PreachingHandouts::Status;

const std::string FILE_PATH = PreachingHandouts::FILE_PATH;

const char* const TWO_HANDOUTS = R"({"v":1,"cards":[
  {"title":"Stress","sections":[{"heading":"","verses":[
    {"ref":"Book 1:2","href":"b-split1.xhtml#chapter1_verse2","text":"Made-up verse one."},
    {"ref":"Book 3:4, 5","href":"b-split3.xhtml#chapter3_verse4","text":"Made-up verse two."}]}]},
  {"title":"An issue","sections":[
    {"heading":"First article","verses":[{"ref":"Book 6:7","href":"b-split6.xhtml#chapter6_verse7","text":"Seven."}]},
    {"heading":"Second article","verses":[{"ref":"Book 8:9","href":"b-split8.xhtml#chapter8_verse9","text":"Nine."}]}]}]})";

void* failAllocation(size_t) { return nullptr; }
void releaseNothing(void*) {}

struct ExhaustedJsonAllocator : ArduinoJson::Allocator {
  void* allocate(size_t) override { return nullptr; }
  void deallocate(void* pointer) override { free(pointer); }
  void* reallocate(void*, size_t) override { return nullptr; }
};

class PreachingHandoutsIo : public ::testing::Test {
 protected:
  void SetUp() override { storage_fake::reset(); }
};

TEST_F(PreachingHandoutsIo, SummariesListEveryHandoutWithItsVerseCount) {
  storage_fake::putFile(FILE_PATH, TWO_HANDOUTS);
  std::vector<PreachingHandouts::Summary> summaries;
  ASSERT_EQ(PreachingHandouts::summaries(nullptr, summaries), Status::Ok);
  ASSERT_EQ(summaries.size(), 2u);
  EXPECT_EQ(summaries[0].title, "Stress");
  EXPECT_EQ(summaries[0].verses, 2);
  EXPECT_EQ(summaries[1].title, "An issue");
  EXPECT_EQ(summaries[1].verses, 2);
}

TEST_F(PreachingHandoutsIo, LoadsOneHandoutsSectionsAndVerses) {
  storage_fake::putFile(FILE_PATH, TWO_HANDOUTS);
  PreachingHandouts::Handout handout;
  ASSERT_EQ(PreachingHandouts::load(1, {}, handout), Status::Ok);
  EXPECT_EQ(handout.title, "An issue");
  ASSERT_EQ(handout.headings.size(), 2u);
  EXPECT_EQ(handout.headings[0], "First article");
  EXPECT_EQ(handout.headings[1], "Second article");
  ASSERT_EQ(handout.verses.size(), 2u);
  EXPECT_EQ(handout.verses[0].section, 0);
  EXPECT_EQ(handout.verses[0].reference, "Book 6:7");
  EXPECT_EQ(handout.verses[0].href, "b-split6.xhtml#chapter6_verse7");
  EXPECT_EQ(handout.verses[0].text, "Seven.");
  EXPECT_EQ(handout.verses[1].section, 1);
  EXPECT_EQ(handout.verses[1].text, "Nine.");
}

TEST_F(PreachingHandoutsIo, ASingleArticleHandoutHasOneEmptyHeading) {
  storage_fake::putFile(FILE_PATH, TWO_HANDOUTS);
  PreachingHandouts::Handout handout;
  ASSERT_EQ(PreachingHandouts::load(0, {}, handout), Status::Ok);
  ASSERT_EQ(handout.headings.size(), 1u);
  EXPECT_TRUE(handout.headings[0].empty());
  ASSERT_EQ(handout.verses.size(), 2u);
  EXPECT_EQ(handout.verses[1].reference, "Book 3:4, 5");
}

TEST_F(PreachingHandoutsIo, AMissingFileIsMissing) {
  std::vector<PreachingHandouts::Summary> summaries;
  EXPECT_EQ(PreachingHandouts::summaries(nullptr, summaries), Status::Missing);
  PreachingHandouts::Handout handout;
  EXPECT_EQ(PreachingHandouts::load(0, {}, handout), Status::Missing);
}

TEST_F(PreachingHandoutsIo, ANewerFormatIsRefusedAsTooNew) {
  storage_fake::putFile(FILE_PATH, R"({"v":2,"cards":[]})");
  std::vector<PreachingHandouts::Summary> summaries;
  EXPECT_EQ(PreachingHandouts::summaries(nullptr, summaries), Status::TooNew);
  EXPECT_TRUE(summaries.empty());
}

TEST_F(PreachingHandoutsIo, UnreadableFilesAreUnreadable) {
  for (const char* contents : {"", R"({"v":0,"cards":[]})", R"({"cards":[]})", R"({"v":1})", R"({"v":1,"cards":[{)"}) {
    storage_fake::putFile(FILE_PATH, contents);
    std::vector<PreachingHandouts::Summary> summaries;
    EXPECT_EQ(PreachingHandouts::summaries(nullptr, summaries), Status::Unreadable) << contents;
    EXPECT_TRUE(summaries.empty()) << contents;
  }
  storage_fake::putFile(FILE_PATH, std::string(PreachingHandouts::MAX_FILE_BYTES + 1, ' '));
  std::vector<PreachingHandouts::Summary> summaries;
  EXPECT_EQ(PreachingHandouts::summaries(nullptr, summaries), Status::Unreadable);
}

TEST_F(PreachingHandoutsIo, AnIndexPastTheEndIsUnreadable) {
  storage_fake::putFile(FILE_PATH, TWO_HANDOUTS);
  PreachingHandouts::Handout handout;
  EXPECT_EQ(PreachingHandouts::load(2, {}, handout), Status::Unreadable);
  EXPECT_TRUE(handout.verses.empty());
}

TEST_F(PreachingHandoutsIo, AVerseWhoseHrefCannotOpenIsSkipped) {
  const std::string longest(PreachingHandouts::MAX_HREF_BYTES, 'x');
  const std::string tooLong(PreachingHandouts::MAX_HREF_BYTES + 1, 'x');
  storage_fake::putFile(FILE_PATH, R"({"v":1,"cards":[{"title":"T","sections":[{"heading":"","verses":[)"
                                   R"({"ref":"A","href":"","text":"a"},)"
                                   R"({"ref":"B","href":")" +
                                       tooLong +
                                       R"(","text":"b"},)"
                                       R"({"ref":"Longest","href":")" +
                                       longest +
                                       R"(","text":"l"},)"
                                       R"({"ref":"C","href":"c.xhtml#chapter1_verse1","text":"c"}]}]}]})");
  std::vector<PreachingHandouts::Summary> summaries;
  ASSERT_EQ(PreachingHandouts::summaries(nullptr, summaries), Status::Ok);
  EXPECT_EQ(summaries[0].verses, 2);
  PreachingHandouts::Handout handout;
  ASSERT_EQ(PreachingHandouts::load(0, {}, handout), Status::Ok);
  ASSERT_EQ(handout.verses.size(), 2u);
  EXPECT_EQ(handout.verses[0].reference, "Longest");
  EXPECT_EQ(handout.verses[0].href, longest);
  EXPECT_EQ(handout.verses[1].reference, "C");
}

TEST_F(PreachingHandoutsIo, ATextThatCannotBeAllocatedIsOutOfMemory) {
  storage_fake::putFile(FILE_PATH, TWO_HANDOUTS);
  PreachingHandouts::Handout handout;

  study::PassageDoc::Allocators noText;
  noText.text = {failAllocation, releaseNothing};
  ASSERT_EQ(PreachingHandouts::load(0, {}, handout), Status::Ok);
  EXPECT_EQ(PreachingHandouts::load(0, noText, handout), Status::OutOfMemory);
  EXPECT_TRUE(handout.title.empty());
  EXPECT_TRUE(handout.headings.empty());
  EXPECT_TRUE(handout.verses.empty());

  ExhaustedJsonAllocator exhausted;
  study::PassageDoc::Allocators noJson;
  noJson.json = &exhausted;
  ASSERT_EQ(PreachingHandouts::load(0, {}, handout), Status::Ok);
  EXPECT_EQ(PreachingHandouts::load(0, noJson, handout), Status::OutOfMemory);
  EXPECT_TRUE(handout.title.empty());
  EXPECT_TRUE(handout.headings.empty());
  EXPECT_TRUE(handout.verses.empty());
}

}  // namespace
