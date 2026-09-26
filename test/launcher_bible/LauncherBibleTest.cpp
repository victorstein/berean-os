#include <gtest/gtest.h>

#include "activities/launcher/LauncherBible.h"

// The card scan is the launcher's only way to find a Bible the registry does
// not know, so a false positive puts another book on the Bible tile and a false
// negative reports "no Bible" with one on the card.

TEST(LauncherBible, RecognisesTheCdnNameInAnyLanguage) {
  EXPECT_TRUE(isCdnNamedCopyOf("/nwt_S.epub", BIBLE_SYMBOL));
  EXPECT_TRUE(isCdnNamedCopyOf("/books/nwt_E.epub", BIBLE_SYMBOL));
  EXPECT_TRUE(isCdnNamedCopyOf("nwt_CHS.epub", BIBLE_SYMBOL));
}

TEST(LauncherBible, ToleratesAnUpperCaseExtension) { EXPECT_TRUE(isCdnNamedCopyOf("/NWT/nwt_S.EPUB", BIBLE_SYMBOL)); }

TEST(LauncherBible, AnotherSymbolThatMerelyStartsTheSameIsNotTheBible) {
  EXPECT_FALSE(isCdnNamedCopyOf("/nwtsty_S.epub", BIBLE_SYMBOL));
  EXPECT_FALSE(isCdnNamedCopyOf("/lff_S.epub", BIBLE_SYMBOL));
}

TEST(LauncherBible, OnlyTheFilenameCounts) {
  EXPECT_FALSE(isCdnNamedCopyOf("/nwt_S.epub/lff_S.epub", BIBLE_SYMBOL));
  EXPECT_FALSE(isCdnNamedCopyOf("/Traduccion del Nuevo Mundo (nwt-S).epub", BIBLE_SYMBOL));
}

TEST(LauncherBible, APeriodicalCdnNameIsNotAPublicationWithoutAnIssue) {
  EXPECT_FALSE(isCdnNamedCopyOf("/w_S_202607.epub", "w"));
}

TEST(LauncherBible, RejectsMalformedNames) {
  EXPECT_FALSE(isCdnNamedCopyOf("/nwt_.epub", BIBLE_SYMBOL));
  EXPECT_FALSE(isCdnNamedCopyOf("/nwt_s.epub", BIBLE_SYMBOL));
  EXPECT_FALSE(isCdnNamedCopyOf("/nwt_S.epub.part", BIBLE_SYMBOL));
  EXPECT_FALSE(isCdnNamedCopyOf("/nwt_S.pdf", BIBLE_SYMBOL));
  EXPECT_FALSE(isCdnNamedCopyOf("/nwt.epub", BIBLE_SYMBOL));
  EXPECT_FALSE(isCdnNamedCopyOf("", BIBLE_SYMBOL));
  EXPECT_FALSE(isCdnNamedCopyOf("/nwt_S.epub", ""));
}
