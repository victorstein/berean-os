#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

#include "Catalog/CatalogArchive.h"

namespace {

// A gzip member with the given FLG byte and optional-field bytes spliced in.
// Payload and trailer are filler: these tests are about the container, and the
// inflate itself is uzlib's job.
std::vector<uint8_t> member(const uint8_t flags, const std::vector<uint8_t>& optional, const size_t payloadBytes = 4,
                            const uint32_t isize = 0) {
  std::vector<uint8_t> out = {0x1F, 0x8B, 0x08, flags, 0, 0, 0, 0, 0, 0x03};
  out.insert(out.end(), optional.begin(), optional.end());
  out.insert(out.end(), payloadBytes, 0xAA);
  for (int i = 0; i < 4; ++i) out.push_back(0xBB);  // CRC32
  for (int i = 0; i < 4; ++i) out.push_back(static_cast<uint8_t>(isize >> (8 * i)));
  return out;
}

TEST(GzipDetection, RecognisesTheMagic) {
  const auto gz = member(0, {});
  EXPECT_TRUE(catalog::looksGzipped(gz.data(), gz.size()));

  const char* plain = "berean-catalog\t1\tS\tid\t2026-09-12\n";
  EXPECT_FALSE(catalog::looksGzipped(reinterpret_cast<const uint8_t*>(plain), 33))
      << "the published asset may be plain text, and the loader decides from the bytes";
}

TEST(GzipDetection, SurvivesAnEmptyOrOneByteBuffer) {
  const uint8_t one = 0x1F;
  EXPECT_FALSE(catalog::looksGzipped(nullptr, 0));
  EXPECT_FALSE(catalog::looksGzipped(&one, 1));
}

TEST(GzipHeader, PlainMemberStartsAtTenBytes) {
  const auto gz = member(0, {});
  size_t offset = 0;
  ASSERT_TRUE(catalog::gzipPayloadOffset(gz.data(), gz.size(), offset));
  EXPECT_EQ(offset, 10u);
}

TEST(GzipHeader, SkipsTheOriginalFilename) {
  // gzip(1) sets FNAME by default, so this is the shape the CI job produces.
  const std::vector<uint8_t> name = {'c', 'a', 't', '.', 't', 'x', 't', 0};
  const auto gz = member(0x08, name);
  size_t offset = 0;
  ASSERT_TRUE(catalog::gzipPayloadOffset(gz.data(), gz.size(), offset));
  EXPECT_EQ(offset, 10u + name.size());
}

TEST(GzipHeader, SkipsExtraCommentAndHeaderCrcTogether) {
  std::vector<uint8_t> optional = {0x02, 0x00, 0xDE, 0xAD};  // FEXTRA: XLEN 2 + 2 bytes
  const std::vector<uint8_t> name = {'a', 0};
  const std::vector<uint8_t> comment = {'h', 'i', 0};
  optional.insert(optional.end(), name.begin(), name.end());
  optional.insert(optional.end(), comment.begin(), comment.end());
  optional.push_back(0xAA);  // FHCRC
  optional.push_back(0xBB);

  const auto gz = member(0x02 | 0x04 | 0x08 | 0x10, optional);
  size_t offset = 0;
  ASSERT_TRUE(catalog::gzipPayloadOffset(gz.data(), gz.size(), offset));
  EXPECT_EQ(offset, 10u + optional.size());
}

TEST(GzipHeader, RejectsANonDeflateMethod) {
  auto gz = member(0, {});
  gz[2] = 0x07;
  size_t offset = 0;
  EXPECT_FALSE(catalog::gzipPayloadOffset(gz.data(), gz.size(), offset));
}

TEST(GzipHeader, RejectsReservedFlagBits) {
  const auto gz = member(0x20, {});
  size_t offset = 0;
  EXPECT_FALSE(catalog::gzipPayloadOffset(gz.data(), gz.size(), offset));
}

TEST(GzipHeader, RejectsAnUnterminatedFilename) {
  // A truncated download must not walk off the end looking for the NUL.
  std::vector<uint8_t> gz = {0x1F, 0x8B, 0x08, 0x08, 0, 0, 0, 0, 0, 0x03, 'c', 'a', 't'};
  size_t offset = 0;
  EXPECT_FALSE(catalog::gzipPayloadOffset(gz.data(), gz.size(), offset));
}

TEST(GzipHeader, RejectsAnExtraFieldLongerThanTheBuffer) {
  const std::vector<uint8_t> optional = {0xFF, 0xFF, 0x01};
  const auto gz = member(0x04, optional);
  size_t offset = 0;
  EXPECT_FALSE(catalog::gzipPayloadOffset(gz.data(), gz.size(), offset));
}

TEST(GzipHeader, RejectsAMemberWithNoRoomForAPayload) {
  // Header and trailer only: nothing to inflate.
  const std::vector<uint8_t> gz = {0x1F, 0x8B, 0x08, 0, 0, 0, 0, 0, 0, 0x03, 0, 0, 0, 0, 0, 0, 0, 0};
  size_t offset = 0;
  EXPECT_FALSE(catalog::gzipPayloadOffset(gz.data(), gz.size(), offset));
}

TEST(GzipTrailer, ReadsTheDeclaredSizeLittleEndian) {
  const auto gz = member(0, {}, 4, 216708);
  uint32_t size = 0;
  ASSERT_TRUE(catalog::gzipDeclaredSize(gz.data(), gz.size(), size));
  EXPECT_EQ(size, 216708u) << "the inflated buffer is sized from this";
}

TEST(GzipTrailer, RefusesABufferTooShortToHoldOne) {
  const std::vector<uint8_t> stub = {0x1F, 0x8B, 0x08, 0x00};
  uint32_t size = 0;
  EXPECT_FALSE(catalog::gzipDeclaredSize(stub.data(), stub.size(), size));
}

}  // namespace
