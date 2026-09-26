// PersistableStoreBase::readDocFromFileAdopting against the Storage fake. The
// decision table is covered by test/temp_adoption; this suite covers the card
// operations that carry each decision out.

#include <ArduinoJson.h>
#include <HalStorage.h>
#include <PersistableStore.h>
#include <SaveBudget.h>
#include <gtest/gtest.h>

#include <string>

#include "HalStorageFake.h"

namespace {

constexpr const char* PATH = "/.berean/doc.json";
constexpr const char* TMP_PATH = "/.berean/doc.json.tmp";

// The bytes jsonOfSize spends on {"s":""} around its string value.
constexpr size_t JSON_OVERHEAD = sizeof(R"({"s":""})") - 1;

// A valid JSON document of exactly `bytes` bytes: {"s":"xxx..."}.
std::string jsonOfSize(size_t bytes) {
  return R"({"s":")" + std::string(bytes - JSON_OVERHEAD, 'x') + R"("})";
}

class AdoptingRead : public ::testing::Test {
 protected:
  void SetUp() override { storage_fake::reset(); }
  JsonDocument doc;
};

TEST_F(AdoptingRead, AGoodPrimaryWinsAndTheTempIsLeftAlone) {
  storage_fake::putFile(PATH, R"({"v":1})");
  storage_fake::putFile(TMP_PATH, R"({"v":2})");
  EXPECT_EQ(PersistableStoreBase::readDocFromFileAdopting(PATH, doc), DocReadStatus::Ok);
  EXPECT_EQ(doc["v"].as<int>(), 1);
  EXPECT_EQ(storage_fake::fileBytes(TMP_PATH), R"({"v":2})");
}

TEST_F(AdoptingRead, NothingOnTheCardIsMissing) {
  EXPECT_EQ(PersistableStoreBase::readDocFromFileAdopting(PATH, doc), DocReadStatus::Missing);
}

TEST_F(AdoptingRead, AParseableTempIsPromotedIntoPlace) {
  storage_fake::putFile(TMP_PATH, R"({"v":2})");
  EXPECT_EQ(PersistableStoreBase::readDocFromFileAdopting(PATH, doc), DocReadStatus::Ok);
  EXPECT_EQ(doc["v"].as<int>(), 2);
  EXPECT_EQ(storage_fake::fileBytes(PATH), R"({"v":2})");
  EXPECT_FALSE(Storage.exists(TMP_PATH));
}

TEST_F(AdoptingRead, AFailedPromotionStillReturnsTheDocumentAndKeepsTheTemp) {
  storage_fake::putFile(TMP_PATH, R"({"v":2})");
  storage_fake::failRenamesFrom(TMP_PATH);
  EXPECT_EQ(PersistableStoreBase::readDocFromFileAdopting(PATH, doc), DocReadStatus::Ok);
  EXPECT_EQ(doc["v"].as<int>(), 2);
  EXPECT_FALSE(Storage.exists(PATH));
  EXPECT_EQ(storage_fake::fileBytes(TMP_PATH), R"({"v":2})");
}

TEST_F(AdoptingRead, AnUnparseableTempReportsMissingAndStaysOnTheCard) {
  storage_fake::putFile(TMP_PATH, R"({"v":)");
  EXPECT_EQ(PersistableStoreBase::readDocFromFileAdopting(PATH, doc), DocReadStatus::Missing);
  EXPECT_TRUE(doc.isNull());
  EXPECT_EQ(storage_fake::fileBytes(TMP_PATH), R"({"v":)");
  EXPECT_FALSE(Storage.exists(PATH));
}

TEST_F(AdoptingRead, AnUnreadablePrimaryIsUnreadableAndTheTempIsNeverConsulted) {
  storage_fake::putFile(PATH, R"({"v":1})");
  storage_fake::putFile(TMP_PATH, R"({"v":2})");
  storage_fake::failReadsOf(PATH);
  EXPECT_EQ(PersistableStoreBase::readDocFromFileAdopting(PATH, doc), DocReadStatus::Unreadable);
  EXPECT_EQ(storage_fake::fileBytes(PATH), R"({"v":1})");
  EXPECT_EQ(storage_fake::fileBytes(TMP_PATH), R"({"v":2})");
}

TEST_F(AdoptingRead, AnEmptyPrimaryIsUnreadable) {
  storage_fake::putFile(PATH, "");
  EXPECT_EQ(PersistableStoreBase::readDocFromFileAdopting(PATH, doc), DocReadStatus::Unreadable);
}

TEST_F(AdoptingRead, AGarbagePrimaryIsAParseErrorAndTheTempIsNeverPromoted) {
  storage_fake::putFile(PATH, "not json");
  storage_fake::putFile(TMP_PATH, R"({"v":2})");
  EXPECT_EQ(PersistableStoreBase::readDocFromFileAdopting(PATH, doc), DocReadStatus::ParseError);
  EXPECT_EQ(storage_fake::fileBytes(PATH), "not json");
  EXPECT_EQ(storage_fake::fileBytes(TMP_PATH), R"({"v":2})");
}

// The chain CLAUDE.md warns about: a file past SDCardManager::readFile's
// 50,000-byte cap is read back truncated mid-token and fails to parse.
TEST_F(AdoptingRead, AValidDocumentPastTheReadCapIsAParseError) {
  storage_fake::putFile(PATH, jsonOfSize(50001));
  EXPECT_EQ(PersistableStoreBase::readDocFromFileAdopting(PATH, doc), DocReadStatus::ParseError);
}

TEST_F(AdoptingRead, ADocumentAtTheSaveBudgetReadsBackWhole) {
  storage_fake::putFile(PATH, jsonOfSize(persist::DEFAULT_SAVE_BUDGET));
  EXPECT_EQ(PersistableStoreBase::readDocFromFileAdopting(PATH, doc), DocReadStatus::Ok);
  EXPECT_EQ(doc["s"].as<std::string>().size(), persist::DEFAULT_SAVE_BUDGET - JSON_OVERHEAD);
}

}  // namespace
