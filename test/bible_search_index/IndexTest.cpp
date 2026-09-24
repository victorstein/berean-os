#include <gtest/gtest.h>

#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "Epub/VerseAnchors.h"
#include "IndexBuilder.h"
#include "IndexFormat.h"
#include "IndexReader.h"
#include "Query.h"
#include "VerseTextScanner.h"

namespace {

using namespace BibleSearch;
using Bytes = std::vector<uint8_t>;
using Verses = std::vector<uint16_t>;

constexpr uint64_t FINGERPRINT = 0x1234567890ABCDEFull;

bool appendToBytes(void* ctx, const void* data, const size_t length) {
  auto* out = static_cast<Bytes*>(ctx);
  const auto* p = static_cast<const uint8_t*>(data);
  out->insert(out->end(), p, p + length);
  return true;
}

bool readFromBytes(void* ctx, const uint32_t offset, void* dst, const uint32_t len) {
  const auto* in = static_cast<const Bytes*>(ctx);
  if (static_cast<size_t>(offset) + len > in->size()) return false;
  memcpy(dst, in->data() + offset, len);
  return true;
}

ByteSource sourceOf(const Bytes& bytes) {
  ByteSource s;
  s.ctx = const_cast<Bytes*>(&bytes);
  s.readAt = readFromBytes;
  s.size = static_cast<uint32_t>(bytes.size());
  return s;
}

Bytes written(const IndexBuilder& builder, const uint64_t fingerprint = FINGERPRINT) {
  Bytes out;
  EXPECT_TRUE(builder.write(appendToBytes, &out, fingerprint));
  return out;
}

struct SyntheticVerse {
  uint8_t chapter;
  uint8_t verse;
  const char* text;
};

// Three chapters. Global verse numbers are the array indices.
constexpr SyntheticVerse CORPUS[] = {
    {1, 1, "El amor es paciente y bondadoso."},       // 0
    {1, 2, "El amor no es celoso."},                  // 1
    {1, 3, "Señor, ten paciencia conmigo."},          // 2
    {2, 1, "Jehová es mi Pastor; nada me faltará."},  // 3
    {2, 2, "El Señor dijo: amor, amor y más AMOR."},  // 4
    {2, 3, "Corazón paciente."},                      // 5
    {3, 1, "Pacientemente esperé."},                  // 6
    {3, 2, "El senor de la casa."},                   // 7
    {3, 3, "Dios es amor."},                          // 8
};
constexpr size_t CORPUS_SIZE = sizeof(CORPUS) / sizeof(CORPUS[0]);
constexpr const char* CONTINUATION = "y misericordia, amor";

void addCorpus(IndexBuilder& builder, const size_t from, const size_t to) {
  for (size_t i = from; i < to; i++) {
    const uint32_t n = builder.addVerse(43, CORPUS[i].chapter, CORPUS[i].verse, static_cast<uint16_t>(100 + i),
                                        static_cast<uint32_t>(i * 1000));
    ASSERT_EQ(n, i);
    ASSERT_TRUE(builder.addVerseText(n, CORPUS[i].text));
  }
}

Bytes corpusIndex() {
  IndexBuilder builder;
  addCorpus(builder, 0, CORPUS_SIZE);
  EXPECT_TRUE(builder.appendToLastVerse(CONTINUATION));
  return written(builder);
}

Verses query(const Bytes& bytes, const char* text, bool* truncated = nullptr) {
  IndexReader reader;
  EXPECT_EQ(reader.open(sourceOf(bytes), FINGERPRINT), IndexReader::Status::Ok);
  QueryResult result = runQuery(reader, text);
  if (truncated) *truncated = result.truncated;
  return result.verses;
}

}  // namespace

TEST(BibleSearchIndex, FindsASingleWord) {
  const Bytes bytes = corpusIndex();
  EXPECT_EQ(query(bytes, "amor"), (Verses{0, 1, 4, 8}));
  EXPECT_EQ(query(bytes, "celoso"), (Verses{1}));
}

TEST(BibleSearchIndex, RequiresEveryWord) {
  const Bytes bytes = corpusIndex();
  EXPECT_EQ(query(bytes, "amor paciente"), (Verses{0}));
  EXPECT_EQ(query(bytes, "paciente amor"), (Verses{0}));
  EXPECT_EQ(query(bytes, "jehova pastor"), (Verses{3}));
}

