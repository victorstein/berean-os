#include <gtest/gtest.h>

#include <string>
#include <type_traits>

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

static_assert(std::is_trivially_copyable_v<ReaderEntryIntent>, "the reader copies the intent by value");

TEST(ReaderEntryIntent, OpenHrefIsBibleOnly) {
  EXPECT_EQ(ReaderEntryIntent::route(Kind::OpenHref, true), Route::Href);
  EXPECT_EQ(ReaderEntryIntent::route(Kind::OpenHref, false), Route::None);
}

TEST(ReaderEntryIntent, OpenHrefCarriesTheLinkAndTheOrigin) {
  const std::string link(ReaderEntryIntent::HREF_BYTES - 1, 'x');
  const PreachingOrigin origin = PreachingOrigin::ofQuestion(8, 3);
  ReaderEntryIntent intent;
  ASSERT_TRUE(ReaderEntryIntent::openHref(link, origin, intent));
  EXPECT_EQ(intent.kind, Kind::OpenHref);
  EXPECT_EQ(std::string(intent.href), link);
  EXPECT_EQ(intent.origin.kind, PreachingOrigin::Kind::Question);
  EXPECT_EQ(intent.origin.question, 8);
  EXPECT_EQ(intent.origin.verse, 3);
}

TEST(ReaderEntryIntent, OpenHrefRefusesAnOverLongOrEmptyLink) {
  ReaderEntryIntent intent = ReaderEntryIntent::of(Kind::GoTo);
  EXPECT_FALSE(ReaderEntryIntent::openHref(std::string(ReaderEntryIntent::HREF_BYTES, 'x'), {}, intent));
  EXPECT_FALSE(ReaderEntryIntent::openHref("", {}, intent));
  EXPECT_EQ(intent.kind, Kind::GoTo);
  EXPECT_EQ(intent.href[0], '\0');
}

TEST(ReaderEntryIntent, AnIntentHasNoOriginUntilGivenOne) {
  EXPECT_FALSE(ReaderEntryIntent{}.origin.isSet());
  EXPECT_FALSE(ReaderEntryIntent::of(Kind::OpenAt).origin.isSet());
}

TEST(ReaderEntryIntent, OriginsKeepTheirTopicAndVerse) {
  const PreachingOrigin tag = PreachingOrigin::ofTag(31, 2);
  EXPECT_EQ(tag.kind, PreachingOrigin::Kind::Tag);
  EXPECT_EQ(tag.tag, 31);
  EXPECT_EQ(tag.verse, 2);
  EXPECT_EQ(PreachingOrigin::ofQuestion(5, -1).verse, 0);
}
