#include <gtest/gtest.h>

#include "StudyStore/UnitFingerprint.h"

namespace {

TEST(UnitFingerprint, IsStableForIdenticalText) {
  EXPECT_EQ(study::fingerprintOf("en el principio"), study::fingerprintOf("en el principio"));
}

TEST(UnitFingerprint, DiffersWhenAWordChanges) {
  EXPECT_NE(study::fingerprintOf("en el principio"), study::fingerprintOf("en el prinicipio"));
}

TEST(UnitFingerprint, DiffersWhenLengthChangesButContentRhymes) {
  EXPECT_NE(study::fingerprintOf("amor"), study::fingerprintOf("amores"));
}

TEST(UnitFingerprint, CountsCodepointsNotBytes) {
  // "transformación" is 14 codepoints and 15 bytes. Storing the byte length
  // would make the fingerprint disagree with every offset in the record.
  EXPECT_EQ(study::fingerprintOf("transformación").length, 14u);
}

TEST(UnitFingerprint, MatchesTheKnownCrcOfACheckVector) {
  // CRC-32/ISO-HDLC of "123456789" is 0xCBF43926. Pins the nibble table.
  EXPECT_EQ(study::fingerprintOf("123456789").crc, 0xCBF43926u);
}

TEST(UnitFingerprint, StreamsInChunksToTheSameValueAsOneCall) {
  uint32_t crc = study::crc32Begin();
  crc = study::crc32Update(crc, "en el ");
  crc = study::crc32Update(crc, "principio");
  EXPECT_EQ(study::crc32End(crc), study::fingerprintOf("en el principio").crc);
}

TEST(UnitFingerprint, RoundTripsThroughItsCompactForm) {
  const study::Fingerprint f = study::fingerprintOf("en el principio");
  const auto parsed = study::fingerprintFromCompact(study::fingerprintToCompact(f));
  ASSERT_TRUE(parsed.has_value());
  EXPECT_EQ(*parsed, f);
}

TEST(UnitFingerprint, EmptyTextIsDistinguishableFromAbsent) {
  EXPECT_EQ(study::fingerprintOf("").length, 0u);
  EXPECT_FALSE(study::fingerprintFromCompact("").has_value());
}

}  // namespace