TEST(BibleSearchIndex, MatchesTheLastWordAsAPrefix) {
  const Bytes bytes = corpusIndex();
  EXPECT_EQ(query(bytes, "pacien"), (Verses{0, 2, 5, 6}));
  EXPECT_EQ(query(bytes, "el pacien"), (Verses{0}));
}

TEST(BibleSearchIndex, MatchesOnlyTheLastWordAsAPrefix) {
  const Bytes bytes = corpusIndex();
  EXPECT_TRUE(query(bytes, "pacien amor").empty()) << "a full word must match exactly";
}

TEST(BibleSearchIndex, MatchesWithAndWithoutAccents) {
  const Bytes bytes = corpusIndex();
  EXPECT_EQ(query(bytes, "senor"), (Verses{2, 4, 7}));
  EXPECT_EQ(query(bytes, "Señor"), (Verses{2, 4, 7}));
  EXPECT_EQ(query(bytes, "SEÑOR dijo"), (Verses{4}));
  EXPECT_EQ(query(bytes, "corazon"), (Verses{5}));
}

TEST(BibleSearchIndex, AnUnknownWordOrNoUsableTokenMatchesNothing) {
  const Bytes bytes = corpusIndex();
  EXPECT_TRUE(query(bytes, "zzz amor").empty());
  EXPECT_TRUE(query(bytes, "amor zzz").empty());
  EXPECT_TRUE(query(bytes, "y a").empty());
  EXPECT_TRUE(query(bytes, "").empty());
}

TEST(BibleSearchIndex, RecordsATermRepeatedInOneVerseOnce) {
  const Bytes bytes = corpusIndex();
  IndexReader reader;
  ASSERT_EQ(reader.open(sourceOf(bytes), FINGERPRINT), IndexReader::Status::Ok);
  TermEntry amor;
  ASSERT_TRUE(reader.findExact("amor", amor));
  EXPECT_EQ(amor.postingCount, 4) << "verse 4 says amor three times; the continuation repeats it in verse 8";
  Verses postings;
  ASSERT_TRUE(reader.postings(amor, postings, 100));
  EXPECT_EQ(postings, (Verses{0, 1, 4, 8}));
}

TEST(BibleSearchIndex, AppendsContinuationTextToTheLastVerse) {
  const Bytes bytes = corpusIndex();
  EXPECT_EQ(query(bytes, "misericordia"), (Verses{8}));
  EXPECT_EQ(query(bytes, "dios misericordia"), (Verses{8}));
}

TEST(BibleSearchIndex, RoundTripsTheVerseTable) {
  const Bytes bytes = corpusIndex();
  IndexReader reader;
  ASSERT_EQ(reader.open(sourceOf(bytes), FINGERPRINT), IndexReader::Status::Ok);
  ASSERT_EQ(reader.verseCount(), CORPUS_SIZE);
  VerseEntry v;
  ASSERT_TRUE(reader.verse(4, v));
  EXPECT_EQ(v.book, 43);
  EXPECT_EQ(v.chapter, 2);
  EXPECT_EQ(v.verse, 2);
  EXPECT_EQ(v.spine, 104);
  EXPECT_EQ(v.offset, 4000u);
  EXPECT_FALSE(reader.verse(static_cast<uint32_t>(CORPUS_SIZE), v));
}

TEST(BibleSearchIndex, SortsTermsInMemcmpOrder) {
  const Bytes bytes = corpusIndex();
  IndexReader reader;
  ASSERT_EQ(reader.open(sourceOf(bytes), FINGERPRINT), IndexReader::Status::Ok);
  std::string previous;
  for (uint32_t i = 0; i < reader.termCount(); i++) {
    TermEntry entry;
    ASSERT_TRUE(reader.term(i, entry));
    char text[MAX_TOKEN_BYTES + 1];
    size_t length = 0;
    ASSERT_TRUE(reader.termString(entry, text, length));
    const std::string current(text, length);
    EXPECT_LT(previous, current);
    previous = current;
  }
}

TEST(BibleSearchIndex, EstimatesExactlyTheBytesItWrites) {
  IndexBuilder builder;
  addCorpus(builder, 0, CORPUS_SIZE);
  EXPECT_EQ(builder.estimatedBytes(), written(builder).size());
}

