#include <gtest/gtest.h>

#include <algorithm>
#include <string>

#include "Preaching/QuestionParser.h"

namespace {

using preaching::BlockKind;
using preaching::Question;
using preaching::QuestionParser;

// Short excerpts of the NWT intro section's markup, never whole files: the
// repository is public.
std::string page(const std::string& body) {
  return "<?xml version=\"1.0\" encoding=\"utf-8\" ?>\n"
         "<html xmlns=\"http://www.w3.org/1999/xhtml\" xml:lang=\"es\">\n"
         "<head><title>Pregunta</title></head>\n"
         "<body>" +
         body + "</body>\n</html>\n";
}

// Under XML_GE=0 expat hands an undeclared entity to the default handler only
// once the document declares an external subset.
std::string pageWithDoctype(const std::string& body) {
  return "<?xml version=\"1.0\" encoding=\"utf-8\" ?>\n"
         "<!DOCTYPE html PUBLIC \"-//W3C//DTD XHTML 1.1//EN\" \"xhtml11.dtd\">\n"
         "<html xmlns=\"http://www.w3.org/1999/xhtml\"><body>" +
         body + "</body></html>\n";
}

Question parsed(const std::string& xhtml) {
  Question question;
  EXPECT_TRUE(preaching::parseQuestion(xhtml.data(), xhtml.size(), question));
  return question;
}

bool refused(const std::string& xhtml) {
  Question question;
  question.title = "untouched";
  const bool ok = preaching::parseQuestion(xhtml.data(), xhtml.size(), question);
  EXPECT_EQ(question.title, "untouched");
  return !ok;
}

Question parsedInChunks(const std::string& xhtml, const size_t chunkBytes) {
  QuestionParser parser;
  EXPECT_TRUE(parser.valid());
  for (size_t offset = 0; offset < xhtml.size(); offset += chunkBytes) {
    const size_t length = std::min(chunkBytes, xhtml.size() - offset);
    EXPECT_TRUE(parser.feed(xhtml.data() + offset, length, offset + length == xhtml.size())) << "at " << offset;
  }
  Question question;
  EXPECT_TRUE(parser.take(question));
  return question;
}

void expectSame(const Question& actual, const Question& expected, const std::string& where) {
  EXPECT_EQ(actual.title, expected.title) << where;
  ASSERT_EQ(actual.blocks.size(), expected.blocks.size()) << where;
  for (size_t i = 0; i < expected.blocks.size(); i++) {
    EXPECT_EQ(actual.blocks[i].kind, expected.blocks[i].kind) << where << " block " << i;
    EXPECT_EQ(actual.blocks[i].text, expected.blocks[i].text) << where << " block " << i;
    EXPECT_EQ(actual.blocks[i].reference, expected.blocks[i].reference) << where << " block " << i;
    EXPECT_EQ(actual.blocks[i].href, expected.blocks[i].href) << where << " block " << i;
  }
}

const char* const Q1_HEADER =
    "<header>\n"
    "<p class=\"contextTtl\" id=\"p1\" data-pid=\"1\"><span id=\"page6\" class=\"pageNum\" aria-hidden=\"true\" "
    "data-no=\"6\" data-before-text=\"6\"></span><strong>PREGUNTA 1</strong></p>\n"
    "<h1 id=\"p2\" data-pid=\"2\"><strong>¿Quién es Dios?</strong></h1>\n"
    "</header>\n";

const char* const Q1_FIGURE =
    "<div id=\"f1\" class=\"east_right\">\n<figure>\n"
    "<img src=\"images/1001061251_univ_cnt_1.jpg\" alt=\"Un hombre mirando hacia el cielo\" class=\"east_right\" />\n"
    "</figure>\n</div>\n";

const char* const Q1_PAIR =
    "<p id=\"p3\" data-pid=\"3\" class=\"p3 sw\"><q class=\"scrp\">“Que la gente sepa que tu nombre es Jehová, que "
    "solo tú eres el Altísimo sobre toda la tierra”.</q></p>\n"
    "<p id=\"p4\" data-pid=\"4\" class=\"p4 sw\"><a href=\"1001061123-split83.xhtml#chapter83_verse18\">Salmo "
    "83:18</a></p>\n";

TEST(QuestionParser, TakesTheTitleAndSkipsTheQuestionLabel) {
  const Question question = parsed(page(Q1_HEADER));
  EXPECT_EQ(question.title, "¿Quién es Dios?");
  EXPECT_TRUE(question.blocks.empty());
}

TEST(QuestionParser, FigureAddsNothing) {
  const Question question = parsed(page(std::string(Q1_HEADER) + "<div class=\"bodyTxt\">\n" + Q1_FIGURE + "</div>\n"));
  EXPECT_TRUE(question.blocks.empty());
}

TEST(QuestionParser, SkipsFigureCaptionsAndPageNumbersThatCarryText) {
  const Question question =
      parsed(page("<h2><span class=\"pageNum\">15</span><strong>PROFECÍA</strong></h2>\n"
                  "<figure><img src=\"a.jpg\" alt=\"Un hombre\" /><p>Un pie de foto</p></figure>\n"
                  "<p><span class=\"pageNum\">7</span>Texto</p>\n"));
  ASSERT_EQ(question.blocks.size(), 2u);
  EXPECT_EQ(question.blocks[0].text, "PROFECÍA");
  EXPECT_EQ(question.blocks[1].text, "Texto");
}

TEST(QuestionParser, PairsAQuoteWithItsReference) {
  const Question question =
      parsed(page(std::string(Q1_HEADER) + "<div class=\"bodyTxt\">\n" + Q1_FIGURE + Q1_PAIR + "</div>\n"));
  ASSERT_EQ(question.blocks.size(), 1u);
  const auto& verse = question.blocks[0];
  EXPECT_EQ(verse.kind, BlockKind::Verse);
  EXPECT_EQ(verse.text,
            "“Que la gente sepa que tu nombre es Jehová, que solo tú eres el Altísimo sobre toda la tierra”.");
  EXPECT_EQ(verse.reference, "Salmo 83:18");
  EXPECT_EQ(verse.href, "1001061123-split83.xhtml#chapter83_verse18");
}

TEST(QuestionParser, PageNumberInsideAQuoteParagraphAddsNothing) {
  const Question question = parsed(page(
      "<p id=\"p13\" class=\"p13 sw ruleAbove\"><span id=\"page7\" class=\"pageNum\" aria-hidden=\"true\" "
      "data-no=\"7\" data-before-text=\"7\"></span><q class=\"scrp\">“Levanten la vista al cielo y vean”.</q></p>\n"
      "<p id=\"p14\" class=\"p14 sw\"><a href=\"1001061127-split40.xhtml#chapter40_verse26\">Isaías "
      "40:26</a></p>\n"));
  ASSERT_EQ(question.blocks.size(), 1u);
  EXPECT_EQ(question.blocks[0].text, "“Levanten la vista al cielo y vean”.");
  EXPECT_EQ(question.blocks[0].reference, "Isaías 40:26");
}

TEST(QuestionParser, JoinsTwoLinksIntoOneReference) {
  const Question question = parsed(
      page("<p id=\"p5\" class=\"p5 sw ruleAbove\"><q class=\"scrp\">“Porque los vivos saben que morirán, pero los "
           "muertos no saben nada”.</q></p>\n"
           "<p id=\"p6\" class=\"p6 sw\"><a href=\"1001061125-split9.xhtml#chapter9_verse5\">Eclesiastés "
           "9:5,</a><a href=\"1001061125-split9.xhtml#chapter9_verse10\"> 10</a></p>\n"));
  ASSERT_EQ(question.blocks.size(), 1u);
  EXPECT_EQ(question.blocks[0].reference, "Eclesiastés 9:5, 10");
  EXPECT_EQ(question.blocks[0].href, "1001061125-split9.xhtml#chapter9_verse5");
}

TEST(QuestionParser, KeepsTheFirstHrefOfAReferenceAcrossChapters) {
  const Question question =
      parsed(page("<p class=\"p31 sw\"><q class=\"scrp\">“Quedaron en darle 30 monedas de plata”.</q></p>\n"
                  "<p id=\"p32\" class=\"p32 sw\"><a href=\"1001061144-split26.xhtml#chapter26_verse14\">Mateo 26:14, "
                  "15;</a><a href=\"1001061144-split27.xhtml#chapter27_verse5\"> 27:5</a></p>\n"));
  ASSERT_EQ(question.blocks.size(), 1u);
  EXPECT_EQ(question.blocks[0].reference, "Mateo 26:14, 15; 27:5");
  EXPECT_EQ(question.blocks[0].href, "1001061144-split26.xhtml#chapter26_verse14");
}

TEST(QuestionParser, KeepsQuestionSixHeadingsInOrder) {
  const Question question = parsed(page(
      "<div class=\"bodyTxt\">\n"
      "<h2 class=\"sw ruleAbove\" id=\"p3\" data-pid=\"3\"><strong>PROFECÍA</strong></h2>\n"
      "<p id=\"p4\" class=\"p4 sw\"><q class=\"scrp\">“De ti, Belén Efrata, [...] de ti saldrá para mí el que será "
      "gobernante en Israel”.</q></p>\n"
      "<p id=\"p5\" class=\"p5 sw\"><a href=\"1001061137-split5.xhtml#chapter5_verse2\">Miqueas 5:2</a></p>\n"
      "<h2 id=\"p6\" data-pid=\"6\"><strong>CUMPLIMIENTO</strong></h2>\n"
      "<p id=\"p7\" class=\"p7 sw\"><q class=\"scrp\">“Después de nacer Jesús en Belén de Judea [...]”.</q></p>\n"
      "<p id=\"p8\" class=\"p8 sw\"><a href=\"1001061144-split2.xhtml#chapter2_verse1\">Mateo 2:1</a></p>\n"
      "<h2 class=\"sw ruleAbove\" id=\"p15\" data-pid=\"15\"><span id=\"page15\" class=\"pageNum\" "
      "aria-hidden=\"true\" data-no=\"15\" data-before-text=\"15\"></span><strong>PROFECÍA</strong></h2>\n"
      "</div>\n<p id=\"p33\" data-pid=\"33\" class=\"p33\"></p>\n"));
  ASSERT_EQ(question.blocks.size(), 5u);
  EXPECT_EQ(question.blocks[0].kind, BlockKind::Heading);
  EXPECT_EQ(question.blocks[0].text, "PROFECÍA");
  EXPECT_EQ(question.blocks[1].kind, BlockKind::Verse);
  EXPECT_EQ(question.blocks[1].text,
            "“De ti, Belén Efrata, [...] de ti saldrá para mí el que será gobernante en Israel”.");
  EXPECT_EQ(question.blocks[1].reference, "Miqueas 5:2");
  EXPECT_EQ(question.blocks[2].kind, BlockKind::Heading);
  EXPECT_EQ(question.blocks[2].text, "CUMPLIMIENTO");
  EXPECT_EQ(question.blocks[3].kind, BlockKind::Verse);
  EXPECT_EQ(question.blocks[3].reference, "Mateo 2:1");
  EXPECT_EQ(question.blocks[4].kind, BlockKind::Heading);
  EXPECT_EQ(question.blocks[4].text, "PROFECÍA");
}

TEST(QuestionParser, ReadsQuestionNineteenProse) {
  const Question question = parsed(
      page("<div class=\"bodyTxt\">\n"
           "<h2 class=\"ss ruleAbove\" id=\"p3\" data-pid=\"3\"><strong>LAS ESCRITURAS HEBREAS, TAMBIÉN CONOCIDAS "
           "COMO ANTIGUO TESTAMENTO</strong></h2>\n"
           "<h3 id=\"p4\" data-pid=\"4\"><strong>EL PENTATEUCO (5 LIBROS):</strong></h3>\n"
           "<p id=\"p5\" data-pid=\"5\" class=\"p5 sw\"><strong>Génesis, Éxodo, Levítico, Números y "
           "Deuteronomio</strong></p>\n"
           "<p id=\"p6\" data-pid=\"6\" class=\"p6 sw\">Desde la creación del mundo hasta el nacimiento de la nación "
           "de Israel</p>\n"
           "</div>\n"));
  ASSERT_EQ(question.blocks.size(), 4u);
  EXPECT_EQ(question.blocks[0].kind, BlockKind::Heading);
  EXPECT_EQ(question.blocks[0].text, "LAS ESCRITURAS HEBREAS, TAMBIÉN CONOCIDAS COMO ANTIGUO TESTAMENTO");
  EXPECT_EQ(question.blocks[1].kind, BlockKind::Heading);
  EXPECT_EQ(question.blocks[1].text, "EL PENTATEUCO (5 LIBROS):");
  EXPECT_EQ(question.blocks[2].kind, BlockKind::Text);
  EXPECT_EQ(question.blocks[2].text, "Génesis, Éxodo, Levítico, Números y Deuteronomio");
  EXPECT_EQ(question.blocks[3].kind, BlockKind::Text);
  EXPECT_EQ(question.blocks[3].text, "Desde la creación del mundo hasta el nacimiento de la nación de Israel");
}

TEST(QuestionParser, CollapsesWhitespaceRuns) {
  const Question question = parsed(page("<p>\n  ¿Qué me enseña\n\t sobre   Dios?  </p>"));
  ASSERT_EQ(question.blocks.size(), 1u);
  EXPECT_EQ(question.blocks[0].text, "¿Qué me enseña sobre Dios?");
}

TEST(QuestionParser, AReferenceWithNoQuoteIsAVerseWithAnEmptyQuote) {
  const Question question =
      parsed(page("<p class=\"p15 sw\"><a href=\"1001061153-split6.xhtml#chapter6_verse1\">Efesios 6:1-3</a></p>"));
  ASSERT_EQ(question.blocks.size(), 1u);
  EXPECT_EQ(question.blocks[0].kind, BlockKind::Verse);
  EXPECT_EQ(question.blocks[0].text, "");
  EXPECT_EQ(question.blocks[0].reference, "Efesios 6:1-3");
  EXPECT_EQ(question.blocks[0].href, "1001061153-split6.xhtml#chapter6_verse1");
}

TEST(QuestionParser, AQuoteWithNoReferenceIsText) {
  const Question beforeHeading =
      parsed(page("<p><q class=\"scrp\">“Uno”.</q></p><h2><strong>CUMPLIMIENTO</strong></h2>"));
  ASSERT_EQ(beforeHeading.blocks.size(), 2u);
  EXPECT_EQ(beforeHeading.blocks[0].kind, BlockKind::Text);
  EXPECT_EQ(beforeHeading.blocks[0].text, "“Uno”.");
  EXPECT_EQ(beforeHeading.blocks[1].kind, BlockKind::Heading);

  const Question atEnd = parsed(page("<p>Antes</p><p><q class=\"scrp\">“Uno”.</q></p>"));
  ASSERT_EQ(atEnd.blocks.size(), 2u);
  EXPECT_EQ(atEnd.blocks[1].kind, BlockKind::Text);
  EXPECT_EQ(atEnd.blocks[1].text, "“Uno”.");
}

TEST(QuestionParser, OnlyTheSecondOfTwoQuotesPairs) {
  const Question question =
      parsed(page("<p><q class=\"scrp\">“Uno”.</q></p><p><q class=\"scrp\">“Dos”.</q></p>"
                  "<p><a href=\"a.xhtml#chapter1_verse2\">Juan 1:2</a></p>"));
  ASSERT_EQ(question.blocks.size(), 2u);
  EXPECT_EQ(question.blocks[0].kind, BlockKind::Text);
  EXPECT_EQ(question.blocks[0].text, "“Uno”.");
  EXPECT_EQ(question.blocks[1].kind, BlockKind::Verse);
  EXPECT_EQ(question.blocks[1].text, "“Dos”.");
}

TEST(QuestionParser, EmptyParagraphsAddNothingAndKeepAPair) {
  const Question question =
      parsed(page("<p><q class=\"scrp\">“Uno”.</q></p><p class=\"p9\"></p>"
                  "<p><a href=\"a.xhtml#chapter1_verse2\">Juan 1:2</a></p><p class=\"p19\"> </p>"));
  ASSERT_EQ(question.blocks.size(), 1u);
  EXPECT_EQ(question.blocks[0].kind, BlockKind::Verse);
  EXPECT_EQ(question.blocks[0].text, "“Uno”.");
}

TEST(QuestionParser, ProseWithAnInlineLinkIsText) {
  const Question question = parsed(page("<p>Lea <a href=\"a.xhtml#chapter3_verse16\">Juan 3:16</a> hoy.</p>"));
  ASSERT_EQ(question.blocks.size(), 1u);
  EXPECT_EQ(question.blocks[0].kind, BlockKind::Text);
  EXPECT_EQ(question.blocks[0].text, "Lea Juan 3:16 hoy.");
  EXPECT_EQ(question.blocks[0].href, "");
}

TEST(QuestionParser, ResolvesEntitiesInADocumentWithADoctype) {
  const Question question = parsed(pageWithDoctype("<p>a&nbsp;b &amp; c &foo; d</p>"));
  ASSERT_EQ(question.blocks.size(), 1u);
  EXPECT_EQ(question.blocks[0].text,
            "a\xC2\xA0"
            "b & c &foo; d");
}

TEST(QuestionParser, RefusesAnEntityWithoutADoctype) { EXPECT_TRUE(refused(page("<p>a&nbsp;b</p>"))); }

TEST(QuestionParser, RefusesMalformedMarkup) {
  QuestionParser parser;
  const std::string xhtml = "<html><body><p>open";
  EXPECT_FALSE(parser.feed(xhtml.data(), xhtml.size(), true));
  Question question;
  EXPECT_FALSE(parser.take(question));
  EXPECT_FALSE(parser.feed("</p>", 4, true));
  EXPECT_FALSE(parser.outOfMemory());
}

TEST(QuestionParser, TakesOnlyAfterTheFinalChunkAndOnlyOnce) {
  QuestionParser parser;
  const std::string xhtml = page("<p>Uno</p>");
  ASSERT_TRUE(parser.feed(xhtml.data(), xhtml.size(), false));
  Question question;
  EXPECT_FALSE(parser.take(question));
  ASSERT_TRUE(parser.feed("", 0, true));
  ASSERT_TRUE(parser.take(question));
  EXPECT_EQ(question.blocks.size(), 1u);
  EXPECT_FALSE(parser.take(question));
  EXPECT_FALSE(parser.feed("", 0, true));
}

TEST(QuestionParser, ChunkBoundariesDoNotChangeTheResult) {
  const std::string xhtml = pageWithDoctype(std::string(Q1_HEADER) + "<div class=\"bodyTxt\">\n" + Q1_FIGURE + Q1_PAIR +
                                            "<p class=\"p5 sw\">Él&nbsp;nos hizo &amp; le pertenecemos</p>\n</div>\n");
  Question expected;
  ASSERT_TRUE(preaching::parseQuestion(xhtml.data(), xhtml.size(), expected));
  ASSERT_EQ(expected.title, "¿Quién es Dios?");
  ASSERT_EQ(expected.blocks.size(), 2u);
  ASSERT_EQ(expected.blocks[0].reference, "Salmo 83:18");
  ASSERT_EQ(expected.blocks[1].text, "Él\xC2\xA0nos hizo & le pertenecemos");

  for (const size_t chunkBytes : {1u, 2u, 3u, 7u, 64u}) {
    expectSame(parsedInChunks(xhtml, chunkBytes), expected, "chunk " + std::to_string(chunkBytes));
  }
  for (size_t split = 1; split < xhtml.size(); split++) {
    QuestionParser parser;
    ASSERT_TRUE(parser.feed(xhtml.data(), split, false)) << "split " << split;
    ASSERT_TRUE(parser.feed(xhtml.data() + split, xhtml.size() - split, true)) << "split " << split;
    Question question;
    ASSERT_TRUE(parser.take(question));
    expectSame(question, expected, "split " + std::to_string(split));
  }
}

std::string paragraphs(const size_t count) {
  std::string body;
  for (size_t i = 0; i < count; i++) body += "<p>x</p>";
  return body;
}

TEST(QuestionParser, AcceptsExactlyTheBlockLimit) {
  EXPECT_EQ(parsed(page(paragraphs(preaching::MAX_BLOCKS))).blocks.size(), preaching::MAX_BLOCKS);
}

TEST(QuestionParser, RefusesOneBlockPastTheLimit) { EXPECT_TRUE(refused(page(paragraphs(preaching::MAX_BLOCKS + 1)))); }

TEST(QuestionParser, RefusesWhenTheTrailingUnpairedQuoteCrossesTheBlockLimit) {
  const std::string xhtml = page(paragraphs(preaching::MAX_BLOCKS) + "<p><q class=\"scrp\">“Uno”.</q></p>");
  QuestionParser parser;
  EXPECT_FALSE(parser.feed(xhtml.data(), xhtml.size(), true));
  Question question;
  EXPECT_FALSE(parser.take(question));
}

TEST(QuestionParser, AcceptsExactlyTheTextLimit) {
  const Question question = parsed(page("<p>" + std::string(preaching::MAX_TEXT_BYTES, 'x') + "</p>"));
  ASSERT_EQ(question.blocks.size(), 1u);
  EXPECT_EQ(question.blocks[0].text.size(), preaching::MAX_TEXT_BYTES);
}

TEST(QuestionParser, RefusesOneByteOverTheTextLimit) {
  EXPECT_TRUE(refused(page("<p>" + std::string(preaching::MAX_TEXT_BYTES + 1, 'x') + "</p>")));
}

TEST(QuestionParser, CountsTitleAndHrefTowardsTheTextLimit) {
  const std::string href = "a.xhtml#chapter1_verse1";
  const size_t room = preaching::MAX_TEXT_BYTES - 5 - 4 - href.size();
  const std::string fits =
      "<h1>Title</h1><p>" + std::string(room, 'x') + "</p><p><a href=\"" + href + "\">Juan</a></p>";
  EXPECT_EQ(parsed(page(fits)).blocks.size(), 2u);
  const std::string over =
      "<h1>Title</h1><p>" + std::string(room + 1, 'x') + "</p><p><a href=\"" + href + "\">Juan</a></p>";
  EXPECT_TRUE(refused(page(over)));
}

TEST(QuestionParser, RefusesAnOversizedParagraphBeforeItCloses) {
  const std::string opening = "<html><body><p>" + std::string(9000, 'x') + "<";
  QuestionParser parser;
  EXPECT_FALSE(parser.feed(opening.data(), opening.size(), false));
}

}  // namespace
