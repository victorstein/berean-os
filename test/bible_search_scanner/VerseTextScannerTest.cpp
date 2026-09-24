#include <gtest/gtest.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "Epub/VerseAnchors.h"
#include "VerseTextScanner.h"

// Each fixture is a short excerpt of one spine document of the Spanish NWT
// (nwt_S.epub): its head, navigation line, a few verses, and the opening of its
// footnote section with one note. Every line kept is verbatim; whole lines are
// only left out.
//   juan2.xhtml      OEBPS/1001061147-split2.xhtml   John 2:1-2 and 2:13-17
//   juan8.xhtml      OEBPS/1001061147-split8.xhtml   John 8:12-20; the chapter opens at 8:12
//   genesis1.xhtml   OEBPS/1001061105.xhtml          Genesis 1:1-5, book title in <header>
//   salmo23.xhtml    OEBPS/1001061123-split23.xhtml  Psalm 23:1-2 after its superscription
//   salmo111.xhtml   OEBPS/1001061123-split111.xhtml Psalm 111:1-2, acrostic headings inside verses
//   salmo119.xhtml   OEBPS/1001061123-split119.xhtml Psalm 119:1-2 and 9, acrostic headings
//   malaquias4.xhtml OEBPS/1001061143-split4.xhtml   Malachi 4:5-6 and the editorial note after them
namespace {

using BibleSearch::VerseText;
using BibleSearch::VerseTextScanner;

std::string fixture(const char* name) {
  std::ifstream in(std::string(BIBLE_SEARCH_FIXTURE_DIR) + "/" + name, std::ios::binary);
  std::stringstream buffer;
  buffer << in.rdbuf();
  return buffer.str();
}

struct Scan {
  bool ok = false;
  std::vector<VerseText> verses;
  std::string continuation;
};

Scan scanInChunks(const std::string& doc, const size_t chunkSize) {
  Scan out;
  VerseTextScanner scanner;
  if (!scanner.valid()) return out;
  out.ok = true;
  for (size_t at = 0; at < doc.size() && out.ok; at += chunkSize) {
    const size_t length = std::min(chunkSize, doc.size() - at);
    out.ok = scanner.feed(doc.data() + at, length, at + length == doc.size());
  }
  out.verses = scanner.take();
  out.continuation = scanner.continuation();
  return out;
}

Scan scanWhole(const std::string& doc) { return scanInChunks(doc, doc.size()); }

const VerseText* findVerse(const std::vector<VerseText>& verses, const uint16_t chapter, const uint16_t verse) {
  for (const auto& v : verses) {
    if (v.chapter == chapter && v.verse == verse) return &v;
  }
  return nullptr;
}

std::string textOf(const Scan& scan, const uint16_t chapter, const uint16_t verse) {
  const VerseText* v = findVerse(scan.verses, chapter, verse);
  return v ? v->text : std::string("<missing>");
}

constexpr const char* ALL_FIXTURES[] = {"juan2.xhtml",    "juan8.xhtml",    "genesis1.xhtml",  "salmo23.xhtml",
                                        "salmo111.xhtml", "salmo119.xhtml", "malaquias4.xhtml"};

}  // namespace

TEST(VerseTextScanner, ExtractsVerseSixteenWithoutItsNumberOrFootnoteMarker) {
  const Scan scan = scanWhole(fixture("juan2.xhtml"));
  ASSERT_TRUE(scan.ok);
  EXPECT_EQ(textOf(scan, 2, 16),
            "Y a los que vendían palomas les dijo: “¡Quiten todo esto de aquí! ¡Dejen de convertir la casa de mi "
            "Padre en un mercado!”.");
}

TEST(VerseTextScanner, FindsEveryVerseInDocumentOrder) {
  const Scan scan = scanWhole(fixture("juan2.xhtml"));
  const uint16_t expected[] = {1, 2, 13, 14, 15, 16, 17};
  ASSERT_EQ(scan.verses.size(), std::size(expected));
  for (size_t i = 0; i < scan.verses.size(); i++) {
    EXPECT_EQ(scan.verses[i].chapter, 2);
    EXPECT_EQ(scan.verses[i].verse, expected[i]);
  }
}

