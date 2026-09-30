#include <gtest/gtest.h>

#include "activities/reader/ReaderEntryIntent.h"

using Kind = ReaderEntryIntent::Kind;
using Route = ReaderEntryIntent::Route;

TEST(ReaderEntryIntent, NoIntentRoutesNowhere) {
  EXPECT_EQ(ReaderEntryIntent::route(Kind::None, true), Route::None);
  EXPECT_EQ(ReaderEntryIntent::route(Kind::None, false), Route::None);
}

TEST(ReaderEntryIntent, OpenAtIsBibleOnly) {
  EXPECT_EQ(ReaderEntryIntent::route(Kind::OpenAt, true), Route::Locate);
  EXPECT_EQ(ReaderEntryIntent::route(Kind::OpenAt, false), Route::None);
}

TEST(ReaderEntryIntent, BookGridFallsBackToTheTocOutsideTheBible) {
  EXPECT_EQ(ReaderEntryIntent::route(Kind::BookGrid, true), Route::ChapterGrid);
  EXPECT_EQ(ReaderEntryIntent::route(Kind::BookGrid, false), Route::TocList);
}

TEST(ReaderEntryIntent, SearchIsBibleOnly) {
  EXPECT_EQ(ReaderEntryIntent::route(Kind::Search, true), Route::Search);
  EXPECT_EQ(ReaderEntryIntent::route(Kind::Search, false), Route::None);
}

TEST(ReaderEntryIntent, TagsOpenTheHighlightsEverywhere) {
  EXPECT_EQ(ReaderEntryIntent::route(Kind::Tags, true), Route::Highlights);
  EXPECT_EQ(ReaderEntryIntent::route(Kind::Tags, false), Route::Highlights);
}

TEST(ReaderEntryIntent, OpenAtCarriesThePlacesUnitAndHint) {
  Place place;
  place.unit = study::Unit{study::UnitKind::Verse, 66, 21, 4, 0};
  place.spineIndex = 1201;
  const auto intent = ReaderEntryIntent::openAt(place);
  EXPECT_EQ(intent.kind, Kind::OpenAt);
  EXPECT_EQ(intent.unit, place.unit);
  EXPECT_EQ(intent.spineHint, 1201);
}
