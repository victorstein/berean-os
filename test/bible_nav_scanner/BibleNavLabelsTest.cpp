#include <gtest/gtest.h>

#include <string>

#include "BibleNavScanner.h"

namespace {

// Verbatim structure of nwt_S.epub's OEBPS/biblebooknav.xhtml, trimmed to two
// rows per section. The spacer row carries a U+00A0 outside any <a> or
// <strong>, and most <a>s are preceded by a literal space inside the <td>.
const std::string kSpanishBookNav =
    "<?xml version=\"1.0\" encoding=\"utf-8\" ?>\n"
    "<html dir=\"ltr\" class=\"dir-ltr\" xmlns=\"http://www.w3.org/1999/xhtml\" xml:lang=\"es\">\n"
    "<head>\n<title>Navegaci\xC3\xB3n por la Biblia</title>\n<meta charset=\"utf-8\" />\n"
    "<link rel=\"stylesheet\" href=\"css/epubs.css\" type=\"text/css\" />\n</head>\n"
    "<body dir=\"ltr\" xml:lang=\"es\" class=\"jwac dir-ltr ml-S ms-ROMAN pub-nwt\">"
    "<table class=\"w_navigation w_bibleBook\"><tr><td colspan=\"5\"><strong>Escrituras Hebreas</strong></td></tr>\n"
    "<tr><td class=\"w_navigation w_bibleBook\"><a href=\"biblechapternav1.xhtml\">G\xC3\xA9n.</a></td>"
    "<td class=\"w_navigation w_bibleBook\"> <a href=\"biblechapternav2.xhtml\">\xC3\x89x.</a></td>"
    "<td class=\"w_navigation w_bibleBook\"> <a href=\"biblechapternav13.xhtml\">1 Cr\xC3\xB3n.</a></td></tr>\n"
    "<tr><td colspan=\"5\">\xC2\xA0 </td></tr>\n"
    "<tr><td colspan=\"5\"><strong>Escrituras Griegas</strong></td></tr>\n"
    "<tr><td class=\"w_navigation w_bibleBook\"> <a href=\"biblechapternav40.xhtml\">Mat.</a></td>"
    "<td class=\"w_navigation w_bibleBook\"> <a href=\"1001061169.xhtml\">Jud.</a></td>"
    "<td class=\"w_navigation w_bibleBook\"> <a href=\"biblechapternav66.xhtml\">Apoc.</a></td></tr></table>\n"
    "</body>\n</html>\n";

BibleNav::BookNavPage scanBookNav(const std::string& xhtml) {
  BibleNav::Scanner scanner;
  EXPECT_TRUE(scanner.valid());
  EXPECT_TRUE(scanner.feed(xhtml.data(), xhtml.size(), /*isFinal=*/true));
  return scanner.takeBookNav();
}

}  // namespace

TEST(BibleNavLabels, LinkTextIsTheAbbreviationInDocumentOrder) {
  const auto page = scanBookNav(kSpanishBookNav);

  ASSERT_EQ(page.labels.size(), 6u);
  EXPECT_EQ(page.labels[0], "G\xC3\xA9n.");
  EXPECT_EQ(page.labels[1], "\xC3\x89x.");
  EXPECT_EQ(page.labels[2], "1 Cr\xC3\xB3n.");
  EXPECT_EQ(page.labels[5], "Apoc.");
}

TEST(BibleNavLabels, TargetsStayParallelToLabels) {
  const auto page = scanBookNav(kSpanishBookNav);

  ASSERT_EQ(page.targets.size(), page.labels.size());
  EXPECT_EQ(page.targets[0], "biblechapternav1.xhtml");
  EXPECT_EQ(page.targets[4], "1001061169.xhtml");
}

TEST(BibleNavLabels, HeadingsRecordTheFirstBookAfterThem) {
  const auto page = scanBookNav(kSpanishBookNav);

  ASSERT_EQ(page.sections.size(), 2u);
  EXPECT_EQ(page.sections[0].title, "Escrituras Hebreas");
  EXPECT_EQ(page.sections[0].firstLink, 0);
  EXPECT_EQ(page.sections[1].title, "Escrituras Griegas");
  EXPECT_EQ(page.sections[1].firstLink, 3);
}

TEST(BibleNavLabels, WhitespaceIsTrimmedAndCollapsed) {
  const std::string xhtml =
      "<html><body><a href=\"a.xhtml\">\n  1   Sam.\t</a><strong>  Two\n Words </strong></body></html>";
  const auto page = scanBookNav(xhtml);

  ASSERT_EQ(page.labels.size(), 1u);
  EXPECT_EQ(page.labels[0], "1 Sam.");
  ASSERT_EQ(page.sections.size(), 1u);
  EXPECT_EQ(page.sections[0].title, "Two Words");
  EXPECT_EQ(page.sections[0].firstLink, 1);
}

TEST(BibleNavLabels, TextInsideNestedElementsIsKept) {
  const auto page = scanBookNav("<html><body><a href=\"a.xhtml\"><span>Gen</span>.</a></body></html>");

  ASSERT_EQ(page.labels.size(), 1u);
  EXPECT_EQ(page.labels[0], "Gen.");
}

TEST(BibleNavLabels, AnEmptyLinkYieldsAnEmptyLabel) {
  const auto page = scanBookNav("<html><body><a href=\"a.xhtml\"></a><a href=\"b.xhtml\">B</a></body></html>");

  ASSERT_EQ(page.labels.size(), 2u);
  EXPECT_EQ(page.labels[0], "");
  EXPECT_EQ(page.labels[1], "B");
}

TEST(BibleNavLabels, APageWithoutHeadingsHasNoSections) {
  const auto page = scanBookNav("<html><body><a href=\"a.xhtml\">A</a></body></html>");

  EXPECT_TRUE(page.sections.empty());
  EXPECT_EQ(page.labels.size(), 1u);
}

TEST(BibleNavLabels, OverlongTextIsCappedOnACharacterBoundary) {
  // 40 two-byte characters: 80 bytes, well past the cap.
  std::string longText;
  for (int i = 0; i < 40; i++) longText += "\xC3\xA9";
  const auto page = scanBookNav("<html><body><a href=\"a.xhtml\">" + longText + "</a></body></html>");

  ASSERT_EQ(page.labels.size(), 1u);
  EXPECT_LE(page.labels[0].size(), BibleNav::MAX_TEXT_BYTES);
  EXPECT_EQ(page.labels[0].size() % 2, 0u);
}

TEST(BibleNavLabels, AMalformedPageYieldsNothing) {
  BibleNav::Scanner scanner;
  const std::string broken = "<html><body><a href=\"a.xhtml\">A</b></body></html>";
  EXPECT_FALSE(scanner.feed(broken.data(), broken.size(), /*isFinal=*/true));

  const auto page = scanner.takeBookNav();
  EXPECT_TRUE(page.targets.empty());
  EXPECT_TRUE(page.labels.empty());
  EXPECT_TRUE(page.sections.empty());
}

TEST(BibleNavLabels, TakeStillReturnsOnlyTargets) {
  BibleNav::Scanner scanner;
  ASSERT_TRUE(scanner.feed(kSpanishBookNav.data(), kSpanishBookNav.size(), /*isFinal=*/true));

  const auto targets = scanner.take();
  ASSERT_EQ(targets.size(), 6u);
  EXPECT_EQ(targets[5], "biblechapternav66.xhtml");
}