TEST(BibleSearchIndex, CapsThePrefixUnion) {
  // 6,000 distinct terms sharing the prefix "zz", one per verse, and "comun"
  // only in the last 500 verses -- all past the lowest 5,000 the union keeps.
  IndexBuilder builder;
  for (uint32_t i = 0; i < 6000; i++) {
    const uint32_t n = builder.addVerse(1, 1, 1, 0, i);
    const std::string text = "zz" + std::to_string(i) + (i >= 5500 ? " comun" : "");
    ASSERT_TRUE(builder.addVerseText(n, text));
  }
  const Bytes bytes = written(builder);
  bool truncated = false;
  EXPECT_TRUE(query(bytes, "comun zz", &truncated).empty());
  EXPECT_TRUE(truncated) << "the prefix union was cut, so matches may be missing";
  const Verses narrow = query(bytes, "zz59", &truncated);
  EXPECT_FALSE(truncated);
  EXPECT_EQ(narrow.size(), 111u) << "zz59, zz590-zz599, zz5900-zz5999";
}

TEST(BibleSearchIndex, CapsTheResults) {
  IndexBuilder builder;
  for (uint32_t i = 0; i < 1500; i++) {
    const uint32_t n = builder.addVerse(1, 1, 1, 0, i);
    ASSERT_TRUE(builder.addVerseText(n, "comun"));
  }
  const Bytes bytes = written(builder);
  bool truncated = false;
  const Verses verses = query(bytes, "comun", &truncated);
  ASSERT_EQ(verses.size(), RESULT_CAP);
  EXPECT_TRUE(truncated);
  EXPECT_EQ(verses.front(), 0);
  EXPECT_EQ(verses.back(), RESULT_CAP - 1) << "the first verses in canonical order are kept";
}

TEST(BibleSearchIndex, ReportsMissingForAnEmptySource) {
  const Bytes empty;
  IndexReader reader;
  EXPECT_EQ(reader.open(sourceOf(empty), FINGERPRINT), IndexReader::Status::Missing);
}

TEST(BibleSearchIndex, RefusesANewerFormatVersion) {
  Bytes bytes = corpusIndex();
  const uint16_t newer = INDEX_FORMAT_VERSION + 1;
  memcpy(bytes.data() + 4, &newer, sizeof(newer));
  IndexReader reader;
  EXPECT_EQ(reader.open(sourceOf(bytes), FINGERPRINT), IndexReader::Status::TooNew);
}

TEST(BibleSearchIndex, ReportsAFingerprintMismatchAsStale) {
  const Bytes bytes = corpusIndex();
  IndexReader reader;
  EXPECT_EQ(reader.open(sourceOf(bytes), FINGERPRINT + 1), IndexReader::Status::Stale);
}

TEST(BibleSearchIndex, ReportsACheckpointAsIncomplete) {
  IndexBuilder builder;
  addCorpus(builder, 0, 4);
  Bytes bytes;
  ASSERT_TRUE(builder.writeCheckpoint(appendToBytes, &bytes, FINGERPRINT, 2));
  IndexReader reader;
  EXPECT_EQ(reader.open(sourceOf(bytes), FINGERPRINT), IndexReader::Status::Incomplete);
  EXPECT_EQ(reader.header().docsDone, 2u);
}

TEST(BibleSearchIndex, ReportsATruncatedOrForeignFileAsUnreadable) {
  const Bytes bytes = corpusIndex();
  IndexReader reader;
  const Bytes truncated(bytes.begin(), bytes.end() - 1);
  EXPECT_EQ(reader.open(sourceOf(truncated), FINGERPRINT), IndexReader::Status::Unreadable);
  const Bytes headerOnly(bytes.begin(), bytes.begin() + 10);
  EXPECT_EQ(reader.open(sourceOf(headerOnly), FINGERPRINT), IndexReader::Status::Unreadable);
  Bytes foreign = bytes;
  foreign[0] = 'X';
  EXPECT_EQ(reader.open(sourceOf(foreign), FINGERPRINT), IndexReader::Status::Unreadable);
  Bytes badOffsets = bytes;
  const uint32_t pastEnd = static_cast<uint32_t>(bytes.size()) + 1;
  memcpy(badOffsets.data() + 40, &pastEnd, sizeof(pastEnd));
  EXPECT_EQ(reader.open(sourceOf(badOffsets), FINGERPRINT), IndexReader::Status::Unreadable);
}

