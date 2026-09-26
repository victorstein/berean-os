// TagPaletteFile's load and save round trip against the Storage fake.

#include <ArduinoJson.h>
#include <HalStorage.h>
#include <gtest/gtest.h>

#include <cstdio>
#include <string>

#include "HalStorageFake.h"
#include "StudyStore/TagPalette.h"
#include "TagPaletteFile.h"

namespace {

const std::string PATH = TagPaletteFile::PATH;
const std::string TMP_PATH = PATH + ".tmp";

std::string serialised(const study::TagPalette& palette) {
  JsonDocument doc;
  palette.toJson(doc);
  std::string out;
  serializeJson(doc, out);
  return out;
}

// Retired tags stay in the file forever, so a palette can outgrow the save
// budget while never exceeding MAX_ACTIVE_TAGS.
study::TagPalette overBudgetPalette() {
  study::TagPalette palette;
  char name[study::TagPalette::MAX_TAG_NAME_BYTES + 1];
  for (int i = 0; i < 2000; ++i) {
    snprintf(name, sizeof(name), "tag-%020d", i);
    const auto id = palette.add(name);
    if (id) palette.retire(*id);
  }
  return palette;
}

class TagPaletteFileIo : public ::testing::Test {
 protected:
  void SetUp() override { storage_fake::reset(); }
};

TEST_F(TagPaletteFileIo, SaveThenLoadRoundTripsTheTags) {
  study::TagPalette saved;
  const auto prayer = saved.add("oracion");
  const auto hope = saved.add("esperanza");
  ASSERT_TRUE(prayer && hope);
  saved.retire(*hope);

  ASSERT_EQ(TagPaletteFile::save(saved), TagPaletteFile::SaveResult::Ok);
  EXPECT_FALSE(Storage.exists(TMP_PATH.c_str()));

  study::TagPalette loaded;
  ASSERT_EQ(TagPaletteFile::load(loaded), TagPaletteFile::LoadResult::Loaded);
  EXPECT_EQ(loaded.name(*prayer), "oracion");
  EXPECT_TRUE(loaded.isActive(*prayer));
  EXPECT_EQ(loaded.name(*hope), "esperanza");
  EXPECT_FALSE(loaded.isActive(*hope));
}

TEST_F(TagPaletteFileIo, LoadOnAnEmptyCardIsEmpty) {
  study::TagPalette palette;
  EXPECT_EQ(TagPaletteFile::load(palette), TagPaletteFile::LoadResult::Empty);
}

TEST_F(TagPaletteFileIo, AnInterruptedSaveIsRecoveredFromTheTemp) {
  study::TagPalette saved;
  const auto prayer = saved.add("oracion");
  ASSERT_TRUE(prayer);
  storage_fake::putFile(TMP_PATH, serialised(saved));

  study::TagPalette loaded;
  EXPECT_EQ(TagPaletteFile::load(loaded), TagPaletteFile::LoadResult::RecoveredFromTemp);
  EXPECT_EQ(loaded.name(*prayer), "oracion");
  EXPECT_EQ(storage_fake::fileBytes(PATH), serialised(saved));
  EXPECT_FALSE(Storage.exists(TMP_PATH.c_str()));
}

TEST_F(TagPaletteFileIo, AGarbageTempIsKeptAndTheLoadIsEmpty) {
  storage_fake::putFile(TMP_PATH, R"({"v":)");
  study::TagPalette palette;
  EXPECT_EQ(TagPaletteFile::load(palette), TagPaletteFile::LoadResult::Empty);
  EXPECT_EQ(storage_fake::fileBytes(TMP_PATH), R"({"v":)");
  EXPECT_FALSE(Storage.exists(PATH.c_str()));
}

TEST_F(TagPaletteFileIo, AnOverBudgetPaletteIsRefusedAndNothingIsWritten) {
  EXPECT_EQ(TagPaletteFile::save(overBudgetPalette()), TagPaletteFile::SaveResult::TooLarge);
  EXPECT_FALSE(Storage.exists(PATH.c_str()));
  EXPECT_FALSE(Storage.exists(TMP_PATH.c_str()));
}

TEST_F(TagPaletteFileIo, AnUnreadablePaletteFailsAndIsLeftAsItWas) {
  study::TagPalette saved;
  ASSERT_TRUE(saved.add("oracion"));
  storage_fake::putFile(PATH, serialised(saved));
  storage_fake::failReadsOf(PATH);

  study::TagPalette loaded;
  EXPECT_EQ(TagPaletteFile::load(loaded), TagPaletteFile::LoadResult::Failed);
  EXPECT_EQ(storage_fake::fileBytes(PATH), serialised(saved));
}

TEST_F(TagPaletteFileIo, AFailedSaveKeepsThePreviousPalette) {
  study::TagPalette before;
  ASSERT_TRUE(before.add("oracion"));
  storage_fake::putFile(PATH, serialised(before));
  storage_fake::failWritesTo(TMP_PATH);

  study::TagPalette after = before;
  ASSERT_TRUE(after.add("esperanza"));
  EXPECT_EQ(TagPaletteFile::save(after), TagPaletteFile::SaveResult::WriteFailed);
  EXPECT_EQ(storage_fake::fileBytes(PATH), serialised(before));
}

}  // namespace
