// The issue's version-refusal criterion end to end: a "v":2 card is never overwritten, and a
// refused store records nothing, not even in RAM.

#include <HalStorageFake.h>
#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "PlacesStore.h"

namespace {

const std::string PATH = sdpaths::PLACES_FILE;
const std::string NEWER = R"({"v":2,"places":[{"u":"v:1:1:1:0","r":"Genesis 1"}]})";

std::string bytesOn(const std::string& path) {
  const auto bytes = storage_fake::fileBytes(path);
  return bytes ? *bytes : std::string("<absent>");
}

Place makePlace(const uint8_t book, const uint16_t chapter, const uint16_t verse, const char* reference) {
  Place p;
  p.unit = study::Unit{study::UnitKind::Verse, book, chapter, verse, 0};
  p.reference = reference;
  return p;
}

// The store is a singleton, like on the device: every test starts from an empty list, loaded, on
// an empty card.
class PlacesStoreTest : public ::testing::Test {
 protected:
  void SetUp() override {
    storage_fake::reset();
    storage_fake::putFile(PATH, R"({"v":1,"places":[]})");
    ASSERT_TRUE(PLACES.load());
    storage_fake::reset();
  }
};

}  // namespace

TEST_F(PlacesStoreTest, RecordsReachTheCardAndReloadEqual) {
  PLACES.record(makePlace(1, 1, 1, "Genesis 1"));
  PLACES.record(makePlace(66, 21, 4, "Revelation 21:4"));
  EXPECT_NE(bytesOn(PATH).find(R"("v":1)"), std::string::npos);

  const std::vector<Place> saved = PLACES.getPlaces();
  ASSERT_EQ(saved.size(), 2u);
  EXPECT_EQ(saved[0].reference, "Revelation 21:4");
  ASSERT_TRUE(PLACES.load());
  EXPECT_EQ(PLACES.getPlaces(), saved);
}

TEST_F(PlacesStoreTest, AFutureVersionFileIsNeverOverwrittenAndRecordsNothing) {
  storage_fake::putFile(PATH, NEWER);
  EXPECT_FALSE(PLACES.load());
  PLACES.record(makePlace(66, 21, 4, "Revelation 21:4"));
  EXPECT_TRUE(PLACES.getPlaces().empty());
  EXPECT_EQ(bytesOn(PATH), NEWER);
}

TEST_F(PlacesStoreTest, AMissingFileLiftsTheRefusal) {
  storage_fake::putFile(PATH, NEWER);
  EXPECT_FALSE(PLACES.load());
  storage_fake::reset();
  EXPECT_FALSE(PLACES.load()) << "Missing: nothing to protect";
  PLACES.record(makePlace(66, 21, 4, "Revelation 21:4"));
  EXPECT_EQ(PLACES.getPlaces().size(), 1u);
  EXPECT_NE(bytesOn(PATH), std::string("<absent>"));
}

TEST_F(PlacesStoreTest, RecordingTheHeadAgainDoesNotWrite) {
  PLACES.record(makePlace(66, 21, 4, "Revelation 21:4"));
  // An attempted save writes the .tmp, removes PATH, then fails the rename (PersistableStore.cpp:109-116).
  storage_fake::failRenamesFrom(PATH + ".tmp");
  PLACES.record(makePlace(66, 21, 4, "Revelation 21:4"));
  EXPECT_NE(bytesOn(PATH), std::string("<absent>")) << "a save was attempted for an unchanged list";
}

TEST_F(PlacesStoreTest, AFailedSaveReportsFailure) {
  storage_fake::failWritesTo(PATH + ".tmp");
  EXPECT_FALSE(PLACES.record(makePlace(66, 21, 4, "Revelation 21:4")));
}

TEST_F(PlacesStoreTest, ARefusedFileOrAnUnchangedListIsNotAFailure) {
  EXPECT_TRUE(PLACES.record(makePlace(66, 21, 4, "Revelation 21:4")));
  EXPECT_TRUE(PLACES.record(makePlace(66, 21, 4, "Revelation 21:4")));
  storage_fake::putFile(PATH, NEWER);
  EXPECT_FALSE(PLACES.load());
  EXPECT_TRUE(PLACES.record(makePlace(1, 1, 1, "Genesis 1")));
}
