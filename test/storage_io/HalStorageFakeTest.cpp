// Pins the Storage fake to the SD card rules the code under test depends on, so
// a later "simplification" of the fake that breaks one fails here first.

#include <HalStorage.h>
#include <gtest/gtest.h>

#include <string>

#include "HalStorageFake.h"

namespace {

class HalStorageFake : public ::testing::Test {
 protected:
  void SetUp() override { storage_fake::reset(); }
};

TEST_F(HalStorageFake, RenameOntoAnExistingFileFailsAndKeepsBoth) {
  storage_fake::putFile("/d/a", "A");
  storage_fake::putFile("/d/b", "B");
  EXPECT_FALSE(Storage.rename("/d/a", "/d/b"));
  EXPECT_EQ(storage_fake::fileBytes("/d/a"), "A");
  EXPECT_EQ(storage_fake::fileBytes("/d/b"), "B");
}

TEST_F(HalStorageFake, RenameMovesTheBytes) {
  storage_fake::putFile("/d/a", "A");
  EXPECT_TRUE(Storage.rename("/d/a", "/d/b"));
  EXPECT_FALSE(storage_fake::fileBytes("/d/a").has_value());
  EXPECT_EQ(storage_fake::fileBytes("/d/b"), "A");
}

TEST_F(HalStorageFake, RenameIntoAMissingDirectoryFails) {
  storage_fake::putFile("/d/a", "A");
  EXPECT_FALSE(Storage.rename("/d/a", "/missing/a"));
  EXPECT_EQ(storage_fake::fileBytes("/d/a"), "A");
}

TEST_F(HalStorageFake, MkdirRefusesAnExistingDirectoryAndCreatesParents) {
  EXPECT_TRUE(Storage.mkdir("/a/b/c"));
  EXPECT_TRUE(storage_fake::isDir("/a"));
  EXPECT_TRUE(storage_fake::isDir("/a/b"));
  EXPECT_TRUE(storage_fake::isDir("/a/b/c"));
  EXPECT_FALSE(Storage.mkdir("/a/b/c"));
  EXPECT_FALSE(Storage.mkdir("/x/y", false));
}

TEST_F(HalStorageFake, WriteFileIntoAMissingDirectoryFails) {
  EXPECT_FALSE(Storage.writeFile("/missing/f", "x"));
  EXPECT_FALSE(Storage.exists("/missing/f"));
}

TEST_F(HalStorageFake, WriteFileReplacesAnExistingFile) {
  storage_fake::putFile("/d/f", "old");
  EXPECT_TRUE(Storage.writeFile("/d/f", "new"));
  EXPECT_EQ(storage_fake::fileBytes("/d/f"), "new");
}

TEST_F(HalStorageFake, ReadFileTruncatesSilentlyAtFiftyThousandBytes) {
  storage_fake::putFile("/big", std::string(50001, 'x'));
  EXPECT_EQ(Storage.readFile("/big").length(), 50000u);
}

TEST_F(HalStorageFake, ReadFileOfAMissingFileIsEmpty) { EXPECT_TRUE(Storage.readFile("/nope").isEmpty()); }

TEST_F(HalStorageFake, AFailedReadLooksLikeAnEmptyFileThatExists) {
  storage_fake::putFile("/f", "data");
  storage_fake::failReadsOf("/f");
  HalFile file;
  EXPECT_TRUE(Storage.exists("/f"));
  EXPECT_TRUE(Storage.readFile("/f").isEmpty());
  EXPECT_FALSE(Storage.openFileForRead("TEST", "/f", file));
}

TEST_F(HalStorageFake, AFailedWriteRemovesTheOldFileFirst) {
  storage_fake::putFile("/d/f", "old");
  storage_fake::failWritesTo("/d/f");
  EXPECT_FALSE(Storage.writeFile("/d/f", "new"));
  EXPECT_FALSE(Storage.exists("/d/f"));
}

TEST_F(HalStorageFake, AFailedRenameChangesNothing) {
  storage_fake::putFile("/d/a", "A");
  storage_fake::failRenamesFrom("/d/a");
  EXPECT_FALSE(Storage.rename("/d/a", "/d/b"));
  EXPECT_EQ(storage_fake::fileBytes("/d/a"), "A");
  EXPECT_FALSE(Storage.exists("/d/b"));
}

TEST_F(HalStorageFake, StreamedReadReturnsTheBytesThenEof) {
  storage_fake::putFile("/f", "abcd");
  HalFile file;
  ASSERT_TRUE(Storage.openFileForRead("TEST", std::string("/f"), file));
  EXPECT_EQ(file.size(), 4u);
  char buf[3] = {};
  EXPECT_EQ(file.read(buf, 3), 3);
  EXPECT_EQ(std::string(buf, 3), "abc");
  EXPECT_EQ(file.read(), 'd');
  EXPECT_EQ(file.read(), -1);
  EXPECT_EQ(file.read(buf, 3), 0);
}

TEST_F(HalStorageFake, WrittenHandleBytesLandOnTheCard) {
  ASSERT_TRUE(Storage.mkdir("/d"));
  HalFile file;
  ASSERT_TRUE(Storage.openFileForWrite("TEST", "/d/f", file));
  const uint8_t bytes[] = {'h', 'i'};
  EXPECT_EQ(file.write(bytes, 2), 2u);
  EXPECT_EQ(file.write(static_cast<uint8_t>('!')), 1u);
  EXPECT_EQ(storage_fake::fileBytes("/d/f"), "hi!");
}

TEST_F(HalStorageFake, ResetEmptiesTheCardAndClearsHooks) {
  storage_fake::putFile("/d/f", "x");
  storage_fake::failReadsOf("/d/f");
  storage_fake::reset();
  EXPECT_FALSE(Storage.exists("/d/f"));
  EXPECT_FALSE(Storage.exists("/d"));
  EXPECT_TRUE(Storage.exists("/"));
  storage_fake::putFile("/d/f", "x");
  EXPECT_EQ(Storage.readFile("/d/f").length(), 1u);
}

TEST_F(HalStorageFake, ClearFailuresKeepsTheCard) {
  storage_fake::putFile("/d/f", "x");
  storage_fake::failReadsOf("/d/f");
  storage_fake::failRenamesFrom("/d/f");
  storage_fake::failWritesTo("/d/g");
  storage_fake::clearFailures();
  EXPECT_EQ(Storage.readFile("/d/f").length(), 1u);
  EXPECT_TRUE(Storage.writeFile("/d/g", "y"));
  EXPECT_TRUE(Storage.rename("/d/f", "/d/h"));
}

}  // namespace