TEST(VerseTextScanner, DropsTheChapterNumberFromTheFirstVerse) {
  const Scan scan = scanWhole(fixture("juan2.xhtml"));
  EXPECT_EQ(textOf(scan, 2, 1),
            "Al tercer día se celebró un banquete de boda en Caná de Galilea, y allí estaba la madre de Jesús.");
}

TEST(VerseTextScanner, DropsAFootnoteMarkerInsideAVerse) {
  // John 2:17 carries its marker mid-sentence: "La devoción*".
  const Scan scan = scanWhole(fixture("juan2.xhtml"));
  EXPECT_EQ(textOf(scan, 2, 17),
            "Sus discípulos recordaron que está escrito: “La devoción que siento por tu casa arderá en mi "
            "interior”.");
}

TEST(VerseTextScanner, EndsTheLastVerseWhereTheFootnoteSectionOpens) {
  const Scan scan = scanWhole(fixture("salmo119.xhtml"));
  EXPECT_EQ(textOf(scan, 119, 9),
            "¿Cómo puede un joven mantener limpio su camino? Estando en guardia y actuando de acuerdo con tu "
            "palabra.");
}

TEST(VerseTextScanner, NeverCollectsFootnoteText) {
  for (const char* name : ALL_FIXTURES) {
    const Scan scan = scanWhole(fixture(name));
    ASSERT_TRUE(scan.ok) << name;
    for (const auto& v : scan.verses) {
      // Every footnote body opens with a "^ Juan 2:4"-style back-link, and most
      // with "Lit." or "O “".
      EXPECT_EQ(v.text.find('^'), std::string::npos) << name << " " << v.chapter << ":" << v.verse;
      EXPECT_EQ(v.text.find("Lit."), std::string::npos) << name << " " << v.chapter << ":" << v.verse;
    }
  }
  const Scan juan2 = scanWhole(fixture("juan2.xhtml"));
  for (const auto& v : juan2.verses) {
    EXPECT_EQ(v.text.find("Expresión idiomática"), std::string::npos) << "the footnote kept in the excerpt";
  }
}

TEST(VerseTextScanner, AnchorOffsetsEqualVerseAnchorsForEveryVerse) {
  for (const char* name : ALL_FIXTURES) {
    const std::string doc = fixture(name);
    const Scan scan = scanWhole(doc);
    const auto anchors = VerseAnchors::scan(doc.data(), doc.size());
    ASSERT_FALSE(anchors.empty()) << name;
    ASSERT_EQ(scan.verses.size(), anchors.size()) << name;
    for (size_t i = 0; i < anchors.size(); i++) {
      EXPECT_EQ(scan.verses[i].chapter, anchors[i].chapter) << name;
      EXPECT_EQ(scan.verses[i].verse, anchors[i].verse) << name;
      EXPECT_EQ(scan.verses[i].anchorOffset, anchors[i].offset) << name << " verse " << anchors[i].verse;
    }
  }
}

TEST(VerseTextScanner, ADocumentOpeningPastVerseOneHasNoContinuation) {
  // No NWT document starts mid-verse: each holds exactly one chapter. John 8
  // is the one that does not open at verse 1, because the NWT omits 7:53-8:11.
  const Scan scan = scanWhole(fixture("juan8.xhtml"));
  ASSERT_TRUE(scan.ok);
  ASSERT_FALSE(scan.verses.empty());
  EXPECT_EQ(scan.verses.front().verse, 12);
  EXPECT_EQ(scan.continuation, "");
  EXPECT_EQ(textOf(scan, 8, 12),
            "Entonces Jesús les habló de nuevo. Dijo: “Yo soy la luz del mundo. El que me siga nunca andará en la "
            "oscuridad, sino que tendrá la luz de la vida”.");
}

