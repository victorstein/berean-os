#include <gtest/gtest.h>

#include <cstring>

#include "StudyStore/UnitText.h"

namespace {

// The Spanish NWT's real shape: a marker span, the verse number in sup, then a
// NARROW NO-BREAK SPACE (U+202F) before the text.
const char* kDoc =
    "<html><head><title>skipme</title></head><body>"
    "<p id=\"p3\" data-pid=\"3\">"
    "<span id=\"chapter1_verse7\"></span><strong><sup>7</sup></strong> alpha bravo "
    "<span id=\"chapter1_verse8\"></span><strong><sup>8</sup></strong> charlie delta"
    "</p></body></html>";

TEST(UnitText, ExtractsExactlyTheTextOfOneVerse) {
  const auto units = study::scanUnits(kDoc, strlen(kDoc));
  ASSERT_EQ(units.anchors.size(), 2u);
  const std::string text = study::extractUnitText(kDoc, strlen(kDoc), units, units.anchors[0]);
  EXPECT_NE(text.find("alpha bravo"), std::string::npos);
  EXPECT_EQ(text.find("charlie"), std::string::npos) << "a unit stops where the next one starts";
}

TEST(UnitText, ExtractsTheLastUnitToTheEndOfTheDocument) {
  const auto units = study::scanUnits(kDoc, strlen(kDoc));
  ASSERT_EQ(units.anchors.size(), 2u);
  const std::string text = study::extractUnitText(kDoc, strlen(kDoc), units, units.anchors[1]);
  EXPECT_NE(text.find("charlie delta"), std::string::npos);
}

// The load-bearing one. The count must agree with VisibleOffsetCounter, which
// is what produced the anchor offsets in the first place.
TEST(UnitText, CodepointCountAgreesWithTheOffsetsThatDelimitIt) {
  const auto units = study::scanUnits(kDoc, strlen(kDoc));
  ASSERT_EQ(units.anchors.size(), 2u);
  const uint32_t span = units.anchors[1].offset - units.anchors[0].offset;
  const std::string text = study::extractUnitText(kDoc, strlen(kDoc), units, units.anchors[0]);

  uint32_t codepoints = 0;
  for (const unsigned char c : text) {
    if ((c & 0xC0u) != 0x80u) ++codepoints;
  }
  EXPECT_EQ(codepoints, span) << "if these disagree, every stored offset is wrong by the difference";
}

TEST(UnitText, PreservesANarrowNoBreakSpaceRatherThanNormalisingIt) {
  const auto units = study::scanUnits(kDoc, strlen(kDoc));
  const std::string text = study::extractUnitText(kDoc, strlen(kDoc), units, units.anchors[0]);
  EXPECT_NE(text.find(" "), std::string::npos)
      << "normalising here but not in the offset counter shifts every later offset";
}

TEST(UnitText, ExpandsAKnownEntityToOneCodepoint) {
  const char* doc =
      "<!DOCTYPE html SYSTEM \"about:legacy-compat\">"
      "<html><body><p data-pid=\"1\">alpha&nbsp;bravo</p></body></html>";
  const auto units = study::scanUnits(doc, strlen(doc));
  ASSERT_EQ(units.anchors.size(), 1u);
  const std::string text = study::extractUnitText(doc, strlen(doc), units, units.anchors[0]);
  EXPECT_EQ(text.find("&nbsp;"), std::string::npos) << "the entity must be expanded, not carried literally";
  EXPECT_NE(text.find("alpha"), std::string::npos);
}

TEST(UnitText, ReturnsEmptyForADocumentWithNoUnits) {
  const char* doc = "<html><body><p>alpha</p></body></html>";
  const auto units = study::scanUnits(doc, strlen(doc));
  EXPECT_TRUE(study::extractUnitText(doc, strlen(doc), units, study::UnitAnchor{0, 0, 0}).empty());
}

TEST(UnitText, DoesNotCaptureTextOutsideBody) {
  const auto units = study::scanUnits(kDoc, strlen(kDoc));
  const std::string text = study::extractUnitText(kDoc, strlen(kDoc), units, units.anchors[0]);
  EXPECT_EQ(text.find("skipme"), std::string::npos);
}

TEST(UnitText, RangeExtractionSlicesOnCodepointBoundaries) {
  const char* doc = "<html><body><p>áéíóú</p></body></html>";
  const std::string text = study::extractRangeText(doc, strlen(doc), 1, 3);
  EXPECT_EQ(text, "éí") << "a byte slice here would split a two-byte sequence";
}

TEST(UnitText, DocumentCrcIsStableAndChangesWithAWord) {
  const char* other = "<html><body><p data-pid=\"1\">alpha bravo charlie</p></body></html>";
  const char* changed = "<html><body><p data-pid=\"1\">alpha bravo charlee</p></body></html>";
  EXPECT_EQ(study::documentVisibleCrc(other, strlen(other)), study::documentVisibleCrc(other, strlen(other)));
  EXPECT_NE(study::documentVisibleCrc(other, strlen(other)), study::documentVisibleCrc(changed, strlen(changed)));
}

TEST(UnitText, DocumentCrcIgnoresMarkupThatChangesNoVisibleText) {
  const char* plain = "<html><body><p>alpha bravo</p></body></html>";
  const char* wrapped = "<html><body><p class=\"x\" data-pid=\"9\"><span>alpha</span> bravo</p></body></html>";
  EXPECT_EQ(study::documentVisibleCrc(plain, strlen(plain)), study::documentVisibleCrc(wrapped, strlen(wrapped)))
      << "a markup-only reissue must not orphan every mark in the document";
}

}  // namespace

namespace {

TEST(UnitTextScanner, ChunkFedAgreesWithTheWholeBufferExtraction) {
  const auto units = study::scanUnits(kDoc, strlen(kDoc));
  ASSERT_EQ(units.anchors.size(), 2u);
  const std::string whole = study::extractUnitText(kDoc, strlen(kDoc), units, units.anchors[0]);

  study::UnitTextScanner scanner;
  ASSERT_TRUE(scanner.valid());
  scanner.setRange(units.anchors[0].offset, units.anchors[1].offset);
  const size_t length = strlen(kDoc);
  const size_t third = length / 3;
  ASSERT_TRUE(scanner.feed(kDoc, third, false));
  ASSERT_TRUE(scanner.feed(kDoc + third, third, false));
  ASSERT_TRUE(scanner.feed(kDoc + 2 * third, length - 2 * third, true));

  EXPECT_EQ(scanner.take(), whole) << "the device streams; the host suite does not. They must agree.";
}

}  // namespace
