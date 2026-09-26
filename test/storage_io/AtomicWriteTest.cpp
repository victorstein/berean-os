// PersistableStoreBase::writeDocToFileAtomic against the Storage fake: the
// write goes to <path>.tmp, the old file is removed, and the .tmp is renamed
// into place (PersistableStore.cpp:22-44).

#include <ArduinoJson.h>
#include <HalStorage.h>
#include <PersistableStore.h>
#include <gtest/gtest.h>

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