TEST(VerseTextScanner, NavigationHeadingsAndSuperscriptionsAreNotContinuation) {
  // Every real document opens with a navigation line ("Salmos 23 : 1 - 6"),
  // and some with a book title or a psalm superscription. Attaching any of it
  // to the previous chapter's last verse would index it under the wrong verse.
  for (const char* name : ALL_FIXTURES) {
    EXPECT_EQ(scanWhole(fixture(name)).continuation, "") << name;
  }
  const Scan salmo23 = scanWhole(fixture("salmo23.xhtml"));
  EXPECT_EQ(textOf(salmo23, 23, 1), "Jehová es mi Pastor. Nada me faltará.");
  const Scan genesis1 = scanWhole(fixture("genesis1.xhtml"));
  EXPECT_EQ(textOf(genesis1, 1, 1), "En el principio, Dios creó los cielos y la tierra.");
}

TEST(VerseTextScanner, SeparatesPoetryLinesThatShareNoWhitespace) {
  // Psalm 23:2 ends one <p> and opens the next with nothing between them.
  const Scan scan = scanWhole(fixture("salmo23.xhtml"));
  EXPECT_EQ(textOf(scan, 23, 2),
            "En prados cubiertos de hierba me hace reposar; me lleva a lugares de descanso donde abunda el agua.");
}

TEST(VerseTextScanner, DropsAcrosticHeadingsInsideAVerse) {
  const Scan scan = scanWhole(fixture("salmo111.xhtml"));
  EXPECT_EQ(textOf(scan, 111, 1),
            "¡Alaben a Jah! Alabaré a Jehová con todo mi corazón entre los justos reunidos y en la congregación.");
}

TEST(VerseTextScanner, DropsTheEditorialNoteAfterTheLastVerse) {
  const Scan scan = scanWhole(fixture("malaquias4.xhtml"));
  ASSERT_EQ(scan.verses.size(), 2u);
  EXPECT_EQ(textOf(scan, 4, 6),
            "Y él hará que el corazón de los padres se vuelva hacia los hijos y el corazón de los hijos hacia los "
            "padres, para que yo no venga y golpee la tierra, entregándola a la destrucción”.");
}

TEST(VerseTextScanner, DropsAcrosticHeadingsBetweenVerses) {
  // The whole of Psalm 119 is 176 verses in 69 KB, headed every eight verses
  // by a Hebrew letter; the excerpt keeps the first heading and the second.
  const Scan scan = scanWhole(fixture("salmo119.xhtml"));
  ASSERT_EQ(scan.verses.size(), 3u);
  EXPECT_EQ(textOf(scan, 119, 1),
            "Felices los que son intachables en su camino, los que andan de acuerdo con la ley de Jehová.");
  EXPECT_EQ(textOf(scan, 119, 2),
            "Felices los que hacen caso de sus recordatorios, los que lo buscan con todo el corazón.");
  for (const auto& v : scan.verses) {
    EXPECT_EQ(v.text.find("[álef]"), std::string::npos) << "acrostic heading leaked into 119:" << v.verse;
    EXPECT_EQ(v.text.find("[bet]"), std::string::npos) << "acrostic heading leaked into 119:" << v.verse;
  }
}

TEST(VerseTextScanner, ByteByByteFeedingMatchesWholeDocumentFeeding) {
  for (const char* name : ALL_FIXTURES) {
    const std::string doc = fixture(name);
    const Scan whole = scanWhole(doc);
    const Scan bytes = scanInChunks(doc, 1);
    ASSERT_TRUE(bytes.ok) << name;
    ASSERT_EQ(bytes.verses.size(), whole.verses.size()) << name;
    for (size_t i = 0; i < whole.verses.size(); i++) {
      EXPECT_EQ(bytes.verses[i].text, whole.verses[i].text) << name << " verse " << whole.verses[i].verse;
      EXPECT_EQ(bytes.verses[i].anchorOffset, whole.verses[i].anchorOffset) << name;
    }
    EXPECT_EQ(bytes.continuation, whole.continuation) << name;
  }
}

