// Host coverage for BookMetadataCache::load() over a book.bin seeded in the in-memory Storage fake.
// The layout is the one docs/file-formats.md documents and buildBookBin writes; buildBookBin itself
// needs a real zip and is not exercised here.

#include <BookMetadataCache.h>
#include <HalStorage.h>
#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <vector>

#include "HalStorageFake.h"

namespace {

// BOOK_CACHE_VERSION in BookMetadataCache.cpp. A bump there must bump this, and the layout below.
constexpr uint8_t BOOK_CACHE_VERSION = 10;
constexpr char CACHE_DIR[] = "/cache";
constexpr char BOOK_BIN[] = "/cache/book.bin";
// Every HalFile read load() makes before the spine pass: version, lutOffset, spineCount, tocCount,
// and a length plus the bytes of each of the five metadata strings.
constexpr size_t HEADER_READS = 4 + 5 * 2;
// SPINE_PASS_BUFFER_SIZE in BookMetadataCache.cpp.
constexpr size_t SPINE_PASS_BUFFER = 4096;

struct Spine {
  std::string href;
  uint32_t cumulativeSize;
  int16_t tocIndex;
};

struct Toc {
  std::string title;
  std::string href;
  std::string anchor;
  uint8_t level;
  int16_t spineIndex;
};

template <typename T>
void putPod(std::string& out, const T& value) {
  out.append(reinterpret_cast<const char*>(&value), sizeof(T));
}

void putString(std::string& out, const std::string& s) {
  putPod(out, static_cast<uint32_t>(s.size()));
  out += s;
}

struct BookBin {
  std::string bytes;
  size_t spineBlockOffset = 0;
  size_t spineBlockBytes = 0;
};

BookBin makeBookBin(const std::vector<Spine>& spine, const std::vector<Toc>& toc,
                    const uint8_t version = BOOK_CACHE_VERSION) {
  std::string metadata;
  for (const char* field : {"Title", "Author", "es", "", ""}) putString(metadata, field);

  const uint32_t lutOffset =
      sizeof(uint8_t) + sizeof(uint32_t) + 2 * sizeof(uint16_t) + static_cast<uint32_t>(metadata.size());
  const uint32_t lutSize = static_cast<uint32_t>(sizeof(uint32_t) * (spine.size() + toc.size()));
  const uint32_t entriesOffset = lutOffset + lutSize;

  std::string spineBlock;
  std::vector<uint32_t> spineLut;
  for (const Spine& entry : spine) {
    spineLut.push_back(entriesOffset + static_cast<uint32_t>(spineBlock.size()));
    putString(spineBlock, entry.href);
    putPod(spineBlock, entry.cumulativeSize);
    putPod(spineBlock, entry.tocIndex);
  }

  std::string tocBlock;
  std::vector<uint32_t> tocLut;
  for (const Toc& entry : toc) {
    tocLut.push_back(entriesOffset + static_cast<uint32_t>(spineBlock.size() + tocBlock.size()));
    putString(tocBlock, entry.title);
    putString(tocBlock, entry.href);
    putString(tocBlock, entry.anchor);
    putPod(tocBlock, entry.level);
    putPod(tocBlock, entry.spineIndex);
  }

  BookBin bin;
  putPod(bin.bytes, version);
  putPod(bin.bytes, lutOffset);
  putPod(bin.bytes, static_cast<uint16_t>(spine.size()));
  putPod(bin.bytes, static_cast<uint16_t>(toc.size()));
  bin.bytes += metadata;
  for (const uint32_t pos : spineLut) putPod(bin.bytes, pos);
  for (const uint32_t pos : tocLut) putPod(bin.bytes, pos);
  bin.spineBlockOffset = bin.bytes.size();
  bin.spineBlockBytes = spineBlock.size();
  bin.bytes += spineBlock;
  bin.bytes += tocBlock;
  return bin;
}

// `count` entries with NWT-shaped hrefs; every seventh is ~200 bytes, longer than the 64-byte skip
// scratch. Cumulative sizes run 1000, 2000, ...
std::vector<Spine> spineOf(const size_t count) {
  std::vector<Spine> spine;
  spine.reserve(count);
  for (size_t i = 0; i < count; ++i) {
    std::string href = "OEBPS/10010611" + std::to_string(i) + "-split16.xhtml";
    if (i % 7 == 3) href = "OEBPS/Text/" + std::string(190, static_cast<char>('a' + i % 26)) + ".xhtml";
    spine.push_back({href, static_cast<uint32_t>((i + 1) * 1000), static_cast<int16_t>(i % 5)});
  }
  return spine;
}

const std::vector<Toc> TOC = {
    {"Génesis", "OEBPS/1001061100.xhtml", "", 1, 0},
    {"Éxodo", "OEBPS/1001061101.xhtml", "chapter1", 2, 1},
};

class BookMetadataCacheTest : public ::testing::Test {
 protected:
  void SetUp() override { storage_fake::reset(); }
};

}  // namespace

