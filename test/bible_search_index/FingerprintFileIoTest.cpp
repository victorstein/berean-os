// The fingerprint record against the in-memory card: one read to load it, and a
// temp-then-rename save that never leaves a torn record.

#include <HalStorage.h>
#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "FingerprintFixtures.h"
#include "HalStorageFake.h"

namespace {

namespace bsfp = BibleSearchFingerprintFile;
using bsfp::Load;
using fingerprint_fixtures::asString;
using fingerprint_fixtures::encoded;
using fingerprint_fixtures::key;
using fingerprint_fixtures::record;
using fingerprint_fixtures::setVersion;

const std::string CACHE = "/.crosspoint/epub_1234";
const std::string PATH = CACHE + "/search-fingerprint.bin";
const std::string TMP_PATH = PATH + ".tmp";

bsfp::Record otherRecord() {
  bsfp::Record r = record();
  r.fingerprint = 0x0102030405060708ull;
  return r;
}

class FingerprintFileIo : public ::testing::Test {
 protected:
  void SetUp() override {
    storage_fake::reset();
    Storage.mkdir(CACHE.c_str());
  }

  static Load loadNow() {
    bsfp::Record out;
    return bsfp::load(CACHE, key(), out);
  }
};

}  // namespace

TEST_F(FingerprintFileIo, ThePathIsBesideBookBin) { EXPECT_EQ(bsfp::pathIn(CACHE), PATH); }

TEST_F(FingerprintFileIo, SaveLeavesExactlyTheRecordAndNoTemp) {
  ASSERT_TRUE(bsfp::save(CACHE, record()));
  EXPECT_EQ(storage_fake::fileBytes(PATH), asString(encoded()));
  EXPECT_FALSE(Storage.exists(TMP_PATH.c_str()));
}

TEST_F(FingerprintFileIo, SavedThenLoadedRoundTrips) {
  ASSERT_TRUE(bsfp::save(CACHE, record()));
  bsfp::Record out;
  ASSERT_EQ(bsfp::load(CACHE, key(), out), Load::Ok);
  EXPECT_EQ(out.fingerprint, record().fingerprint);
  EXPECT_EQ(out.verseDocuments, record().verseDocuments);
}

TEST_F(FingerprintFileIo, SaveReplacesAnEarlierRecord) {
  storage_fake::putFile(PATH, asString(encoded(otherRecord())));
  ASSERT_TRUE(bsfp::save(CACHE, record()));
  EXPECT_EQ(storage_fake::fileBytes(PATH), asString(encoded()));
}

TEST_F(FingerprintFileIo, AFailedTempWriteKeepsThePreviousRecord) {
  const std::string previous = asString(encoded(otherRecord()));
  storage_fake::putFile(PATH, previous);
  storage_fake::failWritesTo(TMP_PATH);
  EXPECT_FALSE(bsfp::save(CACHE, record()));
  EXPECT_EQ(storage_fake::fileBytes(PATH), previous);
}

TEST_F(FingerprintFileIo, AShortTempWriteKeepsThePreviousRecordAndLeavesNoTemp) {
  const std::string previous = asString(encoded(otherRecord()));
  storage_fake::putFile(PATH, previous);
  storage_fake::failWritesAfter(TMP_PATH, 20);
  EXPECT_FALSE(bsfp::save(CACHE, record()));
  EXPECT_EQ(storage_fake::fileBytes(PATH), previous);
  EXPECT_FALSE(Storage.exists(TMP_PATH.c_str()));
}

TEST_F(FingerprintFileIo, AFailedRenameReportsFailure) {
  storage_fake::failRenamesFrom(TMP_PATH);
  EXPECT_FALSE(bsfp::save(CACHE, record()));
}

TEST_F(FingerprintFileIo, AnAbsentRecordIsMissing) { EXPECT_EQ(loadNow(), Load::Missing); }

TEST_F(FingerprintFileIo, ARecordThatCannotBeOpenedIsUnreadable) {
  storage_fake::putFile(PATH, asString(encoded()));
  storage_fake::failReadsOf(PATH);
  EXPECT_EQ(loadNow(), Load::Unreadable);
}

TEST_F(FingerprintFileIo, ATruncatedRecordIsCorrupt) {
  storage_fake::putFile(PATH, asString(encoded()).substr(0, 20));
  EXPECT_EQ(loadNow(), Load::Corrupt);
}

TEST_F(FingerprintFileIo, ARecordWithATrailingByteIsCorrupt) {
  storage_fake::putFile(PATH, asString(encoded()) + "x");
  EXPECT_EQ(loadNow(), Load::Corrupt);
}

TEST_F(FingerprintFileIo, ANewerLongerRecordIsTooNew) {
  std::vector<uint8_t> bytes = encoded();
  setVersion(bytes, static_cast<uint16_t>(bsfp::VERSION + 1));
  bytes.resize(48, 0);
  storage_fake::putFile(PATH, asString(bytes));
  EXPECT_EQ(loadNow(), Load::TooNew);
}

TEST_F(FingerprintFileIo, LoadingIsOneRead) {
  storage_fake::putFile(PATH, asString(encoded()));
  ASSERT_EQ(loadNow(), Load::Ok);
  EXPECT_EQ(storage_fake::readCallsOn(PATH), 1u);
}
