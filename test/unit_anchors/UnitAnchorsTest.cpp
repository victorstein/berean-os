#include <gtest/gtest.h>

#include <cstring>

#include "StudyStore/UnitAnchors.h"

namespace {

// Every verse-marked document in the NWT also carries data-pid. This is that
// shape, and Verse must win.
const char* kBibleDoc =
    "<html><body><p id=\"p3\" data-pid=\"3\">"
    "<span id=\"chapter1_verse7\"></span><strong><sup>7</sup></strong> alpha bravo"
    "<span id=\"chapter1_verse8\"></span><strong><sup>8</sup></strong> charlie delta"
    "</p></body></html>";

const char* kArticleDoc =
    "<html><body><p id=\"p6\" data-pid=\"6\">alpha</p>"
    "<p id=\"p7\" data-pid=\"7\">bravo</p></body></html>";

const char* kPlainDoc = "<html><body><p>alpha bravo charlie</p></body></html>";

TEST(UnitAnchorsKind, VerseWinsWhereBothMarkersExist) {
  const auto a = study::scanUnits(kBibleDoc, strlen(kBibleDoc));
  EXPECT_EQ(a.kind, study::UnitKind::Verse);
  ASSERT_EQ(a.anchors.size(), 2u);
  EXPECT_EQ(a.anchors[0].major, 1);
  EXPECT_EQ(a.anchors[0].minor, 7);
  EXPECT_EQ(a.anchors[1].minor, 8);
}

TEST(UnitAnchorsKind, ParagraphWhereOnlyPidExists) {
  const auto a = study::scanUnits(kArticleDoc, strlen(kArticleDoc));
  EXPECT_EQ(a.kind, study::UnitKind::Paragraph);
  ASSERT_EQ(a.anchors.size(), 2u);
  EXPECT_EQ(a.anchors[0].major, 0);
  EXPECT_EQ(a.anchors[0].minor, 6);
}

TEST(UnitAnchorsResolve, DoesNotStampABookOntoAParagraphUnit) {
  auto a = study::scanUnits(kArticleDoc, strlen(kArticleDoc));
  a.book = 19;  // a Bible document that has pids but no verse markers
  const study::Unit u = study::resolve(a, a.anchors[0].offset + 1);
  EXPECT_EQ(u.kind, study::UnitKind::Paragraph);
  EXPECT_EQ(u.book, 0) << "Unit.h promises book is 0 for anything but a Verse";
}

TEST(UnitAnchorsKind, DocumentOffsetWhereNeitherExists) {
  const auto a = study::scanUnits(kPlainDoc, strlen(kPlainDoc));
  EXPECT_EQ(a.kind, study::UnitKind::DocumentOffset);
  EXPECT_TRUE(a.anchors.empty());
}

TEST(UnitAnchorsResolve, MapsAnOffsetToAUnitWithAnOffsetInsideIt) {
  auto a = study::scanUnits(kBibleDoc, strlen(kBibleDoc));
  a.book = 40;  // Matthew, as the index builder would stamp it
  ASSERT_EQ(a.anchors.size(), 2u);
  const study::Unit u = study::resolve(a, a.anchors[1].offset + 4);
  EXPECT_EQ(u.kind, study::UnitKind::Verse);
  EXPECT_EQ(u.book, 40) << "the book must reach the address, or two languages cannot agree";
  EXPECT_EQ(u.major, 1);
  EXPECT_EQ(u.minor, 8);
  EXPECT_EQ(u.offset, 4u) << "offset is relative to the unit, not the document";
}

TEST(UnitAnchorsResolve, FallsBackToDocumentOffsetBeforeTheFirstAnchor) {
  const char* doc = "<html><body>lead text<p data-pid=\"6\">alpha</p></body></html>";
  const auto b = study::scanUnits(doc, strlen(doc));
  ASSERT_FALSE(b.anchors.empty());
  ASSERT_GT(b.anchors[0].offset, 0u);
  const study::Unit u = study::resolve(b, 0);
  EXPECT_EQ(u.kind, study::UnitKind::DocumentOffset);
  EXPECT_EQ(u.offset, 0u);
}

TEST(UnitAnchorsResolve, WillNotMapAVerseBackIntoADifferentBook) {
  auto matthew = study::scanUnits(kBibleDoc, strlen(kBibleDoc));
  matthew.book = 40;
  const study::Unit inMark{study::UnitKind::Verse, 41, 1, 8, 0};
  EXPECT_FALSE(study::documentOffsetOf(matthew, inMark).has_value());
}

TEST(UnitAnchorsResolve, DocumentOffsetDocumentsKeepTheRawOffset) {
  const auto a = study::scanUnits(kPlainDoc, strlen(kPlainDoc));
  const study::Unit u = study::resolve(a, 1255);
  EXPECT_EQ(u.kind, study::UnitKind::DocumentOffset);
  EXPECT_EQ(u.offset, 1255u);
}

TEST(UnitAnchorsResolve, RoundTripsAVerseBackToItsDocumentOffset) {
  auto a = study::scanUnits(kBibleDoc, strlen(kBibleDoc));
  a.book = 40;
  const uint32_t original = a.anchors[1].offset + 4;
  const study::Unit u = study::resolve(a, original);
  const auto back = study::documentOffsetOf(a, u);
  ASSERT_TRUE(back.has_value());
  EXPECT_EQ(*back, original);
}

}  // namespace

namespace {

TEST(UnitScanner, ChunkFedAgreesWithTheWholeBufferScan) {
  const auto whole = study::scanUnits(kBibleDoc, strlen(kBibleDoc));

  study::UnitScanner scanner;
  ASSERT_TRUE(scanner.valid());
  const size_t length = strlen(kBibleDoc);
  const size_t half = length / 2;
  ASSERT_TRUE(scanner.feed(kBibleDoc, half, false));
  ASSERT_TRUE(scanner.feed(kBibleDoc + half, length - half, true));
  const auto streamed = scanner.take();

  EXPECT_EQ(streamed.kind, whole.kind);
  ASSERT_EQ(streamed.anchors.size(), whole.anchors.size());
  for (size_t i = 0; i < whole.anchors.size(); ++i) {
    EXPECT_EQ(streamed.anchors[i].offset, whole.anchors[i].offset) << "at " << i;
    EXPECT_EQ(streamed.anchors[i].minor, whole.anchors[i].minor) << "at " << i;
  }
}

TEST(UnitScanner, AppliesPrecedenceInOneStreamedPass) {
  study::UnitScanner scanner;
  ASSERT_TRUE(scanner.valid());
  ASSERT_TRUE(scanner.feed(kBibleDoc, strlen(kBibleDoc), true));
  const auto units = scanner.take();
  EXPECT_EQ(units.kind, study::UnitKind::Verse) << "one pass feeds both scanners; verse still wins";
}

TEST(UnitScanner, FallsBackToParagraphInOnePass) {
  study::UnitScanner scanner;
  ASSERT_TRUE(scanner.valid());
  ASSERT_TRUE(scanner.feed(kArticleDoc, strlen(kArticleDoc), true));
  EXPECT_EQ(scanner.take().kind, study::UnitKind::Paragraph);
}

}  // namespace
