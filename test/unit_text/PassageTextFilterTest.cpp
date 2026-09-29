#include <gtest/gtest.h>

#include <cstring>
#include <string>

#include "StudyStore/UnitText.h"

namespace {

// A short excerpt in the Spanish NWT's shape (nwt_S.epub, Gálatas 5): the chapter
// number on verse 1, <sup> verse numbers each followed by U+202F, a footnote
// marker, an unanchored acrostic heading inside a verse, and the footnotes after
// the last verse. Not the publisher's text: the repo is public.
const char* kChapter =
    "<html><body>"
    "<p class=\"w_navigation\"><a href=\"nav.xhtml\">Libro</a> 5</p>\n"
    "<p data-pid=\"26\"><span id=\"chapter5\"></span><span id=\"chapter5_verse1\"></span>"
    "<span class=\"w_ch\"><strong>5</strong> </span>Uno dos tres.</p>\n"
    "<p data-pid=\"27\"><span id=\"chapter5_verse2\"></span><strong><sup>2</sup></strong>\xE2\x80\xAF"
    "Cuatro cinco.<span id=\"footnotesource1\"></span><a epub:type=\"noteref\" href=\"#footnote1\">*</a> "
    "<span id=\"chapter5_verse3\"></span><strong><sup>3</sup></strong>\xE2\x80\xAF"
    "Seis siete.</p>\n"
    "<p class=\"p9 ss\">\xD7\x91 <em>[bet]</em></p>\n"
    "<p data-pid=\"28\"><span id=\"chapter5_verse4\"></span><strong><sup>4</sup></strong>\xE2\x80\xAF"
    "Ocho nueve.</p>\n"
    "<div class=\"groupFootnote\"><aside epub:type=\"footnote\"><div epub:type=\"footnote\" id=\"footnote1\">"
    "<p data-pid=\"70\">O nota al pie.</p></div></aside>\n</div>"
    "</body></html>";

// Offsets, counted as VisibleOffsetCounter counts: "alpha"0-4 ' '5 "bravo"6-10 '*'11 ' '12 "charlie"13-19
// ' '20 "delta"21-25 '\n'26 "echo"27-30 ' '31 "foxtrot"32-38.
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

study::DocumentUnits chapterUnits() { return study::scanUnits(kChapter, strlen(kChapter)); }

std::string verseRange(const size_t firstAnchor, const uint32_t to) {
  const auto units = chapterUnits();
  return study::extractPassageText(kChapter, strlen(kChapter), units.anchors[firstAnchor].offset, to,
                                   study::CaptureFilter{true, false});
}

TEST(PassageTextFilter, TheFixtureHasFourVerses) { ASSERT_EQ(chapterUnits().anchors.size(), 4u); }

TEST(PassageTextFilter, DropsTheChapterNumberAndItsSpace) {
  const auto units = chapterUnits();
  EXPECT_EQ(squash(verseRange(0, units.anchors[1].offset)), "Uno dos tres.");
}

TEST(PassageTextFilter, DropsVerseNumbersFootnoteMarkersAndTheNarrowSpace) {
  const auto units = chapterUnits();
  const std::string text = verseRange(1, units.anchors[2].offset);
  EXPECT_EQ(squash(text), "Cuatro cinco.");
  EXPECT_EQ(text.find("\xE2\x80\xAF"), std::string::npos);
  EXPECT_EQ(text.find('*'), std::string::npos);
}

TEST(PassageTextFilter, DropsAnUnanchoredHeadingInsideAVerse) {
  const auto units = chapterUnits();
  EXPECT_EQ(squash(verseRange(2, units.anchors[3].offset)), "Seis siete.");
}

TEST(PassageTextFilter, TheLastVerseStopsBeforeTheFootnotes) {
  EXPECT_EQ(squash(verseRange(3, UINT32_MAX)), "Ocho nueve.");
}

TEST(PassageTextFilter, SeveralVersesReadAsOneText) {
  EXPECT_EQ(squash(verseRange(0, UINT32_MAX)), "Uno dos tres. Cuatro cinco. Seis siete. Ocho nueve.");
}

TEST(PassageTextFilter, OutsideAVerseDocumentNumbersStayAndMarkersGo) {
  const auto units = chapterUnits();
  const std::string text = study::extractPassageText(kChapter, strlen(kChapter), units.anchors[1].offset,
                                                     units.anchors[2].offset, study::CaptureFilter{false, false});
  EXPECT_NE(text.find('2'), std::string::npos);
  EXPECT_EQ(text.find('*'), std::string::npos);
}

TEST(PassageTextFilter, TheDefaultExtractionIsUnchanged) {
  const auto units = chapterUnits();
  const std::string text =
      study::extractRangeText(kChapter, strlen(kChapter), units.anchors[1].offset, units.anchors[2].offset);
  EXPECT_NE(text.find('*'), std::string::npos) << "fingerprints are taken from the unfiltered text";
  EXPECT_NE(text.find('2'), std::string::npos);
}

TEST(PassageTextFilter, ExtendsTheLastWordToItsEnd) {
  const std::string text =
      study::extractPassageText(kParagraphs, strlen(kParagraphs), 6, 14, study::CaptureFilter{false, true});
  EXPECT_EQ(squash(text), "bravo charlie") << "the range ends one codepoint into \"charlie\"";
}

TEST(PassageTextFilter, WithoutExtensionTheRangeIsExact) {
  const std::string text =
      study::extractPassageText(kParagraphs, strlen(kParagraphs), 6, 14, study::CaptureFilter{false, false});
  EXPECT_EQ(squash(text), "bravo c");
}

TEST(PassageTextFilter, ExtensionAcrossParagraphsKeepsWordsApart) {
  const std::string text =
      study::extractPassageText(kParagraphs, strlen(kParagraphs), 21, 28, study::CaptureFilter{false, true});
  EXPECT_EQ(squash(text), "delta echo");
}

TEST(PassageTextFilter, ExtensionStopsAtTheEndOfItsBlock) {
  const std::string text =
      study::extractPassageText(kParagraphs, strlen(kParagraphs), 32, 33, study::CaptureFilter{false, true});
  EXPECT_EQ(squash(text), "foxtrot");
}

TEST(PassageTextFilter, AScannerWithTheFilterMatchesTheWholeDocumentPass) {
  const auto units = chapterUnits();
  study::UnitTextScanner scanner;
  ASSERT_TRUE(scanner.valid());
  scanner.setRange(units.anchors[1].offset, units.anchors[3].offset);
  scanner.setFilter(study::CaptureFilter{true, false});
  const size_t length = strlen(kChapter);
  ASSERT_TRUE(scanner.feed(kChapter, length / 2, false));
  ASSERT_TRUE(scanner.feed(kChapter + length / 2, length - length / 2, true));
  EXPECT_EQ(squash(scanner.take()), "Cuatro cinco. Seis siete.");
}

}  // namespace
