// A build whose main.cpp never calls PLACES.load() must still respect a file it cannot read.

#include <HalStorageFake.h>
#include <gtest/gtest.h>

#include <string>

#include "PlacesStore.h"

TEST(PlacesStoreLazyLoad, ARecordBeforeAnyLoadStillRespectsARefusedFile) {
  storage_fake::reset();
  const std::string newer = R"({"v":2,"places":[]})";
  storage_fake::putFile(sdpaths::PLACES_FILE, newer);

  Place place;
  place.unit = study::Unit{study::UnitKind::Verse, 66, 21, 4, 0};
  place.reference = "Revelation 21:4";
  PLACES.record(place);

  EXPECT_TRUE(PLACES.getPlaces().empty());
  const auto bytes = storage_fake::fileBytes(sdpaths::PLACES_FILE);
  ASSERT_TRUE(bytes.has_value());
  EXPECT_EQ(*bytes, newer);
}