TEST(BibleSearchIndex, ResumingFromACheckpointWritesAByteIdenticalFile) {
  const Bytes uninterrupted = corpusIndex();

  Bytes checkpoint;
  {
    IndexBuilder first;
    addCorpus(first, 0, 5);
    ASSERT_TRUE(first.writeCheckpoint(appendToBytes, &checkpoint, FINGERPRINT, 2));
  }
  IndexBuilder resumed;
  uint32_t docsDone = 0;
  ASSERT_TRUE(resumed.loadCheckpoint(sourceOf(checkpoint), FINGERPRINT, docsDone));
  EXPECT_EQ(docsDone, 2u);
  EXPECT_EQ(resumed.verseCount(), 5u);
  addCorpus(resumed, 5, CORPUS_SIZE);
  ASSERT_TRUE(resumed.appendToLastVerse(CONTINUATION));

  EXPECT_EQ(written(resumed), uninterrupted);
}

TEST(BibleSearchIndex, ResumeDeduplicatesAgainstTheRestoredLastVerse) {
  Bytes checkpoint;
  {
    IndexBuilder first;
    addCorpus(first, 0, 5);  // verse 4 already holds "amor"
    ASSERT_TRUE(first.writeCheckpoint(appendToBytes, &checkpoint, FINGERPRINT, 1));
  }
  IndexBuilder resumed;
  uint32_t docsDone = 0;
  ASSERT_TRUE(resumed.loadCheckpoint(sourceOf(checkpoint), FINGERPRINT, docsDone));
  ASSERT_TRUE(resumed.appendToLastVerse("amor otra vez"));
  const Bytes bytes = written(resumed);
  IndexReader reader;
  ASSERT_EQ(reader.open(sourceOf(bytes), FINGERPRINT), IndexReader::Status::Ok);
  TermEntry amor;
  ASSERT_TRUE(reader.findExact("amor", amor));
  EXPECT_EQ(amor.postingCount, 3);
}

TEST(BibleSearchIndex, RefusesACheckpointForAnotherBibleOrACompleteFile) {
  IndexBuilder first;
  addCorpus(first, 0, 5);
  Bytes checkpoint;
  ASSERT_TRUE(first.writeCheckpoint(appendToBytes, &checkpoint, FINGERPRINT, 2));
  uint32_t docsDone = 0;
  IndexBuilder other;
  EXPECT_FALSE(other.loadCheckpoint(sourceOf(checkpoint), FINGERPRINT + 1, docsDone));
  IndexBuilder fromComplete;
  EXPECT_FALSE(fromComplete.loadCheckpoint(sourceOf(corpusIndex()), FINGERPRINT, docsDone));
}

TEST(BibleSearchIndex, RefusesTextForAVerseOtherThanTheNewest) {
  IndexBuilder builder;
  const uint32_t first = builder.addVerse(1, 1, 1, 0, 0);
  builder.addVerse(1, 1, 2, 0, 10);
  EXPECT_FALSE(builder.addVerseText(first, "tarde"));
  EXPECT_TRUE(builder.failed());
  Bytes bytes;
  EXPECT_FALSE(builder.write(appendToBytes, &bytes, FINGERPRINT)) << "a failed build must never be written";
}

TEST(BibleSearchIndex, RefusesMoreVersesThanAU16CanNumber) {
  IndexBuilder builder;
  for (uint32_t i = 0; i < MAX_VERSES; i++) ASSERT_EQ(builder.addVerse(1, 1, 1, 0, i), i);
  EXPECT_EQ(builder.addVerse(1, 1, 1, 0, 0), IndexBuilder::INVALID_VERSE);
  EXPECT_TRUE(builder.failed());
}

TEST(BibleSearchIndex, StopsWhenTheAllocatorRunsOut) {
  static int remaining = 0;
  const BuildAllocator scarce{[](size_t bytes) -> void* { return remaining-- > 0 ? std::malloc(bytes) : nullptr; },
                              [](void* block) { std::free(block); }};
  remaining = 3;
  IndexBuilder builder(scarce);
  bool ok = true;
  for (uint32_t i = 0; i < 20000 && ok; i++) {
    const uint32_t n = builder.addVerse(1, 1, 1, 0, i);
    ok = n != IndexBuilder::INVALID_VERSE && builder.addVerseText(n, "palabra" + std::to_string(i));
  }
  EXPECT_FALSE(ok);
  EXPECT_TRUE(builder.failed());
}

