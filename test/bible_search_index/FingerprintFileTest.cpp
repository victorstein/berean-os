// The fingerprint record's codec: its layout, the order its checks run in, and
// the hash it shares with every bible.idx already on a card.

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <vector>

#include "FingerprintFixtures.h"

namespace {

namespace bsfp = BibleSearchFingerprintFile;
using bsfp::Load;
using fingerprint_fixtures::encoded;
using fingerprint_fixtures::key;
using fingerprint_fixtures::record;
using fingerprint_fixtures::reseal;
using fingerprint_fixtures::setVersion;

Load decodeAs(const std::vector<uint8_t>& bytes, const size_t length) {
  bsfp::Record out;
  return bsfp::decode(bytes.data(), length, key(), out);
}

Load decodeWithKey(const bsfp::Key& expected) {
  const std::vector<uint8_t> bytes = encoded();
  bsfp::Record out;
  return bsfp::decode(bytes.data(), bytes.size(), expected, out);
}

std::vector<uint8_t> everyBookTwice() {
  std::vector<uint8_t> books;
  for (int book = 1; book <= 66; book++) {
    books.push_back(static_cast<uint8_t>(book));
    books.push_back(static_cast<uint8_t>(book));
  }
  return books;
}

bool covers(const std::vector<uint8_t>& books) {
  return bsfp::coversEveryBook(books.data(), static_cast<uint32_t>(books.size()));
}

}  // namespace

TEST(BibleSearchFingerprintFile, RoundTrips) {
  const std::vector<uint8_t> bytes = encoded();
  bsfp::Record out;
  ASSERT_EQ(bsfp::decode(bytes.data(), bytes.size(), key(), out), Load::Ok);
  EXPECT_EQ(out.key.epubSize, record().key.epubSize);
  EXPECT_EQ(out.key.spineCount, record().key.spineCount);
  EXPECT_EQ(out.key.unitIndexVersion, record().key.unitIndexVersion);
  EXPECT_EQ(out.verseDocuments, record().verseDocuments);
  EXPECT_EQ(out.fingerprint, record().fingerprint);
}

TEST(BibleSearchFingerprintFile, MatchesTheVersionOneLayout) {
  const std::vector<uint8_t> golden = {
      0x42, 0x53, 0x46, 0x50,                          // magic "BSFP"
      0x01, 0x00,                                      // version 1
      0x01, 0x00,                                      // unitIndexVersion 1
      0x7B, 0x51, 0x25, 0x02, 0x00, 0x00, 0x00, 0x00,  // epubSize 36,000,123
      0x61, 0x0F, 0x00, 0x00,                          // spineCount 3,937
      0xA5, 0x04, 0x00, 0x00,                          // verseDocuments 1,189
      0x2D, 0x4F, 0x55, 0x59, 0xDC, 0xE8, 0x63, 0xB0,  // fingerprint
      0xD7, 0x52, 0x5D, 0x2E, 0x77, 0x73, 0x27, 0x0A,  // check: FNV-1a 64 over bytes 0-31
  };
  EXPECT_EQ(encoded(), golden);
}

TEST(BibleSearchFingerprintFile, ANewerVersionIsTooNewAtAnyLength) {
  std::vector<uint8_t> bytes = encoded();
  setVersion(bytes, static_cast<uint16_t>(bsfp::VERSION + 1));  // the check no longer matches either
  EXPECT_EQ(decodeAs(bytes, 6), Load::TooNew);
  EXPECT_EQ(decodeAs(bytes, 40), Load::TooNew);
  EXPECT_EQ(decodeAs(bytes, 48), Load::TooNew);
}

TEST(BibleSearchFingerprintFile, AForeignFileIsCorruptEvenWithAHighVersion) {
  std::vector<uint8_t> bytes = encoded();
  bytes[0] = 'X';
  setVersion(bytes, 0xFFFF);
  EXPECT_EQ(decodeAs(bytes, 40), Load::Corrupt);
}

TEST(BibleSearchFingerprintFile, TooShortToHoldAVersionIsCorrupt) {
  const std::vector<uint8_t> bytes = encoded();
  EXPECT_EQ(decodeAs(bytes, 0), Load::Corrupt);
  EXPECT_EQ(decodeAs(bytes, 5), Load::Corrupt);
}

