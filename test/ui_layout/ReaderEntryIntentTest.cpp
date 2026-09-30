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

TEST(ReaderEntryIntent, GoToFallsBackToTheTocOutsideTheBible) {
  EXPECT_EQ(ReaderEntryIntent::route(Kind::GoTo, true), Route::ChapterGrid);
  EXPECT_EQ(ReaderEntryIntent::route(Kind::GoTo, false), Route::TocList);
}

TEST(ReaderEntryIntent, SearchIsBibleOnly) {
  EXPECT_EQ(ReaderEntryIntent::route(Kind::Search, true), Route::Search);
  EXPECT_EQ(ReaderEntryIntent::route(Kind::Search, false), Route::None);
}

// A Tags intent into a book that is not the Bible would list that book's own
// passages; like OpenAt and Search it opens the book normally instead.
TEST(ReaderEntryIntent, TagsAreBibleOnly) {
  EXPECT_EQ(ReaderEntryIntent::route(Kind::Tags, true), Route::Highlights);
  EXPECT_EQ(ReaderEntryIntent::route(Kind::Tags, false), Route::None);
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
