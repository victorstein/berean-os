// When the search store keeps its reader after the screen is left, and when a
// kept reader may serve again.

#include <BibleSearch/IndexBuilder.h>
#include <BibleSearch/IndexFormat.h>
#include <gtest/gtest.h>

#include <cstdint>
#include <optional>
#include <vector>

#include "src/study/BibleSearchResidency.h"

namespace {

using BibleSearch::IndexBuilder;
using BibleSearch::IndexHeader;
using Bytes = std::vector<uint8_t>;

constexpr uint64_t FINGERPRINT = 0x0123456789ABCDEFull;
constexpr size_t MIB = 1024u * 1024u;

bool appendBytes(void* ctx, const void* data, const size_t length) {
  auto* out = static_cast<Bytes*>(ctx);
  const auto* p = static_cast<const uint8_t*>(data);
  out->insert(out->end(), p, p + length);
  return true;
}

Bytes builtIndex() {
  IndexBuilder builder;
  EXPECT_TRUE(builder.addVerseText(builder.addVerse(1, 1, 1, 0, 0), "en el principio"));
  EXPECT_TRUE(builder.addVerseText(builder.addVerse(1, 1, 2, 0, 10), "la tierra estaba"));
  Bytes out;
  EXPECT_TRUE(builder.write(appendBytes, &out, FINGERPRINT));
  return out;
}

IndexHeader heldHeaderOf(const Bytes& bytes) {
  const std::optional<IndexHeader> header = BibleSearch::readHeader(bytes.data(), bytes.size());
  EXPECT_TRUE(header.has_value());
  return header.value_or(IndexHeader{});
}

uint32_t sizeOf(const Bytes& bytes) { return static_cast<uint32_t>(bytes.size()); }

}  // namespace

TEST(BibleSearchResidency, TheFloorsAreThreeMebibytesFreeAndAOneMebibyteBlock) {
  EXPECT_EQ(BibleSearchResidency::MIN_FREE_PSRAM, 3 * MIB);
  EXPECT_EQ(BibleSearchResidency::MIN_LARGEST_PSRAM_BLOCK, 1 * MIB);
}

TEST(BibleSearchResidency, KeepsAtAndAboveBothFloors) {
  EXPECT_TRUE(BibleSearchResidency::keepAfterExit(3 * MIB, 1 * MIB));
  EXPECT_TRUE(BibleSearchResidency::keepAfterExit(8 * MIB, 4 * MIB));
}

TEST(BibleSearchResidency, ReleasesBelowEitherFloor) {
  EXPECT_FALSE(BibleSearchResidency::keepAfterExit(3 * MIB - 1, 4 * MIB));
  EXPECT_FALSE(BibleSearchResidency::keepAfterExit(8 * MIB, 1 * MIB - 1));
  EXPECT_FALSE(BibleSearchResidency::keepAfterExit(0, 0));
}

TEST(BibleSearchResidency, AnUnchangedIndexMatchesTheHeldHeader) {
  const Bytes bytes = builtIndex();
  const IndexHeader held = heldHeaderOf(bytes);
  ASSERT_EQ(held.fileSize, sizeOf(bytes));
  EXPECT_TRUE(BibleSearchResidency::heldIndexMatches(held, bytes.data(), bytes.size(), sizeOf(bytes)));
}

TEST(BibleSearchResidency, ChangingAnyHeaderByteBreaksTheMatch) {
  const Bytes bytes = builtIndex();
  const IndexHeader held = heldHeaderOf(bytes);
  // All 48 bytes, which covers each of the twelve serialised fields (the magic
  // and the eleven IndexHeader fields).
  for (size_t i = 0; i < BibleSearch::INDEX_HEADER_BYTES; i++) {
    Bytes changed = bytes;
    changed[i] ^= 0x01;
    EXPECT_FALSE(BibleSearchResidency::heldIndexMatches(held, changed.data(), changed.size(), sizeOf(changed)))
        << "header byte " << i;
  }
}

TEST(BibleSearchResidency, AShortHeaderReadDoesNotMatch) {
  const Bytes bytes = builtIndex();
  const IndexHeader held = heldHeaderOf(bytes);
  EXPECT_FALSE(
      BibleSearchResidency::heldIndexMatches(held, bytes.data(), BibleSearch::INDEX_HEADER_BYTES - 1, sizeOf(bytes)));
}

TEST(BibleSearchResidency, ADifferentFileSizeDoesNotMatch) {
  const Bytes bytes = builtIndex();
  const IndexHeader held = heldHeaderOf(bytes);
  EXPECT_FALSE(BibleSearchResidency::heldIndexMatches(held, bytes.data(), bytes.size(), sizeOf(bytes) + 1));
  EXPECT_FALSE(BibleSearchResidency::heldIndexMatches(held, bytes.data(), bytes.size(), sizeOf(bytes) - 1));
}