TEST(VerseTextScanner, CapturesContinuationTextBeforeTheFirstMarker) {
  // Synthetic: no real NWT document starts mid-verse, but a Bible split
  // differently must not lose the tail of a verse or index it as verse 1.
  const char* doc =
      "<html><body><p class=\"w_navigation w_biblebookname\"><a href=\"x\">Juan</a> 3</p>"
      "<p>de su Hijo unigénito.</p>"
      "<p><span id=\"chapter3_verse17\"></span><strong><sup>17</sup></strong> Dios envió.</p></body></html>";
  const Scan scan = scanWhole(doc);
  ASSERT_TRUE(scan.ok);
  EXPECT_EQ(scan.continuation, "de su Hijo unigénito.");
  ASSERT_EQ(scan.verses.size(), 1u);
  EXPECT_EQ(scan.verses[0].text, "Dios envió.");
}

TEST(VerseTextScanner, DropsALinkWhoseTextIsALoneMarkerSymbol) {
  const char* doc =
      "<html><body><p><span id=\"chapter1_verse1\"></span>uno<a href=\"#x\">*</a> dos<a href=\"#y\"> + </a> "
      "tres <a href=\"#z\">cuatro</a></p></body></html>";
  const Scan scan = scanWhole(doc);
  ASSERT_EQ(scan.verses.size(), 1u);
  EXPECT_EQ(scan.verses[0].text, "uno dos tres cuatro");
}

TEST(VerseTextScanner, KeepsAnEmptyVerseSoAnchorsStayPaired) {
  // The NWT omits some verses (Mark 9:44, 9:46) and keeps only the number and
  // a footnote marker.
  const char* doc =
      "<html><body><p><span id=\"chapter9_verse45\"></span><strong><sup>45</sup></strong> Y si tu pie. "
      "<span id=\"chapter9_verse46\"></span><strong><sup>46</sup></strong> <a epub:type=\"noteref\" "
      "xmlns:epub=\"http://www.idpf.org/2007/ops\" href=\"#f\">*</a> "
      "<span id=\"chapter9_verse47\"></span><strong><sup>47</sup></strong> Y si tu ojo.</p></body></html>";
  const Scan scan = scanWhole(doc);
  ASSERT_EQ(scan.verses.size(), 3u);
  EXPECT_EQ(scan.verses[0].text, "Y si tu pie.");
  EXPECT_EQ(scan.verses[1].text, "");
  EXPECT_EQ(scan.verses[2].text, "Y si tu ojo.");
}

TEST(VerseTextScanner, CapsAVerseOnACodepointBoundary) {
  std::string doc = "<html><body><p><span id=\"chapter1_verse1\"></span>";
  doc += "a";
  for (int i = 0; i < 3000; i++) doc += "é";  // 1 + 6000 bytes: an odd cap would split a codepoint
  doc += "</p></body></html>";
  const Scan scan = scanWhole(doc);
  ASSERT_EQ(scan.verses.size(), 1u);
  EXPECT_LE(scan.verses[0].text.size(), BibleSearch::MAX_VERSE_TEXT_BYTES);
  EXPECT_GE(scan.verses[0].text.size(), BibleSearch::MAX_VERSE_TEXT_BYTES - 1);
  EXPECT_NE(static_cast<unsigned char>(scan.verses[0].text.back()), 0xC3) << "cut must not leave a lead byte";
}

TEST(VerseTextScanner, AMalformedDocumentGivesNothing) {
  const std::string doc = fixture("juan2.xhtml");
  const std::string broken = doc.substr(0, doc.find("</p>")) + "</div>" + doc.substr(doc.find("</p>") + 4);
  const Scan scan = scanWhole(broken);
  EXPECT_FALSE(scan.ok);
  EXPECT_TRUE(scan.verses.empty());
  EXPECT_TRUE(scan.continuation.empty());
}