TEST_F(BookMetadataCacheTest, LoadKeepsEveryCumulativeSize) {
  const std::vector<Spine> spine = {
      {"", 100, -1},
      {"a.xhtml", 250, 0},
      {"OEBPS/1001061146-split16.xhtml", 900, 1},
      {"OEBPS/Text/" + std::string(200, 'x') + ".xhtml", 1500, 1},
  };
  storage_fake::putFile(BOOK_BIN, makeBookBin(spine, TOC).bytes);

  BookMetadataCache cache(CACHE_DIR);
  ASSERT_TRUE(cache.load());
  EXPECT_TRUE(cache.isLoaded());
  ASSERT_EQ(cache.getSpineCount(), 4);
  for (int i = 0; i < 4; ++i) EXPECT_EQ(cache.getCumulativeSize(i), spine[i].cumulativeSize) << "entry " << i;
  EXPECT_EQ(cache.coreMetadata.title, "Title");
}

TEST_F(BookMetadataCacheTest, EntriesStraddlingABufferRefillLoadWhole) {
  const std::vector<Spine> spine = spineOf(400);
  const BookBin bin = makeBookBin(spine, TOC);
  ASSERT_GT(bin.spineBlockBytes, 3 * SPINE_PASS_BUFFER) << "the spine must span several refills";
  storage_fake::putFile(BOOK_BIN, bin.bytes);

  BookMetadataCache cache(CACHE_DIR);
  ASSERT_TRUE(cache.load());
  for (size_t i = 0; i < spine.size(); ++i) {
    EXPECT_EQ(cache.getCumulativeSize(static_cast<int>(i)), spine[i].cumulativeSize) << "entry " << i;
  }
}

// The NWT's 3,937 entries took 276 ms on the device read four HalFile calls at a time, each taking
// storageMutex. The pass must cost one read per buffer window instead.
TEST_F(BookMetadataCacheTest, TheSpineIsReadInBufferWindowsNotPerEntry) {
  const std::vector<Spine> spine = spineOf(3937);
  const BookBin bin = makeBookBin(spine, TOC);
  storage_fake::putFile(BOOK_BIN, bin.bytes);

  BookMetadataCache cache(CACHE_DIR);
  ASSERT_TRUE(cache.load());
  const size_t windows = (bin.spineBlockBytes + SPINE_PASS_BUFFER - 1) / SPINE_PASS_BUFFER;
  EXPECT_LE(storage_fake::readCallsOn(BOOK_BIN), HEADER_READS + windows)
      << "per-entry reads would be " << HEADER_READS + 4 * spine.size();
}

TEST_F(BookMetadataCacheTest, AnotherVersionIsRefused) {
  storage_fake::putFile(BOOK_BIN, makeBookBin(spineOf(3), TOC, BOOK_CACHE_VERSION - 1).bytes);

  BookMetadataCache cache(CACHE_DIR);
  EXPECT_FALSE(cache.load());
  EXPECT_FALSE(cache.isLoaded());
}

// A truncated book.bin used to load garbage sizes; it must fail so Epub::load rebuilds it.
TEST_F(BookMetadataCacheTest, AShortSpineBlockFailsTheLoad) {
  const BookBin bin = makeBookBin(spineOf(50), TOC);
  storage_fake::putFile(BOOK_BIN, bin.bytes.substr(0, bin.spineBlockOffset + bin.spineBlockBytes / 2));

  BookMetadataCache cache(CACHE_DIR);
  EXPECT_FALSE(cache.load());
  EXPECT_FALSE(cache.isLoaded());
  EXPECT_EQ(cache.getCumulativeSize(0), 0u);
}