// The Task 2 fixtures, in canonical order, through the scanner and the builder
// exactly as the firmware build will drive them.
namespace {

struct FixtureDoc {
  const char* file;
  uint8_t book;
  uint16_t spine;
};

constexpr FixtureDoc FIXTURE_DOCS[] = {
    {"genesis1.xhtml", 1, 10},     {"salmo23.xhtml", 19, 500}, {"salmo111.xhtml", 19, 588}, {"salmo119.xhtml", 19, 596},
    {"malaquias4.xhtml", 39, 900}, {"juan2.xhtml", 43, 1100},  {"juan8.xhtml", 43, 1106},
};

std::string fixture(const char* name) {
  std::ifstream in(std::string(BIBLE_SEARCH_FIXTURE_DIR) + "/" + name, std::ios::binary);
  std::stringstream buffer;
  buffer << in.rdbuf();
  return buffer.str();
}

Bytes fixtureIndex() {
  IndexBuilder builder;
  for (const auto& doc : FIXTURE_DOCS) {
    const std::string xhtml = fixture(doc.file);
    VerseTextScanner scanner;
    EXPECT_TRUE(scanner.feed(xhtml.data(), xhtml.size(), true)) << doc.file;
    const std::string continuation = scanner.continuation();
    if (!continuation.empty()) EXPECT_TRUE(builder.appendToLastVerse(continuation));
    for (const auto& v : scanner.take()) {
      const uint32_t n = builder.addVerse(doc.book, static_cast<uint8_t>(v.chapter), static_cast<uint8_t>(v.verse),
                                          doc.spine, v.anchorOffset);
      EXPECT_TRUE(builder.addVerseText(n, v.text));
    }
  }
  return written(builder);
}

std::vector<std::string> references(const Bytes& bytes, const char* text) {
  IndexReader reader;
  EXPECT_EQ(reader.open(sourceOf(bytes), FINGERPRINT), IndexReader::Status::Ok);
  std::vector<std::string> out;
  for (const uint16_t n : runQuery(reader, text).verses) {
    VerseEntry v;
    EXPECT_TRUE(reader.verse(n, v));
    out.push_back(std::to_string(v.book) + " " + std::to_string(v.chapter) + ":" + std::to_string(v.verse));
  }
  return out;
}

}  // namespace

TEST(BibleSearchIndexRealMarkup, FindsPalomasInJohnTwo) {
  const Bytes bytes = fixtureIndex();
  EXPECT_EQ(references(bytes, "palomas"), (std::vector<std::string>{"43 2:14", "43 2:16"}));
}

TEST(BibleSearchIndexRealMarkup, FindsQuitenMercadoInJohnTwoSixteenOnly) {
  const Bytes bytes = fixtureIndex();
  EXPECT_EQ(references(bytes, "quiten mercado"), (std::vector<std::string>{"43 2:16"}));
}

TEST(BibleSearchIndexRealMarkup, FindsAcrossBooksInCanonicalOrder) {
  const Bytes bytes = fixtureIndex();
  EXPECT_EQ(references(bytes, "jehova pastor"), (std::vector<std::string>{"19 23:1"}));
  const auto luz = references(bytes, "luz");
  ASSERT_GE(luz.size(), 2u);
  EXPECT_EQ(luz.front(), "1 1:3") << "Genesis 1:3, 'Que haya luz', comes first";
}

TEST(BibleSearchIndexRealMarkup, NeverIndexesFootnotesOrVerseNumbers) {
  const Bytes bytes = fixtureIndex();
  EXPECT_TRUE(references(bytes, "centro comercio").empty()) << "the footnote on John 2:16";
  EXPECT_TRUE(references(bytes, "alef").empty()) << "the acrostic headings in Psalm 119";
}

TEST(BibleSearchIndexRealMarkup, StoresTheReadersOwnOffset) {
  const Bytes bytes = fixtureIndex();
  IndexReader reader;
  ASSERT_EQ(reader.open(sourceOf(bytes), FINGERPRINT), IndexReader::Status::Ok);
  const QueryResult result = runQuery(reader, "quiten mercado");
  ASSERT_EQ(result.verses.size(), 1u);
  VerseEntry v;
  ASSERT_TRUE(reader.verse(result.verses[0], v));
  const std::string juan2 = fixture("juan2.xhtml");
  const auto anchors = VerseAnchors::scan(juan2.data(), juan2.size());
  ASSERT_EQ(anchors.size(), 25u);
  EXPECT_EQ(v.offset, anchors[15].offset);
  EXPECT_EQ(v.spine, 1100);
}