TEST(VerseTextScanner, TakeReturnsOnlyCompletedVersesMidDocument) {
  const std::string doc = fixture("juan2.xhtml");
  const size_t cut = doc.find("chapter2_verse13");
  VerseTextScanner scanner;
  ASSERT_TRUE(scanner.feed(doc.data(), cut, false));
  const auto first = scanner.take();
  ASSERT_EQ(first.size(), 1u) << "verse 2 is still open until verse 13's marker arrives";
  EXPECT_EQ(first[0].verse, 1);
  ASSERT_TRUE(scanner.feed(doc.data() + cut, doc.size() - cut, true));
  const auto rest = scanner.take();
  ASSERT_EQ(rest.size(), 6u);
  EXPECT_EQ(rest[0].verse, 2);
  EXPECT_EQ(rest[0].text, "También invitaron al banquete de boda a Jesús y a sus discípulos.");
  const auto anchors = VerseAnchors::scan(doc.data(), doc.size());
  EXPECT_EQ(rest[0].anchorOffset, anchors[1].offset);
}

TEST(VerseTextScanner, ASkippedBlockStillSeparatesTheWordsAroundIt) {
  const char* doc =
      "<html><body><p><span id=\"chapter1_verse1\"></span>corazón<p class=\"ss\">ב [bet]</p>entre</p>"
      "<p><span id=\"chapter1_verse2\"></span>uno<h2>título</h2>dos</p></body></html>";
  const Scan scan = scanWhole(doc);
  ASSERT_EQ(scan.verses.size(), 2u);
  EXPECT_EQ(scan.verses[0].text, "corazón entre");
  EXPECT_EQ(scan.verses[1].text, "uno dos");
}

TEST(VerseTextScanner, KeepsWorkingAfterTakeMovesTheVersesOut) {
  const std::string doc = fixture("juan8.xhtml");
  const size_t cut = doc.find("chapter8_verse16\"");
  VerseTextScanner scanner;
  ASSERT_TRUE(scanner.feed(doc.data(), cut, false));
  const auto first = scanner.take();
  ASSERT_TRUE(scanner.feed(doc.data() + cut, doc.size() - cut, true));
  const auto rest = scanner.take();
  EXPECT_EQ(first.size() + rest.size(), 9u);
  EXPECT_EQ(rest.back().verse, 20);
}

namespace {

// Counts every allocation the scanner's own parser makes and refuses them once
// `allocationsLeft` runs out.
size_t allocations = 0;
size_t allocationsLeft = SIZE_MAX;

void* limitedAllocate(const size_t bytes) {
  allocations++;
  if (allocationsLeft == 0) return nullptr;
  allocationsLeft--;
  return std::malloc(bytes);
}

void* limitedReallocate(void* block, const size_t bytes) {
  allocations++;
  if (allocationsLeft == 0) return nullptr;
  allocationsLeft--;
  return std::realloc(block, bytes);
}

constexpr BibleSearch::ParserMemory LIMITED_MEMORY{limitedAllocate, limitedReallocate, std::free};

}  // namespace

TEST(VerseTextScanner, ReportsRunningOutOfMemoryApartFromBadMarkup) {
  allocations = 0;
  allocationsLeft = SIZE_MAX;
  size_t toCreate = 0;
  {
    VerseTextScanner probe(&LIMITED_MEMORY);
    ASSERT_TRUE(probe.valid());
    toCreate = allocations;
  }

  // Enough to create the parser and nothing more: its first allocation while
  // parsing fails.
  allocationsLeft = toCreate;
  VerseTextScanner scanner(&LIMITED_MEMORY);
  ASSERT_TRUE(scanner.valid());
  const std::string doc = fixture("juan2.xhtml");
  EXPECT_FALSE(scanner.feed(doc.data(), doc.size(), true));
  EXPECT_TRUE(scanner.outOfMemory());
  EXPECT_TRUE(scanner.take().empty());
  allocationsLeft = SIZE_MAX;
}

TEST(VerseTextScanner, BadMarkupIsNotOutOfMemory) {
  VerseTextScanner scanner;
  ASSERT_TRUE(scanner.valid());
  const std::string doc = "<html><body><p><span id=\"chapter1_verse1\"></span>text</b></p></body></html>";
  EXPECT_FALSE(scanner.feed(doc.data(), doc.size(), true));
  EXPECT_FALSE(scanner.outOfMemory());
}
