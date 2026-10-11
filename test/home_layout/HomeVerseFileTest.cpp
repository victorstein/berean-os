// The Home verse file on the in-memory card (spec berean-o-7, A7-A12): what reaches the card, what
// is refused, and a .tmp left by an interrupted rename. Modelled on test/places_store.

#include <ArduinoJson.h>
#include <HalStorageFake.h>
#include <SdPaths.h>
#include <gtest/gtest.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "activities/launcher/HomeVerseFile.h"

namespace {

const std::string PATH = sdpaths::HOME_VERSE_FILE;
constexpr const char* BUILD = "1a2b3c4d";
constexpr uint8_t RUNGS = 3;

// A pick as HomeVerse::scan leaves one.
home_verse::Entry pickEntry() {
  home_verse::Entry entry;
  entry.valid = true;
  entry.key.dated = true;
  entry.key.day.year = 2026;
  entry.key.day.month = 10;
  entry.key.day.day = 11;
  entry.key.fileExists = true;
  entry.key.fileBytes = 43864;
  entry.empty = home_verse::Empty::None;
  entry.hasPick = true;
  entry.pick.start = study::Unit{study::UnitKind::Verse, 43, 3, 16, 0};
  entry.pick.spine = 1201;
  snprintf(entry.pick.reference, sizeof(entry.pick.reference), "%s", "John 3:16");
  entry.pick.rung = 1;
  home_verse::packLines({"For God loved the world", "so much that he gave"}, entry.pick);
  return entry;
}

struct CountdownJsonAllocator : ArduinoJson::Allocator {
  int remaining = 1 << 30;
  int allocations = 0;
  void* allocate(const size_t size) override {
    if (remaining <= 0) return nullptr;
    --remaining;
    ++allocations;
    return malloc(size);
  }
  void deallocate(void* pointer) override { free(pointer); }
  void* reallocate(void* pointer, const size_t size) override {
    if (remaining <= 0) return nullptr;
    --remaining;
    ++allocations;
    return realloc(pointer, size);
  }
};

class HomeVerseFileTest : public ::testing::Test {
 protected:
  void SetUp() override { storage_fake::reset(); }
};

}  // namespace

TEST_F(HomeVerseFileTest, ASavedPickLoadsBack) {
  ASSERT_GT(HomeVerseFile::save(pickEntry(), BUILD, nullptr), 0u);
  ASSERT_TRUE(storage_fake::fileBytes(PATH).has_value());

  home_verse::Entry restored;
  ASSERT_EQ(HomeVerseFile::load(BUILD, RUNGS, nullptr, restored), HomeVerseFile::Load::Restored);
  EXPECT_TRUE(restored.valid);
  EXPECT_TRUE(restored.hasPick);
  EXPECT_STREQ(restored.pick.reference, "John 3:16");
  EXPECT_STREQ(restored.pick.line(1), "so much that he gave");
}

TEST_F(HomeVerseFileTest, NoFileLoadsNothing) {
  home_verse::Entry out;
  out.valid = true;
  EXPECT_EQ(HomeVerseFile::load(BUILD, RUNGS, nullptr, out), HomeVerseFile::Load::None);
  EXPECT_FALSE(out.valid);
}

TEST_F(HomeVerseFileTest, AnInterruptedRenameIsAdopted) {
  ASSERT_GT(HomeVerseFile::save(pickEntry(), BUILD, nullptr), 0u);
  const std::string bytes = *storage_fake::fileBytes(PATH);
  storage_fake::reset();
  storage_fake::putFile(PATH + ".tmp", bytes);

  home_verse::Entry restored;
  EXPECT_EQ(HomeVerseFile::load(BUILD, RUNGS, nullptr, restored), HomeVerseFile::Load::Restored);
  EXPECT_TRUE(restored.valid);
  ASSERT_TRUE(storage_fake::fileBytes(PATH).has_value());
  EXPECT_EQ(*storage_fake::fileBytes(PATH), bytes);
  EXPECT_FALSE(storage_fake::fileBytes(PATH + ".tmp").has_value());
}

TEST_F(HomeVerseFileTest, AnotherBuildsFileIsNotRestored) {
  ASSERT_GT(HomeVerseFile::save(pickEntry(), BUILD, nullptr), 0u);
  home_verse::Entry out;
  EXPECT_EQ(HomeVerseFile::load("5e6f7a8b", RUNGS, nullptr, out), HomeVerseFile::Load::OtherBuild);
  EXPECT_FALSE(out.valid);
}

