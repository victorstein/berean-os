#include <gtest/gtest.h>

#include <algorithm>
#include <cstdlib>
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

// 6,000 distinct terms sharing the prefix "zz", one per verse, and "comun" only
// in the last 500 verses -- all past verse #5000.
Bytes zzIndex() {
  IndexBuilder builder;
  for (uint32_t i = 0; i < 6000; i++) {
    const uint32_t n = builder.addVerse(1, 1, 1, 0, i);
    const std::string text = "zz" + std::to_string(i) + (i >= 5500 ? " comun" : "");
    EXPECT_TRUE(builder.addVerseText(n, text));
  }
  return written(builder);
}

TEST(BibleSearchIndex, KeepsPrefixMatchesPastTheFiveThousandthVerseWhenAWordNarrowsThem) {
  const Bytes bytes = zzIndex();
  bool truncated = true;
  const Verses verses = query(bytes, "comun zz", &truncated);
  ASSERT_EQ(verses.size(), 500u);
  EXPECT_EQ(verses.front(), 5500);
  EXPECT_EQ(verses.back(), 5999);
  EXPECT_FALSE(truncated);
}

TEST(BibleSearchIndex, CapsASinglePrefixWord) {
  const Bytes bytes = zzIndex();
  bool truncated = false;
  const Verses all = query(bytes, "zz", &truncated);
  ASSERT_EQ(all.size(), RESULT_CAP);
  EXPECT_EQ(all.front(), 0);
  EXPECT_EQ(all.back(), RESULT_CAP - 1);
  EXPECT_TRUE(truncated);
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

// Hostile and damaged files. Each case breaks exactly one invariant, so no
// other check can mask the one under test.
namespace {

void put16At(Bytes& bytes, const size_t at, const uint16_t v) { memcpy(bytes.data() + at, &v, sizeof(v)); }
void put32At(Bytes& bytes, const size_t at, const uint32_t v) { memcpy(bytes.data() + at, &v, sizeof(v)); }
uint32_t get32At(const Bytes& bytes, const size_t at) {
  uint32_t v = 0;
  memcpy(&v, bytes.data() + at, sizeof(v));
  return v;
}

constexpr size_t AT_VERSION = 4;
constexpr size_t AT_VERSE_COUNT = 16;
constexpr size_t AT_TERM_TABLE = 32;
constexpr size_t AT_TERM_STRINGS = 36;
constexpr size_t AT_POSTINGS = 40;
constexpr size_t AT_FILE_SIZE = 44;

// Where `term`'s postings start, in a corpus index.
size_t postingsAt(const Bytes& bytes, const char* term) {
  IndexReader reader;
  EXPECT_EQ(reader.open(sourceOf(bytes), FINGERPRINT), IndexReader::Status::Ok);
  TermEntry entry;
  EXPECT_TRUE(reader.findExact(term, entry));
  return reader.header().postingsOffset + entry.postingsOffset;
}

bool postingsOf(const Bytes& bytes, const char* term, Verses& out) {
  IndexReader reader;
  EXPECT_EQ(reader.open(sourceOf(bytes), FINGERPRINT), IndexReader::Status::Ok);
  TermEntry entry;
  EXPECT_TRUE(reader.findExact(term, entry));
  return reader.postings(entry, out, entry.postingCount);
}

}  // namespace

TEST(BibleSearchIndexHostile, RefusesAnOlderFormatVersionAsUnreadable) {
  Bytes bytes = corpusIndex();
  put16At(bytes, AT_VERSION, INDEX_FORMAT_VERSION - 1);
  IndexReader reader;
  EXPECT_EQ(reader.open(sourceOf(bytes), FINGERPRINT), IndexReader::Status::Unreadable);
}

TEST(BibleSearchIndexHostile, RefusesAHeaderThatDisagreesWithItself) {
  struct Case {
    const char* name;
    void (*corrupt)(Bytes&);
  };
  const Case cases[] = {
      {"verse count past MAX_VERSES, every offset consistent with it",
       [](Bytes& b) {
         const uint32_t verses = MAX_VERSES + 1;
         const uint32_t end = static_cast<uint32_t>(INDEX_HEADER_BYTES + verses * VERSE_ENTRY_BYTES);
         Bytes forged(end, 0);
         memcpy(forged.data(), b.data(), INDEX_HEADER_BYTES);
         put32At(forged, AT_VERSE_COUNT, verses);
         put32At(forged, 20, 0);  // termCount
         put32At(forged, AT_TERM_TABLE, end);
         put32At(forged, AT_TERM_STRINGS, end);
         put32At(forged, AT_POSTINGS, end);
         put32At(forged, AT_FILE_SIZE, end);
         b = forged;
       }},
      {"term table not where the verse table ends",
       [](Bytes& b) {
         put32At(b, AT_TERM_TABLE, get32At(b, AT_TERM_TABLE) + 1);
         put32At(b, AT_TERM_STRINGS, get32At(b, AT_TERM_STRINGS) + 1);
       }},
      {"term strings not where the term table ends",
       [](Bytes& b) { put32At(b, AT_TERM_STRINGS, get32At(b, AT_TERM_STRINGS) + 1); }},
      {"postings before the term strings", [](Bytes& b) { put32At(b, AT_POSTINGS, get32At(b, AT_TERM_STRINGS) - 1); }},
      {"recorded size larger than the file",
       [](Bytes& b) {
         put32At(b, AT_FILE_SIZE, get32At(b, AT_FILE_SIZE) + 1);
         put32At(b, AT_POSTINGS, get32At(b, AT_FILE_SIZE));
       }},
  };
  for (const Case& c : cases) {
    Bytes bytes = corpusIndex();
    c.corrupt(bytes);
    IndexReader reader;
    EXPECT_EQ(reader.open(sourceOf(bytes), FINGERPRINT), IndexReader::Status::Unreadable) << c.name;
  }
}

TEST(BibleSearchIndexHostile, RefusesAPostingPastTheLastVerse) {
  Bytes bytes = corpusIndex();
  bytes[postingsAt(bytes, "celoso")] = 0x7F;  // verse 127 of 9
  Verses out;
  EXPECT_FALSE(postingsOf(bytes, "celoso", out));
}

TEST(BibleSearchIndexHostile, RefusesARepeatedVerseInOnePostingList) {
  Bytes bytes = corpusIndex();
  // amor: 0, 1, 4, 8 is deltas 0, 1, 3, 4. A zero is legal only first.
  bytes[postingsAt(bytes, "amor") + 1] = 0x00;
  Verses out;
  EXPECT_FALSE(postingsOf(bytes, "amor", out));
}

TEST(BibleSearchIndexHostile, RefusesAVarintLongerThanThreeBytes) {
  // celoso's one posting, verse 1, re-encoded in four bytes: a value that
  // would be valid if the length were not checked.
  Bytes bytes = corpusIndex();
  const size_t at = postingsAt(bytes, "celoso");
  bytes[at] = 0x81;
  bytes[at + 1] = 0x80;
  bytes[at + 2] = 0x80;
  bytes[at + 3] = 0x00;
  Verses out;
  EXPECT_FALSE(postingsOf(bytes, "celoso", out));
}

TEST(BibleSearchIndexHostile, RefusesATermStringWithNoTerminator) {
  // A 32-byte term, the longest a token can be: its NUL is the 33rd byte, the
  // last one the reader may look at.
  IndexBuilder builder;
  const std::string longest(MAX_TOKEN_BYTES, 'z');
  builder.addVerse(1, 1, 1, 0, 0);
  ASSERT_TRUE(builder.addVerseText(0, "amor " + longest));
  Bytes bytes = written(builder);
  IndexReader reader;
  ASSERT_EQ(reader.open(sourceOf(bytes), FINGERPRINT), IndexReader::Status::Ok);
  TermEntry entry;
  ASSERT_TRUE(reader.findExact(longest, entry));
  bytes[reader.header().termStringsOffset + entry.stringOffset + MAX_TOKEN_BYTES] = 'z';
  ASSERT_EQ(reader.open(sourceOf(bytes), FINGERPRINT), IndexReader::Status::Ok);
  char text[MAX_TOKEN_BYTES + 1];
  size_t length = 0;
  EXPECT_FALSE(reader.termString(entry, text, length));
}

TEST(BibleSearchIndex, DecodesVarintsAtTheirByteBoundaries) {
  // Deltas of 127 and 128 straddle the one-byte boundary; 16,383 and 16,384
  // the two-byte one.
  IndexBuilder builder;
  const uint32_t marked[] = {0, 127, 255, 16638, 33022};
  size_t next = 0;
  for (uint32_t i = 0; i <= 33022; i++) {
    const uint32_t n = builder.addVerse(1, 1, 1, 0, i);
    const bool isMarked = next < std::size(marked) && marked[next] == i;
    if (isMarked) next++;
    ASSERT_TRUE(builder.addVerseText(n, isMarked ? "marca" : "otro"));
  }
  const Bytes bytes = written(builder);
  Verses out;
  ASSERT_TRUE(postingsOf(bytes, "marca", out));
  EXPECT_EQ(out, (Verses{0, 127, 255, 16638, 33022}));
}

TEST(BibleSearchIndex, FindsTheLastTermInTheTableByPrefix) {
  const Bytes bytes = corpusIndex();
  IndexReader reader;
  ASSERT_EQ(reader.open(sourceOf(bytes), FINGERPRINT), IndexReader::Status::Ok);
  uint32_t first = 0;
  uint32_t last = 0;
  ASSERT_TRUE(reader.findPrefixRange("y", first, last));
  ASSERT_TRUE(reader.findPrefixRange("te", first, last));
  EXPECT_EQ(last, reader.termCount()) << "'ten' sorts last in the corpus";
  EXPECT_EQ(last - first, 1u);
}

TEST(BibleSearchIndex, BuildsPastHashGrowthAndChunkBoundaries) {
  // 24,000 distinct terms pass the first hash table's 70% load (22,937 of
  // 32,768 slots), and ~190 KB of strings cross three 64 KB string chunks.
  // The last 1,000 verses repeat early terms after the rehash, which must find
  // them rather than insert duplicates.
  const auto word = [](uint32_t i) {
    std::string w = "w";
    for (int k = 0; k < 4; k++, i /= 26) w.push_back(static_cast<char>('a' + i % 26));
    return w + "xyz";
  };
  constexpr uint32_t TERMS = 24000;
  constexpr uint32_t REPEATS = 1000;
  IndexBuilder builder;
  for (uint32_t i = 0; i < TERMS + REPEATS; i++) {
    const uint32_t n = builder.addVerse(1, 1, 1, 0, i);
    ASSERT_TRUE(builder.addVerseText(n, word(i < TERMS ? i : i - TERMS)));
  }
  EXPECT_EQ(builder.termCount(), TERMS);
  const Bytes bytes = written(builder);
  IndexReader reader;
  ASSERT_EQ(reader.open(sourceOf(bytes), FINGERPRINT), IndexReader::Status::Ok);
  ASSERT_GT(reader.header().postingsOffset - reader.header().termStringsOffset, 3u * 64 * 1024);
  for (uint32_t i = 0; i < TERMS; i++) {
    Verses out;
    TermEntry entry;
    ASSERT_TRUE(reader.findExact(word(i), entry)) << word(i);
    ASSERT_TRUE(reader.postings(entry, out, entry.postingCount));
    const Verses expected = i < REPEATS ? Verses{static_cast<uint16_t>(i), static_cast<uint16_t>(TERMS + i)}
                                        : Verses{static_cast<uint16_t>(i)};
    ASSERT_EQ(out, expected) << word(i);
  }
}

TEST(BibleSearchIndex, AFailedStringAllocationRecordsNoTerm) {
  // The strings pool asks for 64 KB chunks; refusing exactly that size fails
  // the term's string while its record could still be allocated.
  const BuildAllocator noStrings{
      [](size_t bytes) -> void* { return bytes == 64 * 1024 ? nullptr : std::malloc(bytes); },
      [](void* block) { std::free(block); }};
  IndexBuilder builder(noStrings);
  builder.addVerse(1, 1, 1, 0, 0);
  EXPECT_FALSE(builder.addVerseText(0, "amor"));
  EXPECT_TRUE(builder.failed());
  EXPECT_EQ(builder.termCount(), 0u);
}

TEST(BibleSearchIndex, AFailedCheckpointReadMarksTheBuilderFailed) {
  IndexBuilder first;
  addCorpus(first, 0, 5);
  Bytes checkpoint;
  ASSERT_TRUE(first.writeCheckpoint(appendToBytes, &checkpoint, FINGERPRINT, 2));
  ByteSource headerOnly = sourceOf(checkpoint);
  headerOnly.readAt = [](void* ctx, const uint32_t offset, void* dst, const uint32_t len) {
    return offset < INDEX_HEADER_BYTES && readFromBytes(ctx, offset, dst, len);
  };
  IndexBuilder resumed;
  uint32_t docsDone = 0;
  EXPECT_FALSE(resumed.loadCheckpoint(headerOnly, FINGERPRINT, docsDone));
  EXPECT_TRUE(resumed.failed());
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

// Read cost. A counting source stands in for the SD card, where every readAt is
// a HalFile seek and read under storageMutex.
namespace {

struct CountingSource {
  const Bytes* bytes;
  size_t reads = 0;
  bool failAfterOpen = false;
};

ByteSource countingSourceOf(CountingSource& counter) {
  ByteSource s;
  s.ctx = &counter;
  s.readAt = [](void* ctx, const uint32_t offset, void* dst, const uint32_t len) {
    auto* c = static_cast<CountingSource*>(ctx);
    if (c->failAfterOpen && offset >= INDEX_HEADER_BYTES) return false;
    c->reads++;
    return readFromBytes(const_cast<Bytes*>(c->bytes), offset, dst, len);
  };
  s.size = static_cast<uint32_t>(counter.bytes->size());
  return s;
}

// 20,000 verses, each with its own "pa..." term and one shared word: a prefix
// range of 20,000 terms whose postings span ~40 KB.
Bytes wideIndex() {
  IndexBuilder builder;
  for (uint32_t i = 0; i < 20000; i++) {
    const uint32_t n = builder.addVerse(1, 1, 1, 0, i);
    std::string text = "pa";
    for (uint32_t k = 0, v = i; k < 4; k++, v /= 26) text.push_back(static_cast<char>('a' + v % 26));
    EXPECT_TRUE(builder.addVerseText(n, text + (i % 2 == 0 ? " par" : "")));
  }
  return written(builder);
}

}  // namespace

TEST(BibleSearchIndexReads, CachedTermIndexServesLookupsWithoutReading) {
  const Bytes bytes = wideIndex();
  CountingSource counter{&bytes};
  IndexReader reader;
  ASSERT_EQ(reader.open(countingSourceOf(counter), FINGERPRINT), IndexReader::Status::Ok);
  counter.reads = 0;
  ASSERT_TRUE(reader.cacheTermIndex());
  EXPECT_EQ(counter.reads, 1u) << "the term table and strings load in one sequential read";
  EXPECT_TRUE(reader.cacheTermIndex()) << "a second call keeps the cache";
  EXPECT_EQ(counter.reads, 1u);
  counter.reads = 0;
  TermEntry entry;
  ASSERT_TRUE(reader.findExact("par", entry));
  uint32_t first = 0;
  uint32_t last = 0;
  ASSERT_TRUE(reader.findPrefixRange("pa", first, last));
  EXPECT_EQ(counter.reads, 0u);
  EXPECT_EQ(last - first, 20001u);
}

TEST(BibleSearchIndexReads, APrefixRangeCostsOneReadPerPageOfItsSpan) {
  const Bytes bytes = wideIndex();
  CountingSource counter{&bytes};
  IndexReader reader;
  ASSERT_EQ(reader.open(countingSourceOf(counter), FINGERPRINT), IndexReader::Status::Ok);
  ASSERT_TRUE(reader.cacheTermIndex());
  const uint32_t span = reader.header().fileSize - reader.header().postingsOffset;
  counter.reads = 0;
  const QueryResult result = runQuery(reader, "pa");
  ASSERT_TRUE(result.ok);
  EXPECT_EQ(result.verses.size(), RESULT_CAP);
  EXPECT_LE(counter.reads, span / IndexReader::PAGE_BYTES + 2) << "span " << span << " B over 20,001 terms";
  counter.reads = 0;
  const QueryResult narrowed = runQuery(reader, "par pa");
  ASSERT_TRUE(narrowed.ok);
  EXPECT_EQ(narrowed.verses.size(), RESULT_CAP);
  EXPECT_TRUE(narrowed.truncated) << "10,000 verses match";
  EXPECT_LE(counter.reads, span / IndexReader::PAGE_BYTES + 4);
}

TEST(BibleSearchIndexReads, WithoutTheCacheEveryAnswerIsTheSame) {
  const Bytes bytes = wideIndex();
  CountingSource counter{&bytes};
  IndexReader cached;
  IndexReader uncached;
  ASSERT_EQ(cached.open(sourceOf(bytes), FINGERPRINT), IndexReader::Status::Ok);
  ASSERT_TRUE(cached.cacheTermIndex());
  ASSERT_EQ(uncached.open(countingSourceOf(counter), FINGERPRINT), IndexReader::Status::Ok);
  for (const char* q : {"pa", "par pa", "paab", "par paqq", "zz"}) {
    const QueryResult a = runQuery(cached, q);
    const QueryResult b = runQuery(uncached, q);
    EXPECT_EQ(a.verses, b.verses) << q;
    EXPECT_EQ(a.truncated, b.truncated) << q;
  }
}

TEST(BibleSearchIndexReads, AnUnallocatableCacheFallsBackToTheSource) {
  const Bytes bytes = wideIndex();
  const BuildAllocator smallOnly{[](size_t n) -> void* { return n > 64 * 1024 ? nullptr : std::malloc(n); },
                                 [](void* block) { std::free(block); }};
  IndexReader reader(smallOnly);
  ASSERT_EQ(reader.open(sourceOf(bytes), FINGERPRINT), IndexReader::Status::Ok);
  EXPECT_FALSE(reader.cacheTermIndex());
  EXPECT_FALSE(reader.termIndexCached());
  EXPECT_EQ(runQuery(reader, "paabaa").verses, (Verses{26}));
}

TEST(BibleSearchIndexReads, AnUnallocatablePageStillReads) {
  const Bytes bytes = corpusIndex();
  const BuildAllocator nothing{[](size_t) -> void* { return nullptr; }, [](void*) {}};
  IndexReader reader(nothing);
  ASSERT_EQ(reader.open(sourceOf(bytes), FINGERPRINT), IndexReader::Status::Ok);
  const QueryResult result = runQuery(reader, "amor");
  EXPECT_FALSE(result.ok) << "the query's own working memory could not be allocated";
  Verses out;
  TermEntry entry;
  ASSERT_TRUE(reader.findExact("amor", entry));
  ASSERT_TRUE(reader.postings(entry, out, entry.postingCount));
  EXPECT_EQ(out, (Verses{0, 1, 4, 8}));
}

TEST(BibleSearchIndexQuery, ReportsAnIoFailureApartFromNoResults) {
  const Bytes bytes = corpusIndex();
  CountingSource counter{&bytes};
  IndexReader reader;
  ASSERT_EQ(reader.open(countingSourceOf(counter), FINGERPRINT), IndexReader::Status::Ok);
  const QueryResult none = runQuery(reader, "zzz");
  EXPECT_TRUE(none.ok);
  EXPECT_TRUE(none.verses.empty());
  counter.failAfterOpen = true;
  for (const char* q : {"amor", "el amor", "el amor paciente"}) {
    const QueryResult failed = runQuery(reader, q);
    EXPECT_FALSE(failed.ok) << q;
    EXPECT_TRUE(failed.verses.empty()) << q;
  }
}

TEST(BibleSearchIndexQuery, IntersectsEveryFullWord) {
  IndexBuilder builder;
  const char* texts[] = {"gama", "beta gama", "alfa gama", "alfa beta gama"};
  for (uint32_t i = 0; i < 4; i++) ASSERT_TRUE(builder.addVerseText(builder.addVerse(1, 1, 1, 0, i), texts[i]));
  const Bytes bytes = written(builder);
  EXPECT_EQ(query(bytes, "alfa beta ga"), (Verses{3}));
  EXPECT_EQ(query(bytes, "beta alfa ga"), (Verses{3}));
  EXPECT_EQ(query(bytes, "alfa ga"), (Verses{2, 3}));

  IndexBuilder disjoint;
  ASSERT_TRUE(disjoint.addVerseText(disjoint.addVerse(1, 1, 1, 0, 0), "beta gama"));
  ASSERT_TRUE(disjoint.addVerseText(disjoint.addVerse(1, 1, 1, 0, 1), "alfa gama"));
  EXPECT_TRUE(query(written(disjoint), "alfa beta ga").empty());
}

TEST(BibleSearchIndexQuery, SizesItsWorkingMemoryByTheRarestWord) {
  // "comun" is in 20,000 verses (40 KB of candidates), "rara" in two. An
  // allocator that refuses anything over 16 KB still answers, because the
  // candidate buffer is sized by the rarest full word.
  IndexBuilder builder;
  for (uint32_t i = 0; i < 20000; i++) {
    const uint32_t n = builder.addVerse(1, 1, 1, 0, i);
    ASSERT_TRUE(builder.addVerseText(n, (i == 7 || i == 19000) ? "comun rara fin" : "comun fin"));
  }
  const Bytes bytes = written(builder);
  const BuildAllocator small{[](size_t n) -> void* { return n > 16 * 1024 ? nullptr : std::malloc(n); },
                             [](void* block) { std::free(block); }};
  IndexReader reader(small);
  ASSERT_EQ(reader.open(sourceOf(bytes), FINGERPRINT), IndexReader::Status::Ok);
  for (const char* q : {"comun rara fi", "rara comun fi"}) {
    const QueryResult result = runQuery(reader, q);
    ASSERT_TRUE(result.ok) << q;
    EXPECT_EQ(result.verses, (Verses{7, 19000})) << q;
  }
}

TEST(BibleSearchIndexQuery, ReportsAMalformedPostingListAsNotOk) {
  Bytes bytes = corpusIndex();
  bytes[postingsAt(bytes, "celoso")] = 0x7F;
  IndexReader reader;
  ASSERT_EQ(reader.open(sourceOf(bytes), FINGERPRINT), IndexReader::Status::Ok);
  EXPECT_FALSE(runQuery(reader, "celoso").ok);
  EXPECT_FALSE(runQuery(reader, "celoso amor").ok);
}

TEST(BibleSearchIndex, BatchesWritesThroughAnAllocatedBuffer) {
  IndexBuilder builder;
  for (uint32_t i = 0; i < 2000; i++) ASSERT_TRUE(builder.addVerseText(builder.addVerse(1, 1, 1, 0, i), "palabra"));
  struct Counted {
    Bytes bytes;
    size_t calls = 0;
  } counted;
  ASSERT_TRUE(builder.write(
      [](void* ctx, const void* data, const size_t length) {
        auto* c = static_cast<Counted*>(ctx);
        c->calls++;
        return appendToBytes(&c->bytes, data, length);
      },
      &counted, FINGERPRINT));
  EXPECT_EQ(counted.bytes, written(builder));
  EXPECT_LE(counted.calls, counted.bytes.size() / IndexBuilder::SINK_BUFFER_BYTES + 1);
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
  EXPECT_TRUE(references(bytes, "expresion idiomatica").empty()) << "the footnote kept in the John 2 excerpt";
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
  ASSERT_EQ(anchors.size(), 7u);
  ASSERT_EQ(anchors[5].verse, 16);
  EXPECT_EQ(v.offset, anchors[5].offset);
  EXPECT_EQ(v.spine, 1100);
}

namespace {

struct PostingReads {
  const Bytes* bytes;
  uint32_t postingsOffset = UINT32_MAX;
  uint32_t longestPostingRead = 0;
};

ByteSource postingReadsSourceOf(PostingReads& reads) {
  ByteSource s;
  s.ctx = &reads;
  s.readAt = [](void* ctx, const uint32_t offset, void* dst, const uint32_t len) {
    auto* r = static_cast<PostingReads*>(ctx);
    if (offset >= r->postingsOffset) r->longestPostingRead = std::max(r->longestPostingRead, len);
    return readFromBytes(const_cast<Bytes*>(r->bytes), offset, dst, len);
  };
  s.size = static_cast<uint32_t>(reads.bytes->size());
  return s;
}

Bytes twoVerseIndex(const char* first, const char* second) {
  IndexBuilder builder;
  EXPECT_TRUE(builder.addVerseText(builder.addVerse(1, 1, 1, 0, 0), first));
  EXPECT_TRUE(builder.addVerseText(builder.addVerse(1, 1, 2, 0, 10), second));
  return written(builder);
}

}  // namespace

TEST(BibleSearchIndexReads, AReaderOnTheFallbackPageRefillsInItsOwnSteps) {
  const Bytes bytes = wideIndex();
  const BuildAllocator noPage{[](size_t n) -> void* { return n == IndexReader::PAGE_BYTES ? nullptr : std::malloc(n); },
                              [](void* block) { std::free(block); }};
  IndexReader paged;
  ASSERT_EQ(paged.open(sourceOf(bytes), FINGERPRINT), IndexReader::Status::Ok);
  ASSERT_TRUE(paged.cacheTermIndex());

  PostingReads reads{&bytes};
  IndexReader fallback(noPage);
  ASSERT_EQ(fallback.open(postingReadsSourceOf(reads), FINGERPRINT), IndexReader::Status::Ok);
  ASSERT_TRUE(fallback.cacheTermIndex());
  reads.postingsOffset = fallback.header().postingsOffset;

  for (const char* q : {"pa", "par pa", "par", "paab"}) {
    const QueryResult a = runQuery(paged, q);
    const QueryResult b = runQuery(fallback, q);
    ASSERT_TRUE(b.ok) << q;
    EXPECT_EQ(a.verses, b.verses) << q;
    EXPECT_EQ(a.truncated, b.truncated) << q;
  }
  EXPECT_GT(reads.longestPostingRead, 0u);
  EXPECT_LE(reads.longestPostingRead, 64u) << "the fallback page holds 64 bytes, not PAGE_BYTES";
}

TEST(BibleSearchIndexReads, ReopeningOnAnotherSourceDropsTheOldPage) {
  const Bytes first = twoVerseIndex("amor", "fe");
  const Bytes second = twoVerseIndex("fe", "amor");
  ASSERT_EQ(first.size(), second.size());

  IndexReader reader;
  TermEntry entry;
  Verses out;
  ASSERT_EQ(reader.open(sourceOf(first), FINGERPRINT), IndexReader::Status::Ok);
  ASSERT_TRUE(reader.findExact("amor", entry));
  ASSERT_TRUE(reader.postings(entry, out, entry.postingCount));
  ASSERT_EQ(out, (Verses{0}));

  ASSERT_EQ(reader.open(sourceOf(second), FINGERPRINT), IndexReader::Status::Ok);
  out.clear();
  ASSERT_TRUE(reader.findExact("amor", entry));
  ASSERT_TRUE(reader.postings(entry, out, entry.postingCount));
  EXPECT_EQ(out, (Verses{1})) << "postings must come from the new source, not the previous page";
}

TEST(BibleSearchIndex, ReportsEachVerseAsRecorded) {
  IndexBuilder builder;
  addCorpus(builder, 0, CORPUS_SIZE);
  VerseEntry entry;
  ASSERT_TRUE(builder.verseAt(4, entry));
  EXPECT_EQ(entry.book, 43);
  EXPECT_EQ(entry.chapter, 2);
  EXPECT_EQ(entry.verse, 2);
  EXPECT_EQ(entry.spine, 104);
  EXPECT_EQ(entry.offset, 4000u);
  EXPECT_FALSE(builder.verseAt(CORPUS_SIZE, entry));
}
