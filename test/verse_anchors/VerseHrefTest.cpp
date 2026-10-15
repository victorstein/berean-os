#include <gtest/gtest.h>

#include <cstring>
#include <vector>

#include "activities/reader/VerseHref.h"

namespace {

void expectVerse(const char* href, const uint16_t chapter, const uint16_t verse) {
  const auto parsed = VerseHref::parse(href);
  ASSERT_TRUE(parsed.has_value()) << href;
  EXPECT_EQ(parsed->chapter, chapter) << href;
  EXPECT_EQ(parsed->verse, verse) << href;
}

}  // namespace

TEST(VerseHrefParse, ReadsTheChapterAndVerseOfAPreachingReference) {
  expectVerse("1001061123-split83.xhtml#chapter83_verse18", 83, 18);
}

TEST(VerseHrefParse, ReadsTheLongestReferenceInTheSection) {
  expectVerse("1001061123-split119.xhtml#chapter119_verse105", 119, 105);
}

TEST(VerseHrefParse, ReadsASameFileFragment) { expectVerse("#chapter3_verse16", 3, 16); }

TEST(VerseHrefParse, AcceptsTheLargestUint16Numbers) {
  expectVerse("x.xhtml#chapter65535_verse65535", 65535, 65535);
}

TEST(VerseHrefParse, RefusesAnHrefWithNoVerse) {
  EXPECT_FALSE(VerseHref::parse("1001061123-split83.xhtml").has_value());
  EXPECT_FALSE(VerseHref::parse("1001061123-split83.xhtml#").has_value());
  EXPECT_FALSE(VerseHref::parse("1001061123-split83.xhtml#p12").has_value());
  EXPECT_FALSE(VerseHref::parse("").has_value());
}

TEST(VerseHrefParse, RefusesTrailingJunk) { EXPECT_FALSE(VerseHref::parse("x.xhtml#chapter1_verse2x").has_value()); }

// 65536, not a larger number: %u past UINT32_MAX is undefined and wraps on the host.
TEST(VerseHrefParse, RefusesANumberPastUint16) {
  EXPECT_FALSE(VerseHref::parse("x.xhtml#chapter65536_verse1").has_value());
  EXPECT_FALSE(VerseHref::parse("x.xhtml#chapter1_verse65536").has_value());
}

TEST(VerseHrefOffsetOf, FindsTheMatchingAnchor) {
  const std::vector<VerseAnchors::VerseAnchor> anchors = {{0, 83, 17}, {40, 83, 18}, {90, 83, 19}};
  EXPECT_EQ(VerseHref::offsetOf(anchors, {83, 18}), std::optional<uint32_t>(40));
}

TEST(VerseHrefOffsetOf, MissesAVerseNotInTheScan) {
  const std::vector<VerseAnchors::VerseAnchor> anchors = {{0, 83, 17}, {40, 83, 18}};
  EXPECT_FALSE(VerseHref::offsetOf(anchors, {83, 20}).has_value());
  EXPECT_FALSE(VerseHref::offsetOf({}, {83, 18}).has_value());
}

TEST(VerseHrefOffsetOf, MatchesTheChapterAsWellAsTheVerse) {
  const std::vector<VerseAnchors::VerseAnchor> anchors = {{10, 1, 5}, {20, 2, 5}};
  EXPECT_EQ(VerseHref::offsetOf(anchors, {2, 5}), std::optional<uint32_t>(20));
}

// The href and the scan read the same id grammar, so the offset an href resolves to is the one the
// scan recorded for that marker.
TEST(VerseHrefAgreement, AnHrefResolvesToTheMarkerItsFragmentNames) {
  const char* doc =
      "<html><body><p>"
      "<span id=\"chapter83_verse17\"></span><sup>17</sup> abcde "
      "<span id=\"chapter83_verse18\"></span><sup>18</sup> fghij"
      "</p></body></html>";
  const auto anchors = VerseAnchors::scan(doc, std::strlen(doc));
  ASSERT_EQ(anchors.size(), 2u);
  ASSERT_GT(anchors[1].offset, 0u);

  const auto verse = VerseHref::parse("1001061123-split83.xhtml#chapter83_verse18");
  ASSERT_TRUE(verse.has_value());
  EXPECT_EQ(VerseHref::offsetOf(anchors, *verse), std::optional<uint32_t>(anchors[1].offset));
}