TEST(BibleSearchFingerprintFile, TheCurrentVersionAtAnyOtherLengthIsCorrupt) {
  const std::vector<uint8_t> bytes = encoded();
  EXPECT_EQ(decodeAs(bytes, 39), Load::Corrupt);
  EXPECT_EQ(decodeAs(bytes, 41), Load::Corrupt);
}

TEST(BibleSearchFingerprintFile, AFlippedCheckOrPayloadByteIsCorrupt) {
  std::vector<uint8_t> checkFlipped = encoded();
  checkFlipped[32] ^= 0x01;
  EXPECT_EQ(decodeAs(checkFlipped, 40), Load::Corrupt);

  std::vector<uint8_t> payloadFlipped = encoded();
  payloadFlipped[24] ^= 0x01;
  EXPECT_EQ(decodeAs(payloadFlipped, 40), Load::Corrupt);
}

TEST(BibleSearchFingerprintFile, AnOlderVersionIsCorruptEvenWhenSealed) {
  std::vector<uint8_t> bytes = encoded();
  setVersion(bytes, static_cast<uint16_t>(bsfp::VERSION - 1));
  reseal(bytes);
  EXPECT_EQ(decodeAs(bytes, 40), Load::Corrupt);
}

TEST(BibleSearchFingerprintFile, AnotherBibleOrUnitsFormatIsStale) {
  bsfp::Key otherSize = key();
  otherSize.epubSize += 1;
  EXPECT_EQ(decodeWithKey(otherSize), Load::Stale);

  bsfp::Key otherSpine = key();
  otherSpine.spineCount += 1;
  EXPECT_EQ(decodeWithKey(otherSpine), Load::Stale);

  bsfp::Key otherUnits = key();
  otherUnits.unitIndexVersion += 1;
  EXPECT_EQ(decodeWithKey(otherUnits), Load::Stale);
}

TEST(BibleSearchFingerprintFile, OnlyANewerOrUnreadableRecordIsLeftInPlace) {
  EXPECT_TRUE(bsfp::mayOverwrite(Load::Ok));
  EXPECT_TRUE(bsfp::mayOverwrite(Load::Missing));
  EXPECT_TRUE(bsfp::mayOverwrite(Load::Corrupt));
  EXPECT_TRUE(bsfp::mayOverwrite(Load::Stale));
  EXPECT_FALSE(bsfp::mayOverwrite(Load::TooNew));
  EXPECT_FALSE(bsfp::mayOverwrite(Load::Unreadable));
}

TEST(BibleSearchFingerprintFile, TheHashIsFnv1a64) {
  bsfp::Fnv1a64 empty;
  EXPECT_EQ(empty.hash, 0xcbf29ce484222325ull);

  bsfp::Fnv1a64 a;
  a.add("a", 1);
  EXPECT_EQ(a.hash, 0xaf63dc4c8601ec8cull);
}

TEST(BibleSearchFingerprintFile, TheIntegerHelpersHashLittleEndianBytes) {
  bsfp::Fnv1a64 viaHelpers;
  viaHelpers.addU32(0x11223344u);
  viaHelpers.addU64(0x8877665544332211ull);

  bsfp::Fnv1a64 viaBytes;
  const uint8_t bytes[] = {0x44, 0x33, 0x22, 0x11, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88};
  viaBytes.add(bytes, sizeof(bytes));

  EXPECT_EQ(viaHelpers.hash, viaBytes.hash);
}

TEST(BibleSearchFingerprintFile, CoversEveryBookOnlyWhenAllSixtySixResolved) {
  std::vector<uint8_t> books = everyBookTwice();
  EXPECT_TRUE(covers(books));

  std::reverse(books.begin(), books.end());
  EXPECT_TRUE(covers(books));

  books.push_back(0);
  books.push_back(67);
  EXPECT_TRUE(covers(books)) << "values outside 1-66 are ignored";

  books.erase(std::remove(books.begin(), books.end(), uint8_t{40}), books.end());
  EXPECT_FALSE(covers(books)) << "book 40 missing";

  EXPECT_FALSE(bsfp::coversEveryBook(nullptr, 0));
}