TEST_F(BookMetadataCacheTest, EntriesStillResolveThroughTheLookupTablesAfterLoad) {
  const std::vector<Spine> spine = spineOf(20);
  storage_fake::putFile(BOOK_BIN, makeBookBin(spine, TOC).bytes);

  BookMetadataCache cache(CACHE_DIR);
  ASSERT_TRUE(cache.load());
  for (const int i : {0, 3, 19}) {
    const BookMetadataCache::SpineEntry entry = cache.getSpineEntry(i);
    EXPECT_EQ(entry.href, spine[i].href) << "entry " << i;
    EXPECT_EQ(entry.cumulativeSize, spine[i].cumulativeSize) << "entry " << i;
    EXPECT_EQ(entry.tocIndex, spine[i].tocIndex) << "entry " << i;
  }
  const BookMetadataCache::TocEntry toc = cache.getTocEntry(1);
  EXPECT_EQ(toc.title, "Éxodo");
  EXPECT_EQ(toc.anchor, "chapter1");
  EXPECT_EQ(toc.spineIndex, 1);
}

// A parked book's cache is checked again before reuse (ParkedBook). The old handle may name a file
// that was deleted and rebuilt since, so reopen() reads a fresh one and compares it with load()'s.
TEST_F(BookMetadataCacheTest, ReopenKeepsAnUntouchedCache) {
  const std::vector<Spine> spine = spineOf(20);
  storage_fake::putFile(BOOK_BIN, makeBookBin(spine, TOC).bytes);

  BookMetadataCache cache(CACHE_DIR);
  ASSERT_TRUE(cache.load());
  EXPECT_TRUE(cache.reopen());
  EXPECT_TRUE(cache.isLoaded());
  EXPECT_EQ(cache.getSpineEntry(19).href, spine[19].href);
}

TEST_F(BookMetadataCacheTest, ReopenNeedsALoadFirst) {
  storage_fake::putFile(BOOK_BIN, makeBookBin(spineOf(3), TOC).bytes);

  BookMetadataCache cache(CACHE_DIR);
  EXPECT_FALSE(cache.reopen());
}

TEST_F(BookMetadataCacheTest, ReopenFailsOnceTheFileIsGone) {
  storage_fake::putFile(BOOK_BIN, makeBookBin(spineOf(3), TOC).bytes);

  BookMetadataCache cache(CACHE_DIR);
  ASSERT_TRUE(cache.load());
  ASSERT_TRUE(Storage.remove(BOOK_BIN));
  EXPECT_FALSE(cache.reopen());
  EXPECT_FALSE(cache.isLoaded());
}

TEST_F(BookMetadataCacheTest, ReopenFailsWhenTheSizeChanged) {
  const BookBin bin = makeBookBin(spineOf(3), TOC);
  storage_fake::putFile(BOOK_BIN, bin.bytes);

  BookMetadataCache cache(CACHE_DIR);
  ASSERT_TRUE(cache.load());
  storage_fake::putFile(BOOK_BIN, bin.bytes + "x");
  EXPECT_FALSE(cache.reopen());
  EXPECT_FALSE(cache.isLoaded());
}

TEST_F(BookMetadataCacheTest, ReopenFailsWhenTheVersionChangedAtTheSameSize) {
  const BookBin bin = makeBookBin(spineOf(3), TOC);
  storage_fake::putFile(BOOK_BIN, bin.bytes);

  BookMetadataCache cache(CACHE_DIR);
  ASSERT_TRUE(cache.load());
  std::string otherVersion = bin.bytes;
  otherVersion[0] = static_cast<char>(BOOK_CACHE_VERSION + 1);
  storage_fake::putFile(BOOK_BIN, otherVersion);
  EXPECT_FALSE(cache.reopen());
  EXPECT_FALSE(cache.isLoaded());
}

// Byte 5 is the low byte of spineCount: u8 version at 0, u32 lutOffset at 1-4.
TEST_F(BookMetadataCacheTest, ReopenFailsWhenTheSpineCountChangedAtTheSameSize) {
  const BookBin bin = makeBookBin(spineOf(3), TOC);
  storage_fake::putFile(BOOK_BIN, bin.bytes);

  BookMetadataCache cache(CACHE_DIR);
  ASSERT_TRUE(cache.load());
  std::string otherSpineCount = bin.bytes;
  otherSpineCount[5] = static_cast<char>(otherSpineCount[5] ^ 1);
  storage_fake::putFile(BOOK_BIN, otherSpineCount);
  EXPECT_FALSE(cache.reopen());
  EXPECT_FALSE(cache.isLoaded());
}
