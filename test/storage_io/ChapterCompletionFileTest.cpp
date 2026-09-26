// ChapterCompletionFile's load and save against the Storage fake.

#include <ArduinoJson.h>
#include <HalStorage.h>
#include <gtest/gtest.h>

#include <string>

#include "ChapterCompletionFile.h"
#include "HalStorageFake.h"
#include "StudyStore/ChapterCompletion.h"

namespace {

const std::string PUB_KEY = "nwt_S";
const std::string PATH = ChapterCompletionFile::path(PUB_KEY);
const std::string TMP_PATH = PATH + ".tmp";

std::string serialised(const study::ChapterCompletion& record) {
  JsonDocument doc;
  EXPECT_TRUE(record.toJsonWithinBudget(doc));
  std::string out;
  serializeJson(doc, out);
  return out;
}

study::ChapterCompletion genesisOneAndPsalm23() {
  study::ChapterCompletion record;
  record.markRead(1, 1);
  record.markRead(19, 23);
  return record;
}

class ChapterCompletionFileIo : public ::testing::Test {
 protected:
  void SetUp() override { storage_fake::reset(); }
};

TEST_F(ChapterCompletionFileIo, SaveThenLoadRoundTripsTheRecord) {
  const study::ChapterCompletion saved = genesisOneAndPsalm23();
  ASSERT_EQ(ChapterCompletionFile::save(PUB_KEY, saved), ChapterCompletionFile::SaveResult::Ok);
  EXPECT_FALSE(Storage.exists(TMP_PATH.c_str()));

  study::ChapterCompletion loaded;
  ASSERT_EQ(ChapterCompletionFile::load(PUB_KEY, loaded), ChapterCompletionFile::LoadResult::Loaded);
  EXPECT_EQ(loaded, saved);
}

TEST_F(ChapterCompletionFileIo, LoadOnAnEmptyCardIsEmpty) {
  study::ChapterCompletion record;
  EXPECT_EQ(ChapterCompletionFile::load(PUB_KEY, record), ChapterCompletionFile::LoadResult::Empty);
}

TEST_F(ChapterCompletionFileIo, AnInterruptedSaveIsRecoveredFromTheTemp) {
  const study::ChapterCompletion saved = genesisOneAndPsalm23();
  storage_fake::putFile(TMP_PATH, serialised(saved));

  study::ChapterCompletion loaded;
  EXPECT_EQ(ChapterCompletionFile::load(PUB_KEY, loaded), ChapterCompletionFile::LoadResult::RecoveredFromTemp);
  EXPECT_EQ(loaded, saved);
  EXPECT_EQ(storage_fake::fileBytes(PATH), serialised(saved));
  EXPECT_FALSE(Storage.exists(TMP_PATH.c_str()));
}

TEST_F(ChapterCompletionFileIo, AGarbageTempIsKeptAndTheLoadIsEmpty) {
  storage_fake::putFile(TMP_PATH, R"({"v":)");
  study::ChapterCompletion record;
  EXPECT_EQ(ChapterCompletionFile::load(PUB_KEY, record), ChapterCompletionFile::LoadResult::Empty);
  EXPECT_EQ(storage_fake::fileBytes(TMP_PATH), R"({"v":)");
  EXPECT_FALSE(Storage.exists(PATH.c_str()));
}

TEST_F(ChapterCompletionFileIo, AnUnreadableRecordFailsAndIsLeftAsItWas) {
  const study::ChapterCompletion saved = genesisOneAndPsalm23();
  storage_fake::putFile(PATH, serialised(saved));
  storage_fake::failReadsOf(PATH);

  study::ChapterCompletion loaded;
  EXPECT_EQ(ChapterCompletionFile::load(PUB_KEY, loaded), ChapterCompletionFile::LoadResult::Failed);
  EXPECT_EQ(storage_fake::fileBytes(PATH), serialised(saved));
}

}  // namespace
