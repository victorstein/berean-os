// PersistableStoreBase::loadAdopting against the Storage fake. The result
// mapping is covered by test/temp_adoption; this suite covers the card
// operations behind each result, and that the caller's own reader and acceptor
// are the ones used.

#include <ArduinoJson.h>
#include <HalStorage.h>
#include <PersistableStore.h>
#include <gtest/gtest.h>

#include "HalStorageFake.h"

namespace {

constexpr const char* PATH = "/.berean/doc.json";
constexpr const char* TMP_PATH = "/.berean/doc.json.tmp";

struct Target {
  int v = -1;
  bool accept = true;
  int calls = 0;
};

bool acceptInto(void* target, JsonVariantConst json) {
  auto* into = static_cast<Target*>(target);
  ++into->calls;
  into->v = json["v"] | -1;
  return into->accept;
}

int readerCalls = 0;

DocReadStatus countingReader(const char* path, JsonDocument& doc) {
  ++readerCalls;
  return PersistableStoreBase::readDocFromFileChecked(path, doc);
}

class LoadAdopting : public ::testing::Test {
 protected:
  void SetUp() override {
    storage_fake::reset();
    readerCalls = 0;
  }

  AdoptedLoad load() {
    return PersistableStoreBase::loadAdopting(PATH, &PersistableStoreBase::readDocFromFileChecked, acceptInto,
                                              &target);
  }

  Target target;
};

TEST_F(LoadAdopting, AnAcceptedPrimaryIsLoadedAndTheTempIsLeftAlone) {
  storage_fake::putFile(PATH, R"({"v":1})");
  storage_fake::putFile(TMP_PATH, R"({"v":2})");
  EXPECT_EQ(load(), AdoptedLoad::Loaded);
  EXPECT_EQ(target.v, 1);
  EXPECT_EQ(storage_fake::fileBytes(TMP_PATH), R"({"v":2})");
}

TEST_F(LoadAdopting, ARejectedPrimaryFailsAndTheCardIsUntouched) {
  storage_fake::putFile(PATH, R"({"v":1})");
  target.accept = false;
  EXPECT_EQ(load(), AdoptedLoad::Failed);
  EXPECT_EQ(storage_fake::fileBytes(PATH), R"({"v":1})");
  EXPECT_FALSE(Storage.exists(TMP_PATH));
}

TEST_F(LoadAdopting, AnUnreadablePrimaryFailsAndTheTempIsNeverConsulted) {
  storage_fake::putFile(PATH, R"({"v":1})");
  storage_fake::putFile(TMP_PATH, R"({"v":2})");
  storage_fake::failReadsOf(PATH);
  EXPECT_EQ(load(), AdoptedLoad::Failed);
  EXPECT_EQ(target.calls, 0);
  EXPECT_EQ(storage_fake::fileBytes(PATH), R"({"v":1})");
  EXPECT_EQ(storage_fake::fileBytes(TMP_PATH), R"({"v":2})");
}

TEST_F(LoadAdopting, AGarbagePrimaryFailsAndTheTempIsNeverPromoted) {
  storage_fake::putFile(PATH, "not json");
  storage_fake::putFile(TMP_PATH, R"({"v":2})");
  EXPECT_EQ(load(), AdoptedLoad::Failed);
  EXPECT_EQ(target.calls, 0);
  EXPECT_EQ(storage_fake::fileBytes(PATH), "not json");
  EXPECT_EQ(storage_fake::fileBytes(TMP_PATH), R"({"v":2})");
}

TEST_F(LoadAdopting, NothingOnTheCardIsEmpty) {
  EXPECT_EQ(load(), AdoptedLoad::Empty);
  EXPECT_EQ(target.calls, 0);
}

TEST_F(LoadAdopting, AParseableTempIsPromotedAndRecovered) {
  storage_fake::putFile(TMP_PATH, R"({"v":2})");
  EXPECT_EQ(load(), AdoptedLoad::RecoveredFromTemp);
  EXPECT_EQ(target.v, 2);
  EXPECT_EQ(storage_fake::fileBytes(PATH), R"({"v":2})");
  EXPECT_FALSE(Storage.exists(TMP_PATH));
}

TEST_F(LoadAdopting, ARejectedTempFailsButIsStillPromoted) {
  storage_fake::putFile(TMP_PATH, R"({"v":2})");
  target.accept = false;
  EXPECT_EQ(load(), AdoptedLoad::Failed);
  EXPECT_EQ(storage_fake::fileBytes(PATH), R"({"v":2})");
  EXPECT_FALSE(Storage.exists(TMP_PATH));
}

TEST_F(LoadAdopting, AFailedPromotionStillRecoversAndKeepsTheTemp) {
  storage_fake::putFile(TMP_PATH, R"({"v":2})");
  storage_fake::failRenamesFrom(TMP_PATH);
  EXPECT_EQ(load(), AdoptedLoad::RecoveredFromTemp);
  EXPECT_EQ(target.v, 2);
  EXPECT_FALSE(Storage.exists(PATH));
  EXPECT_EQ(storage_fake::fileBytes(TMP_PATH), R"({"v":2})");
}

// Issue #98's case.
TEST_F(LoadAdopting, AGarbageTempIsKeptAndTheLoadIsEmpty) {
  storage_fake::putFile(TMP_PATH, R"({"v":)");
  EXPECT_EQ(load(), AdoptedLoad::Empty);
  EXPECT_EQ(target.calls, 0);
  EXPECT_EQ(storage_fake::fileBytes(TMP_PATH), R"({"v":)");
  EXPECT_FALSE(Storage.exists(PATH));
}

// The transient-failure case: the .tmp's bytes may be intact.
TEST_F(LoadAdopting, AnUnreadableTempIsKeptAndTheLoadIsEmpty) {
  storage_fake::putFile(TMP_PATH, R"({"v":2})");
  storage_fake::failReadsOf(TMP_PATH);
  EXPECT_EQ(load(), AdoptedLoad::Empty);
  EXPECT_EQ(target.calls, 0);
  EXPECT_EQ(storage_fake::fileBytes(TMP_PATH), R"({"v":2})");
  EXPECT_FALSE(Storage.exists(PATH));
}

TEST_F(LoadAdopting, TheSuppliedReaderReadsBothThePrimaryAndTheTemp) {
  storage_fake::putFile(TMP_PATH, R"({"v":2})");
  EXPECT_EQ(PersistableStoreBase::loadAdopting(PATH, countingReader, acceptInto, &target),
            AdoptedLoad::RecoveredFromTemp);
  EXPECT_EQ(readerCalls, 2);
  EXPECT_EQ(target.v, 2);
}

}  // namespace
