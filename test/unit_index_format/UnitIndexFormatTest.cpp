#include <gtest/gtest.h>

#include <cstring>
#include <vector>

#include "StudyStore/UnitIndexFormat.h"

namespace {

study::UnitIndexHeader sampleHeader() {
  study::UnitIndexHeader h;
  h.documentCount = 3937;
  h.sourceSize = 104004412;
  h.tableCrc = 0xdeadbeef;
  h.tableOffset = study::UNIT_INDEX_HEADER_BYTES;
  h.bookMapOffset = 0;
  return h;
}

TEST(UnitIndexFormat, RoundTripsAHeaderThroughBytes) {
  uint8_t buf[study::UNIT_INDEX_HEADER_BYTES];
  study::writeHeader(buf, sampleHeader());
  const auto back = study::readHeader(buf, sizeof(buf));
  ASSERT_TRUE(back.has_value());
  EXPECT_EQ(back->documentCount, 3937);
  EXPECT_EQ(back->sourceSize, 104004412u);
  EXPECT_EQ(back->tableCrc, 0xdeadbeefu);
  EXPECT_EQ(back->tableOffset, study::UNIT_INDEX_HEADER_BYTES);
  EXPECT_EQ(back->bookMapOffset, 0u);
}

TEST(UnitIndexFormat, RoundTripsADocumentEntryWithItsAnchors) {
  study::UnitIndexEntry e;
  e.dataOffset = 4096;
  e.anchorCount = 2;
  e.kind = study::UnitKind::Verse;
  e.book = 19;

  uint8_t entry[study::UNIT_INDEX_ENTRY_BYTES];
  study::writeEntry(entry, e);
  const auto backEntry = study::readEntry(entry);
  EXPECT_EQ(backEntry.dataOffset, 4096u);
  EXPECT_EQ(backEntry.anchorCount, 2);
  EXPECT_EQ(backEntry.kind, study::UnitKind::Verse);
  EXPECT_EQ(backEntry.book, 19);
  EXPECT_TRUE(backEntry.indexed());

  const std::vector<study::UnitAnchor> anchors{{100, 119, 145}, {220, 119, 146}};
  std::vector<uint8_t> data(anchors.size() * study::UNIT_INDEX_ANCHOR_BYTES);
  study::writeAnchors(data.data(), anchors);
  const auto backAnchors = study::readAnchors(data.data(), 2);
  ASSERT_EQ(backAnchors.size(), 2u);
  EXPECT_EQ(backAnchors[0].offset, 100u);
  EXPECT_EQ(backAnchors[0].major, 119);
  EXPECT_EQ(backAnchors[0].minor, 145);
  EXPECT_EQ(backAnchors[1].minor, 146);
}

TEST(UnitIndexFormat, RejectsAWrongMagic) {
  uint8_t buf[study::UNIT_INDEX_HEADER_BYTES];
  study::writeHeader(buf, sampleHeader());
  buf[0] ^= 0xFF;
  EXPECT_FALSE(study::readHeader(buf, sizeof(buf)).has_value());
}

TEST(UnitIndexFormat, RejectsAFutureFormatVersion) {
  uint8_t buf[study::UNIT_INDEX_HEADER_BYTES];
  study::writeHeader(buf, sampleHeader());
  const uint16_t future = study::UNIT_INDEX_VERSION + 1;
  memcpy(buf + 4, &future, sizeof(future));
  EXPECT_FALSE(study::readHeader(buf, sizeof(buf)).has_value());
}

TEST(UnitIndexFormat, RejectsATruncatedHeader) {
  uint8_t buf[study::UNIT_INDEX_HEADER_BYTES];
  study::writeHeader(buf, sampleHeader());
  EXPECT_FALSE(study::readHeader(buf, study::UNIT_INDEX_HEADER_BYTES - 1).has_value());
}

TEST(UnitIndexFormat, ReportsADocumentWithDataOffsetZeroAsNotYetIndexed) {
  uint8_t entry[study::UNIT_INDEX_ENTRY_BYTES];
  study::writeEntry(entry, study::UnitIndexEntry{});
  EXPECT_FALSE(study::readEntry(entry).indexed());
}

TEST(UnitIndexFormat, DetectsAChangedSourceSizeAsStale) {
  const auto h = sampleHeader();
  EXPECT_FALSE(study::headerIsStale(h, 104004412));
  EXPECT_TRUE(study::headerIsStale(h, 104004413));
}

TEST(UnitIndexFormat, DetectsACorruptedTableViaItsCrc) {
  std::vector<uint8_t> table(study::UNIT_INDEX_ENTRY_BYTES * 4, 0);
  study::UnitIndexEntry e;
  e.dataOffset = 64;
  e.anchorCount = 5;
  study::writeEntry(table.data(), e);

  const uint32_t good = study::tableChecksum(table.data(), table.size());
  table[7] ^= 0x01;  // one bit of one entry, as a torn sector would leave it
  EXPECT_NE(study::tableChecksum(table.data(), table.size()), good);
}

TEST(UnitIndexFormat, ReadsEveryFieldThroughMemcpyOnAnUnalignedBuffer) {
  // The alignment guard. A pointer cast here faults on the C3 and trips UBSan
  // everywhere; this is shared code, so the rule is unconditional.
  std::vector<uint8_t> buf(1 + study::UNIT_INDEX_HEADER_BYTES + study::UNIT_INDEX_ENTRY_BYTES +
                           2 * study::UNIT_INDEX_ANCHOR_BYTES);
  uint8_t* base = buf.data() + 1;  // deliberately odd

  study::writeHeader(base, sampleHeader());
  study::UnitIndexEntry e;
  e.dataOffset = 4096;
  e.anchorCount = 2;
  e.kind = study::UnitKind::Paragraph;
  e.book = 0;
  study::writeEntry(base + study::UNIT_INDEX_HEADER_BYTES, e);
  study::writeAnchors(base + study::UNIT_INDEX_HEADER_BYTES + study::UNIT_INDEX_ENTRY_BYTES,
                      {{7, 0, 40}, {99, 0, 7}});

  const auto h = study::readHeader(base, study::UNIT_INDEX_HEADER_BYTES);
  ASSERT_TRUE(h.has_value());
  EXPECT_EQ(h->sourceSize, 104004412u);

  const auto backEntry = study::readEntry(base + study::UNIT_INDEX_HEADER_BYTES);
  EXPECT_EQ(backEntry.dataOffset, 4096u);
  EXPECT_EQ(backEntry.kind, study::UnitKind::Paragraph);

  const auto anchors =
      study::readAnchors(base + study::UNIT_INDEX_HEADER_BYTES + study::UNIT_INDEX_ENTRY_BYTES, 2);
  ASSERT_EQ(anchors.size(), 2u);
  EXPECT_EQ(anchors[0].minor, 40);
  EXPECT_EQ(anchors[1].minor, 7) << "pid order is not document order, and the format must not assume it is";
}

TEST(UnitIndexFormat, AnUnknownKindByteDegradesToDocumentOffset) {
  uint8_t entry[study::UNIT_INDEX_ENTRY_BYTES];
  study::writeEntry(entry, study::UnitIndexEntry{});
  entry[6] = 99;
  EXPECT_EQ(study::readEntry(entry).kind, study::UnitKind::DocumentOffset)
      << "a corrupt kind must not index anchors as verses";
}

TEST(UnitIndexFormat, TheNwtIndexFitsInAReasonableFile) {
  // 3,937 documents plus ~31,000 verse anchors and the paragraph anchors.
  const size_t table = 3937 * study::UNIT_INDEX_ENTRY_BYTES;
  const size_t anchors = 40000 * study::UNIT_INDEX_ANCHOR_BYTES;
  EXPECT_LT(study::UNIT_INDEX_HEADER_BYTES + table + anchors, 512u * 1024u);
}

}  // namespace
