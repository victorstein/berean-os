// PassageFile's load and save against the Storage fake. PassageFile streams its
// reads, so a file past SDCardManager::readFile's 50,000-byte cap must still
// load -- from the .tmp as well as from the primary.

#include <ArduinoJson.h>
#include <HalStorage.h>
#include <gtest/gtest.h>

#include <string>

#include "HalStorageFake.h"
#include "PassageFile.h"
#include "StudyStore/PassageDoc.h"

namespace {

const std::string PUB_KEY = "nwt_S";
const std::string PATH = PassageFile::path(PUB_KEY);
const std::string TMP_PATH = PATH + ".tmp";

study::TaggedPassage samplePassage() {
  study::TaggedPassage p;
  p.start = study::Unit{study::UnitKind::Verse, 19, 119, 145, 0};
  p.end = study::Unit{study::UnitKind::Verse, 19, 119, 145, 108};
  p.fingerprint = study::Fingerprint{114, 0xa1b2c3d4};
  p.document = "1001061130-split10.xhtml";
  p.documentSpine = 198;
  p.snippet = "Te he llamado con todo el corazon";
  p.reference = "Salmos 119:145";
  p.tags = {study::toTagId(3), study::toTagId(17)};
  return p;
}

std::string serialised(const study::PassageDoc& doc) {
  JsonDocument json;
  doc.toJson(json);
  std::string out;
  serializeJson(json, out);
  return out;
}

// Past the 50,000-byte readFile cap, inside PassageDoc::SAVE_BYTE_BUDGET.
study::PassageDoc pastTheReadCap() {
  study::PassageDoc doc;
  for (int i = 0; i < 300; ++i) {
    study::TaggedPassage p = samplePassage();
    p.snippet = std::string(study::PassageDoc::MAX_SNIPPET_BYTES - 4, 'a') + std::to_string(i);
    EXPECT_TRUE(doc.add(p));
  }
  return doc;
}

class PassageFileIo : public ::testing::Test {
 protected:
  void SetUp() override { storage_fake::reset(); }
};

TEST_F(PassageFileIo, SaveThenLoadRoundTripsThePassages) {
  study::PassageDoc saved;
  ASSERT_TRUE(saved.add(samplePassage()));
  ASSERT_EQ(PassageFile::save(PUB_KEY, saved), PassageFile::SaveResult::Ok);
  EXPECT_FALSE(Storage.exists(TMP_PATH.c_str()));

  study::PassageDoc loaded;
  ASSERT_EQ(PassageFile::load(PUB_KEY, loaded), PassageFile::LoadResult::Loaded);
  ASSERT_EQ(loaded.passages().size(), 1u);
  EXPECT_EQ(loaded.passages()[0].reference, "Salmos 119:145");
}

TEST_F(PassageFileIo, LoadOnAnEmptyCardIsEmpty) {
  study::PassageDoc doc;
  EXPECT_EQ(PassageFile::load(PUB_KEY, doc), PassageFile::LoadResult::Empty);
}

TEST_F(PassageFileIo, ATempPastTheReadCapIsRecoveredWhole) {
  const study::PassageDoc saved = pastTheReadCap();
  const std::string bytes = serialised(saved);
  ASSERT_GT(bytes.size(), 50000u) << "the fixture must exceed the readFile cap to prove anything";
  storage_fake::putFile(TMP_PATH, bytes);

  study::PassageDoc loaded;
  EXPECT_EQ(PassageFile::load(PUB_KEY, loaded), PassageFile::LoadResult::RecoveredFromTemp);
  EXPECT_EQ(loaded.passages().size(), saved.passages().size());
  EXPECT_EQ(storage_fake::fileBytes(PATH), bytes);
  EXPECT_FALSE(Storage.exists(TMP_PATH.c_str()));
}

TEST_F(PassageFileIo, AGarbageTempIsKeptAndTheLoadIsEmpty) {
  storage_fake::putFile(TMP_PATH, R"({"v":)");
  study::PassageDoc doc;
  EXPECT_EQ(PassageFile::load(PUB_KEY, doc), PassageFile::LoadResult::Empty);
  EXPECT_EQ(storage_fake::fileBytes(TMP_PATH), R"({"v":)");
  EXPECT_FALSE(Storage.exists(PATH.c_str()));
}

// A zero-byte .tmp is what a write interrupted before its first byte leaves.
TEST_F(PassageFileIo, AnEmptyTempIsKeptAndTheLoadIsEmpty) {
  storage_fake::putFile(TMP_PATH, "");
  study::PassageDoc doc;
  EXPECT_EQ(PassageFile::load(PUB_KEY, doc), PassageFile::LoadResult::Empty);
  EXPECT_EQ(storage_fake::fileBytes(TMP_PATH), "");
}

TEST_F(PassageFileIo, AnUnreadablePrimaryFailsAndIsLeftAsItWas) {
  study::PassageDoc saved;
  ASSERT_TRUE(saved.add(samplePassage()));
  storage_fake::putFile(PATH, serialised(saved));
  storage_fake::failReadsOf(PATH);

  study::PassageDoc loaded;
  EXPECT_EQ(PassageFile::load(PUB_KEY, loaded), PassageFile::LoadResult::Failed);
  EXPECT_EQ(storage_fake::fileBytes(PATH), serialised(saved));
}

}  // namespace
