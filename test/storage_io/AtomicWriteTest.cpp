// PersistableStoreBase::writeDocToFileAtomic against the Storage fake: the
// write goes to <path>.tmp, the old file is removed, and the .tmp is renamed
// into place.

#include <ArduinoJson.h>
#include <HalStorage.h>
#include <PersistableStore.h>
#include <gtest/gtest.h>

#include <string>

#include "HalStorageFake.h"

namespace {

constexpr const char* PATH = "/.crosspoint/store.json";
constexpr const char* TMP_PATH = "/.crosspoint/store.json.tmp";

JsonDocument docWithValue(int value) {
  JsonDocument doc;
  doc["v"] = value;
  return doc;
}

class AtomicWrite : public ::testing::Test {
 protected:
  void SetUp() override { storage_fake::reset(); }
};

TEST_F(AtomicWrite, AFreshWriteLeavesExactlyTheSerialisedBytesAndNoTemp) {
  ASSERT_TRUE(PersistableStoreBase::writeDocToFileAtomic(PATH, docWithValue(1)));
  EXPECT_EQ(storage_fake::fileBytes(PATH), R"({"v":1})");
  EXPECT_FALSE(Storage.exists(TMP_PATH));
  EXPECT_TRUE(storage_fake::isDir("/.crosspoint"));
}

TEST_F(AtomicWrite, OverwritesAnExistingPrimary) {
  storage_fake::putFile(PATH, R"({"v":1})");
  ASSERT_TRUE(PersistableStoreBase::writeDocToFileAtomic(PATH, docWithValue(2)));
  EXPECT_EQ(storage_fake::fileBytes(PATH), R"({"v":2})");
}

TEST_F(AtomicWrite, AStaleTempFromAnEarlierCrashDoesNotBlockTheWrite) {
  storage_fake::putFile(PATH, R"({"v":1})");
  storage_fake::putFile(TMP_PATH, "half a wri");
  ASSERT_TRUE(PersistableStoreBase::writeDocToFileAtomic(PATH, docWithValue(2)));
  EXPECT_EQ(storage_fake::fileBytes(PATH), R"({"v":2})");
  EXPECT_FALSE(Storage.exists(TMP_PATH));
}

TEST_F(AtomicWrite, AFailedTempWriteLeavesThePrimaryUntouched) {
  storage_fake::putFile(PATH, R"({"v":1})");
  storage_fake::failWritesTo(TMP_PATH);
  EXPECT_FALSE(PersistableStoreBase::writeDocToFileAtomic(PATH, docWithValue(2)));
  EXPECT_EQ(storage_fake::fileBytes(PATH), R"({"v":1})");
}

TEST_F(AtomicWrite, CreatesTheTargetsOwnParentAndNotCrosspoint) {
  ASSERT_TRUE(PersistableStoreBase::writeDocToFileAtomic("/.berean/x/store.json", docWithValue(1)));
  EXPECT_EQ(storage_fake::fileBytes("/.berean/x/store.json"), R"({"v":1})");
  EXPECT_FALSE(storage_fake::isDir("/.crosspoint"));
}

TEST_F(AtomicWrite, TheNonAtomicWriteCreatesTheTargetsOwnParentAndNotCrosspoint) {
  ASSERT_TRUE(PersistableStoreBase::writeDocToFile("/.berean/x/store.json", docWithValue(1)));
  EXPECT_EQ(storage_fake::fileBytes("/.berean/x/store.json"), R"({"v":1})");
  EXPECT_FALSE(storage_fake::isDir("/.crosspoint"));
}

TEST_F(AtomicWrite, AShortTempWriteLeavesThePrimaryUntouchedAndTheTempBehind) {
  storage_fake::putFile(PATH, R"({"v":1})");
  storage_fake::failWritesAfter(TMP_PATH, 3);
  EXPECT_FALSE(PersistableStoreBase::writeDocToFileAtomic(PATH, docWithValue(2)));
  EXPECT_EQ(storage_fake::fileBytes(PATH), R"({"v":1})");
  EXPECT_EQ(storage_fake::fileBytes(TMP_PATH), R"({"v)");
}

TEST_F(AtomicWrite, AShortWritePastTheBufferIsCaughtToo) {
  JsonDocument doc;
  doc["s"] = std::string(4000, 'x');
  storage_fake::failWritesAfter(TMP_PATH, 1500);
  EXPECT_FALSE(PersistableStoreBase::writeDocToFileAtomic(PATH, doc));
  EXPECT_FALSE(Storage.exists(PATH));
}

TEST_F(AtomicWrite, ADocumentPastTheReadCapIsStreamedWholeAndReadBackWhole) {
  JsonDocument doc;
  doc["s"] = std::string(60000, 'x');
  ASSERT_TRUE(PersistableStoreBase::writeDocToFileAtomic(PATH, doc));
  EXPECT_EQ(storage_fake::fileBytes(PATH)->size(), 60008u);

  JsonDocument read;
  ASSERT_EQ(PersistableStoreBase::readDocFromFileStreamed(PATH, read), DocReadStatus::Ok);
  EXPECT_EQ(read["s"].as<std::string>().size(), 60000u);
}

TEST_F(AtomicWrite, TheStreamedReadPullsTheFileInChunksNotBytes) {
  JsonDocument doc;
  doc["s"] = std::string(60000, 'x');
  ASSERT_TRUE(PersistableStoreBase::writeDocToFileAtomic(PATH, doc));

  JsonDocument read;
  ASSERT_EQ(PersistableStoreBase::readDocFromFileStreamed(PATH, read), DocReadStatus::Ok);
  EXPECT_LT(storage_fake::readCallsOn(PATH), 600u)
      << "every HalFile::read takes storageMutex on the device; a 60 KB file must not cost one per byte";
}

TEST_F(AtomicWrite, AFailedRenameLeavesOnlyTheTempWhichTheNextReadAdopts) {
  storage_fake::putFile(PATH, R"({"v":1})");
  storage_fake::failRenamesFrom(TMP_PATH);
  EXPECT_FALSE(PersistableStoreBase::writeDocToFileAtomic(PATH, docWithValue(2)));
  EXPECT_FALSE(Storage.exists(PATH));
  EXPECT_EQ(storage_fake::fileBytes(TMP_PATH), R"({"v":2})");

  storage_fake::clearFailures();
  JsonDocument doc;
  EXPECT_EQ(PersistableStoreBase::readDocFromFileAdopting(PATH, doc), DocReadStatus::Ok);
  EXPECT_EQ(doc["v"].as<int>(), 2);
  EXPECT_EQ(storage_fake::fileBytes(PATH), R"({"v":2})");
  EXPECT_FALSE(Storage.exists(TMP_PATH));
}

}  // namespace