TEST_F(HomeVerseFileTest, AnUnusableBuildIdNeitherSavesNorLoads) {
  EXPECT_EQ(HomeVerseFile::save(pickEntry(), "00000000", nullptr), 0u);
  EXPECT_FALSE(storage_fake::fileBytes(PATH).has_value());

  ASSERT_GT(HomeVerseFile::save(pickEntry(), BUILD, nullptr), 0u);
  home_verse::Entry out;
  EXPECT_EQ(HomeVerseFile::load("", RUNGS, nullptr, out), HomeVerseFile::Load::NoBuildId);
  EXPECT_FALSE(out.valid);
}

TEST_F(HomeVerseFileTest, AnInvalidEntryIsNotWritten) {
  home_verse::Entry entry = pickEntry();
  entry.valid = false;
  EXPECT_EQ(HomeVerseFile::save(entry, BUILD, nullptr), 0u);
  EXPECT_FALSE(storage_fake::fileBytes(PATH).has_value());
}

TEST_F(HomeVerseFileTest, AnUnreadableOrFutureFileIsRefusedThenReplaced) {
  home_verse::Entry out;
  storage_fake::putFile(PATH, "not json");
  EXPECT_EQ(HomeVerseFile::load(BUILD, RUNGS, nullptr, out), HomeVerseFile::Load::Unreadable);
  storage_fake::putFile(PATH, R"({"v":2})");
  EXPECT_EQ(HomeVerseFile::load(BUILD, RUNGS, nullptr, out), HomeVerseFile::Load::UnknownFormat);
  storage_fake::putFile(PATH, R"({"v":1,"b":"1a2b3c4d","k":[],"e":0})");
  EXPECT_EQ(HomeVerseFile::load(BUILD, RUNGS, nullptr, out), HomeVerseFile::Load::Malformed);

  ASSERT_GT(HomeVerseFile::save(pickEntry(), BUILD, nullptr), 0u);
  EXPECT_EQ(HomeVerseFile::load(BUILD, RUNGS, nullptr, out), HomeVerseFile::Load::Restored);
}

TEST_F(HomeVerseFileTest, ASaveBuildsItsDocumentOnTheGivenAllocatorAndRefusesWhenItRunsOut) {
  CountdownJsonAllocator counting;
  ASSERT_GT(HomeVerseFile::save(pickEntry(), BUILD, &counting), 0u);
  EXPECT_TRUE(storage_fake::fileBytes(PATH).has_value());
  EXPECT_GT(counting.allocations, 0);

  storage_fake::reset();
  CountdownJsonAllocator exhausted;
  exhausted.remaining = 0;
  EXPECT_EQ(HomeVerseFile::save(pickEntry(), BUILD, &exhausted), 0u);
  EXPECT_FALSE(storage_fake::fileBytes(PATH).has_value());
  EXPECT_FALSE(storage_fake::fileBytes(PATH + ".tmp").has_value());
}

TEST_F(HomeVerseFileTest, ALoadThatRunsOutOfMemoryIsUnreadable) {
  ASSERT_GT(HomeVerseFile::save(pickEntry(), BUILD, nullptr), 0u);

  CountdownJsonAllocator exhausted;
  exhausted.remaining = 0;
  home_verse::Entry out;
  out.valid = true;
  EXPECT_EQ(HomeVerseFile::load(BUILD, RUNGS, &exhausted, out), HomeVerseFile::Load::Unreadable);
  EXPECT_FALSE(out.valid);
}

TEST_F(HomeVerseFileTest, EveryOutcomeHasALogWord) {
  EXPECT_STREQ(HomeVerseFile::describe(HomeVerseFile::Load::None), "none");
  EXPECT_STREQ(HomeVerseFile::describe(HomeVerseFile::Load::Restored), "restored");
  EXPECT_STREQ(HomeVerseFile::describe(HomeVerseFile::Load::Unreadable), "unreadable");
  EXPECT_STREQ(HomeVerseFile::describe(HomeVerseFile::Load::UnknownFormat), "unknown format");
  EXPECT_STREQ(HomeVerseFile::describe(HomeVerseFile::Load::OtherBuild), "other build");
  EXPECT_STREQ(HomeVerseFile::describe(HomeVerseFile::Load::Malformed), "malformed");
  EXPECT_STREQ(HomeVerseFile::describe(HomeVerseFile::Load::NoBuildId), "no build id");
}
