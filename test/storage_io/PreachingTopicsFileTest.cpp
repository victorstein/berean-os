// PreachingTopicsFile's load, latch and save against the Storage fake.

#include <ArduinoJson.h>
#include <HalStorage.h>
#include <gtest/gtest.h>

#include <cstdint>
#include <initializer_list>
#include <string>
#include <vector>

#include "HalStorageFake.h"
#include "PreachingTopicsFile.h"
#include "StudyStore/TagPalette.h"
#include "TagPaletteFile.h"

namespace {

const std::string PATH = PreachingTopicsFile::PATH;
const std::string TMP_PATH = PATH + ".tmp";
const std::string TAGS_PATH = TagPaletteFile::PATH;

std::vector<study::TagId> ids(const std::initializer_list<uint16_t> raw) {
  std::vector<study::TagId> out;
  out.reserve(raw.size());
  for (const uint16_t id : raw) out.push_back(study::toTagId(id));
  return out;
}

// Ids 1..count, all active, written to tags.json.
study::TagPalette seedPalette(const int count) {
  study::TagPalette palette;
  for (int i = 1; i <= count; ++i) palette.add("tag-" + std::to_string(i));
  EXPECT_EQ(TagPaletteFile::save(palette), TagPaletteFile::SaveResult::Ok);
  return palette;
}

class PreachingTopicsFileIo : public ::testing::Test {
 protected:
  void SetUp() override { storage_fake::reset(); }
};

TEST_F(PreachingTopicsFileIo, AMissingFileIsAnEmptySelectionThatMaySave) {
  seedPalette(3);
  PreachingTopicsFile topics;
  EXPECT_EQ(topics.load(), PreachingTopicsFile::LoadResult::Empty);
  EXPECT_TRUE(topics.tags().empty());
  EXPECT_FALSE(topics.saveRefused());
}

TEST_F(PreachingTopicsFileIo, LoadReadsTheStoredIds) {
  seedPalette(3);
  storage_fake::putFile(PATH, R"({"v":1,"tags":[3,1]})");
  PreachingTopicsFile topics;
  EXPECT_EQ(topics.load(), PreachingTopicsFile::LoadResult::Loaded);
  EXPECT_EQ(topics.tags(), ids({3, 1}));
  EXPECT_FALSE(topics.saveRefused());
  EXPECT_TRUE(topics.palette().isActive(study::toTagId(2)));
}

TEST_F(PreachingTopicsFileIo, DuplicateIdsLoadOnce) {
  storage_fake::putFile(PATH, R"({"v":1,"tags":[5,5,9]})");
  PreachingTopicsFile topics;
  EXPECT_EQ(topics.load(), PreachingTopicsFile::LoadResult::Loaded);
  EXPECT_EQ(topics.tags(), ids({5, 9}));
}

TEST_F(PreachingTopicsFileIo, ANewerVersionIsRefusedAndLatchesSaving) {
  const std::string newer = R"({"v":2,"tags":[1]})";
  storage_fake::putFile(PATH, newer);
  PreachingTopicsFile topics;
  EXPECT_EQ(topics.load(), PreachingTopicsFile::LoadResult::Failed);
  EXPECT_TRUE(topics.tags().empty());
  EXPECT_TRUE(topics.saveRefused());
  EXPECT_EQ(topics.save(ids({1})), PreachingTopicsFile::SaveResult::Refused);
  EXPECT_EQ(storage_fake::fileBytes(PATH), newer);
}

TEST_F(PreachingTopicsFileIo, ACorruptFileLatchesSavingAndSurvives) {
  const std::string corrupt = R"({"v":1,"tags":[)";
  storage_fake::putFile(PATH, corrupt);
  PreachingTopicsFile topics;
  EXPECT_EQ(topics.load(), PreachingTopicsFile::LoadResult::Failed);
  EXPECT_TRUE(topics.saveRefused());
  EXPECT_EQ(topics.save(ids({})), PreachingTopicsFile::SaveResult::Refused);
  EXPECT_EQ(storage_fake::fileBytes(PATH), corrupt);
}

TEST_F(PreachingTopicsFileIo, AnUnreadableFileLatchesSaving) {
  storage_fake::putFile(PATH, R"({"v":1,"tags":[1]})");
  storage_fake::failReadsOf(PATH);
  PreachingTopicsFile topics;
  EXPECT_EQ(topics.load(), PreachingTopicsFile::LoadResult::Failed);
  EXPECT_TRUE(topics.saveRefused());
}

TEST_F(PreachingTopicsFileIo, MoreThanTheTagCapIsRefusedAndExactlyTheCapLoads) {
  std::string atCap = R"({"v":1,"tags":[)";
  for (size_t id = 1; id <= study::TagPalette::MAX_ACTIVE_TAGS; ++id) {
    if (id > 1) atCap += ",";
    atCap += std::to_string(id);
  }
  storage_fake::putFile(PATH, atCap + "]}");
  PreachingTopicsFile atCapTopics;
  EXPECT_EQ(atCapTopics.load(), PreachingTopicsFile::LoadResult::Loaded);
  EXPECT_EQ(atCapTopics.tags().size(), study::TagPalette::MAX_ACTIVE_TAGS);

  storage_fake::putFile(PATH, atCap + ",201]}");
  PreachingTopicsFile overCap;
  EXPECT_EQ(overCap.load(), PreachingTopicsFile::LoadResult::Failed);
  EXPECT_TRUE(overCap.saveRefused());
}

TEST_F(PreachingTopicsFileIo, MalformedDocumentsAreRefused) {
  for (const char* malformed : {R"({"tags":[1]})", R"({"v":1,"tags":3})", R"({"v":1})", R"({"v":1,"tags":[0]})",
                                R"({"v":1,"tags":[-1]})", R"({"v":1,"tags":[70000]})", R"({"v":1,"tags":["a"]})",
                                R"([1,2])"}) {
    storage_fake::putFile(PATH, malformed);
    PreachingTopicsFile topics;
    EXPECT_EQ(topics.load(), PreachingTopicsFile::LoadResult::Failed) << malformed;
    EXPECT_TRUE(topics.saveRefused()) << malformed;
  }
}

TEST_F(PreachingTopicsFileIo, AnInterruptedWriteIsRecoveredFromTheTemp) {
  storage_fake::putFile(TMP_PATH, R"({"v":1,"tags":[4]})");
  PreachingTopicsFile topics;
  EXPECT_EQ(topics.load(), PreachingTopicsFile::LoadResult::RecoveredFromTemp);
  EXPECT_EQ(topics.tags(), ids({4}));
  EXPECT_FALSE(topics.saveRefused());
  EXPECT_TRUE(storage_fake::fileBytes(PATH).has_value());
}

TEST_F(PreachingTopicsFileIo, ACorruptPaletteLatchesSavingAndBothFilesSurvive) {
  const std::string corruptPalette = R"({"v":1,"t":[)";
  const std::string picks = R"({"v":1,"tags":[1]})";
  storage_fake::putFile(TAGS_PATH, corruptPalette);
  storage_fake::putFile(PATH, picks);
  PreachingTopicsFile topics;
  EXPECT_EQ(topics.load(), PreachingTopicsFile::LoadResult::Loaded);
  EXPECT_TRUE(topics.saveRefused());
  EXPECT_EQ(topics.save(ids({1})), PreachingTopicsFile::SaveResult::Refused);
  EXPECT_EQ(storage_fake::fileBytes(TAGS_PATH), corruptPalette);
  EXPECT_EQ(storage_fake::fileBytes(PATH), picks);
}

TEST_F(PreachingTopicsFileIo, SavingBeforeLoadingIsRefused) {
  seedPalette(3);
  PreachingTopicsFile topics;
  EXPECT_TRUE(topics.saveRefused());
  EXPECT_EQ(topics.save(ids({1})), PreachingTopicsFile::SaveResult::Refused);
  EXPECT_FALSE(storage_fake::fileBytes(PATH).has_value());
}

}  // namespace
