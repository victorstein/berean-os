#include <gtest/gtest.h>

#include <cstring>
#include <string>

#include "StudyStore/PassageSpan.h"
#include "StudyStore/UnitText.h"

namespace {

const char* kChapter =
    "<html><body>"
    "<p class=\"w_navigation\"><a href=\"nav.xhtml\">Libro</a> 5</p>\n"
    "<p data-pid=\"26\"><span id=\"chapter5_verse1\"></span>"
    "<span class=\"w_ch\"><strong>5</strong> </span>Uno dos tres.</p>\n"
    "<p data-pid=\"27\"><span id=\"chapter5_verse2\"></span><strong><sup>2</sup></strong>\xE2\x80\xAF"
    "Cuatro cinco. <span id=\"chapter5_verse3\"></span><strong><sup>3</sup></strong>\xE2\x80\xAF"
    "Seis siete.</p>\n"
    "<div class=\"groupFootnote\"><aside epub:type=\"footnote\"><p data-pid=\"70\">Nota.</p></aside></div>"
    "</body></html>";

const char* kParagraphs =
    "<html><body><p data-pid=\"1\">alpha bravo<a epub:type=\"noteref\" href=\"#n\">*</a> charlie delta</p>\n"
    "<p data-pid=\"2\">echo foxtrot</p></body></html>";

std::string squash(const std::string& text) {
  std::string out;
  bool space = false;
  for (const char c : text) {
    if (c == ' ' || c == '\n' || c == '\t' || c == '\r') {
      space = !out.empty();
      continue;
    }
    if (space) out.push_back(' ');
    space = false;
    out.push_back(c);
  }
  return out;
}

struct Chapter {
  study::DocumentUnits units = study::scanUnits(kChapter, strlen(kChapter));
  uint32_t anchor(const size_t i) const { return units.anchors[i].offset; }
  study::Unit at(const uint32_t offset) const { return study::resolve(units, offset); }
  std::string text(const study::PassageSpan& span) const {
    return squash(study::extractPassageText(kChapter, strlen(kChapter), span.from, span.to,
                                            study::CaptureFilter{span.verseDocument, span.extendToWordEnd}));
  }
};

TEST(PassageSpan, AMidVerseSelectionStoresItsWholeVerses) {
  const Chapter c;
  ASSERT_EQ(c.units.anchors.size(), 3u);
  // Starts inside "cinco." in verse 2 and ends inside "Seis" in verse 3.
  const auto span = study::snapSpan(c.units, c.at(c.anchor(1) + 10), c.at(c.anchor(2) + 5));
  ASSERT_TRUE(span.has_value());
  EXPECT_EQ(span->from, c.anchor(1));
  EXPECT_EQ(span->to, UINT32_MAX) << "verse 3 is the chapter's last";
  EXPECT_TRUE(span->verseDocument);
  EXPECT_FALSE(span->extendToWordEnd);
  EXPECT_EQ(c.text(*span), "Cuatro cinco. Seis siete.") << "whole verses, no footnote";
}

TEST(PassageSpan, ASelectionInsideOneVerseStopsAtTheNextAnchor) {
  const Chapter c;
  const auto span = study::snapSpan(c.units, c.at(c.anchor(1) + 3), c.at(c.anchor(1) + 8));
  ASSERT_TRUE(span.has_value());
  EXPECT_EQ(span->from, c.anchor(1));
  EXPECT_EQ(span->to, c.anchor(2));
  EXPECT_EQ(c.text(*span), "Cuatro cinco.");
}

TEST(PassageSpan, AnEndOnTheNextAnchorBelongsToThePreviousVerse) {
  const Chapter c;
  const auto span = study::snapSpan(c.units, c.at(c.anchor(1) + 3), c.at(c.anchor(2)));
  ASSERT_TRUE(span.has_value());
  EXPECT_EQ(span->to, c.anchor(2)) << "the last selected codepoint is end - 1, still in verse 2";
}

TEST(PassageSpan, AnEndEqualToTheStartCoversTheStartsVerse) {
  const Chapter c;
  const study::Unit start = c.at(c.anchor(2));
  const auto span = study::snapSpan(c.units, start, start);
  ASSERT_TRUE(span.has_value());
  EXPECT_EQ(span->from, c.anchor(2));
  EXPECT_EQ(span->to, UINT32_MAX) << "never the previous verse";
}

TEST(PassageSpan, AStartBeforeTheFirstAnchorStartsWhereItStarts) {
  const Chapter c;
  const study::Unit start = c.at(2);
  ASSERT_EQ(start.kind, study::UnitKind::DocumentOffset);
  const auto span = study::snapSpan(c.units, start, c.at(c.anchor(0) + 4));
  ASSERT_TRUE(span.has_value());
  EXPECT_EQ(span->from, 2u);
  EXPECT_EQ(span->to, c.anchor(1));
  EXPECT_TRUE(span->verseDocument);
}

TEST(PassageSpan, AMigratedDocumentOffsetRowStillSnapsToWholeVerses) {
  const Chapter c;
  const study::Unit start{study::UnitKind::DocumentOffset, 0, 0, 0, c.anchor(1) + 4};
  const study::Unit end{study::UnitKind::DocumentOffset, 0, 0, 0, c.anchor(1) + 6};
  const auto span = study::snapSpan(c.units, start, end);
  ASSERT_TRUE(span.has_value());
  EXPECT_EQ(span->from, c.anchor(1));
  EXPECT_EQ(span->to, c.anchor(2));
}

TEST(PassageSpan, AnEndNotInThisDocumentIsUnresolved) {
  const Chapter c;
  const study::Unit elsewhere{study::UnitKind::Verse, 0, 9, 9, 0};
  EXPECT_FALSE(study::snapSpan(c.units, c.at(c.anchor(1)), elsewhere).has_value());
}

TEST(PassageSpan, AParagraphDocumentKeepsTheSelectionAndExtendsTheLastWord) {
  const auto units = study::scanUnits(kParagraphs, strlen(kParagraphs));
  ASSERT_EQ(units.kind, study::UnitKind::Paragraph);
  const auto span = study::snapSpan(units, study::resolve(units, 6), study::resolve(units, 14));
  ASSERT_TRUE(span.has_value());
  EXPECT_EQ(span->from, 6u);
  EXPECT_EQ(span->to, 14u);
  EXPECT_FALSE(span->verseDocument);
  EXPECT_TRUE(span->extendToWordEnd);
  EXPECT_EQ(squash(study::extractPassageText(kParagraphs, strlen(kParagraphs), span->from, span->to,
                                             study::CaptureFilter{false, true})),
            "bravo charlie");
}

}  // namespace
