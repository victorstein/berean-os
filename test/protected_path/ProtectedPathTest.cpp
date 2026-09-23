#include <ProtectedPath.h>
#include <gtest/gtest.h>

using protectedpath::isProtectedName;
using protectedpath::isProtectedPath;

TEST(ProtectedPath, OrdinaryPathsAreAllowed) {
  EXPECT_FALSE(isProtectedPath("/"));
  EXPECT_FALSE(isProtectedPath(""));
  EXPECT_FALSE(isProtectedPath("/firmware.bin"));
  EXPECT_FALSE(isProtectedPath("/books/novel.epub"));
  EXPECT_FALSE(isProtectedPath("/v1.0/notes.txt"));
}

TEST(ProtectedPath, EveryComponentIsChecked) {
  EXPECT_TRUE(isProtectedPath("/.berean"));
  EXPECT_TRUE(isProtectedPath("/.berean/tags.json"));
  EXPECT_TRUE(isProtectedPath("/.crosspoint/wifi.json"));
  EXPECT_TRUE(isProtectedPath("/books/.hidden/x.epub"));
  EXPECT_TRUE(isProtectedPath("/System Volume Information/IndexerVolumeGuid"));
}

TEST(ProtectedPath, DotDotIsRefusedNotResolved) {
  EXPECT_TRUE(isProtectedPath("/books/../.berean/tags.json"));
  EXPECT_TRUE(isProtectedPath("/books/../novel.epub"));
  EXPECT_TRUE(isProtectedPath(".."));
  EXPECT_TRUE(isProtectedPath("/./books"));
}

TEST(ProtectedPath, TrailingAndDoubledSlashesDoNotHideAComponent) {
  EXPECT_TRUE(isProtectedPath("/.berean/"));
  EXPECT_TRUE(isProtectedPath("//.berean//tags.json"));
  EXPECT_TRUE(isProtectedPath(".berean/tags.json"));
  EXPECT_FALSE(isProtectedPath("//books//"));
}

TEST(ProtectedPath, BackslashCountsAsASeparator) { EXPECT_TRUE(isProtectedPath("/books\\.berean\\tags.json")); }

TEST(ProtectedPath, CaseVariantsAreProtected) {
  EXPECT_TRUE(isProtectedPath("/.BEREAN/tags.json"));
  EXPECT_TRUE(isProtectedPath("/system volume information"));
  EXPECT_TRUE(isProtectedPath("/xtcache/page.bin"));
}

TEST(ProtectedPath, MatchesTheNameSdFatWouldResolve) {
  // SdFat skips leading spaces and trims trailing dots and spaces from each
  // component, so these open the protected folders on the card.
  EXPECT_TRUE(isProtectedPath("/ .berean/tags.json"));
  EXPECT_TRUE(isProtectedPath("/  .crosspoint/wifi.json"));
  EXPECT_TRUE(isProtectedPath("/XTCache./page.bin"));
  EXPECT_TRUE(isProtectedPath("/System Volume Information . /x"));
}

TEST(ProtectedPath, PercentEncodingIsNotDecodedHere) {
  // Handlers receive decoded paths; a literal "%2E" is an ordinary character
  // to SdFat, naming a different file from ".berean".
  EXPECT_FALSE(isProtectedPath("/%2Eberean/tags.json"));
  EXPECT_TRUE(isProtectedPath("/.berean/tags.json"));
}

TEST(ProtectedPath, NameCheckMatchesListingEntries) {
  EXPECT_TRUE(isProtectedName(".berean"));
  EXPECT_TRUE(isProtectedName("XTCache"));
  EXPECT_FALSE(isProtectedName("books"));
  EXPECT_FALSE(isProtectedName("notes.v2.txt"));
}
