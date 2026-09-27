#include <gtest/gtest.h>

#include <algorithm>
#include <optional>
#include <string>
#include <vector>

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

// The order is what keeps a registered Bible ahead of any book whose title
// merely says "New World", and the recents guess behind both real lookups.

namespace {

std::optional<std::string> resolveRecording(std::vector<BibleLookup>& tried, const BibleLookup hitAt) {
  return resolveBible([&](const BibleLookup step) -> std::optional<std::string> {
    tried.push_back(step);
    if (step == hitAt) return std::string("/hit.epub");
    return std::nullopt;
  });
}

}  // namespace

TEST(LauncherBible, TriesTheRegistryThenTheCardThenRecentsAndOffersNothingWhenAllMiss) {
  std::vector<BibleLookup> tried;
  const auto found = resolveBible([&](const BibleLookup step) -> std::optional<std::string> {
    tried.push_back(step);
    return std::nullopt;
  });
  EXPECT_FALSE(found.has_value());
  EXPECT_EQ(tried, (std::vector<BibleLookup>{BibleLookup::Registry, BibleLookup::CardScan, BibleLookup::Recents}));
}

TEST(LauncherBible, ARegistryHitSkipsTheCardScanAndRecents) {
  std::vector<BibleLookup> tried;
  EXPECT_EQ(resolveRecording(tried, BibleLookup::Registry), std::optional<std::string>("/hit.epub"));
  EXPECT_EQ(tried, (std::vector<BibleLookup>{BibleLookup::Registry}));
}

TEST(LauncherBible, ACardScanHitSkipsRecents) {
  std::vector<BibleLookup> tried;
  EXPECT_EQ(resolveRecording(tried, BibleLookup::CardScan), std::optional<std::string>("/hit.epub"));
  EXPECT_EQ(tried, (std::vector<BibleLookup>{BibleLookup::Registry, BibleLookup::CardScan}));
}

TEST(LauncherBible, RecentsIsReachedOnlyAfterBothRealLookupsMiss) {
  std::vector<BibleLookup> tried;
  EXPECT_EQ(resolveRecording(tried, BibleLookup::Recents), std::optional<std::string>("/hit.epub"));
  EXPECT_EQ(tried, (std::vector<BibleLookup>{BibleLookup::Registry, BibleLookup::CardScan, BibleLookup::Recents}));
}

TEST(LauncherBible, NamesEachLookupForTheLog) {
  EXPECT_STREQ(bibleLookupName(BibleLookup::Registry), "registry");
  EXPECT_STREQ(bibleLookupName(BibleLookup::CardScan), "card scan");
  EXPECT_STREQ(bibleLookupName(BibleLookup::Recents), "recents");
}

// The pre-#104 guess, verbatim: it only runs when nothing better exists.

TEST(LauncherBible, RecentsGuessAcceptsAnNwtPathOrAnNwtTitleInEitherLanguage) {
  EXPECT_TRUE(looksLikeBibleInRecents("/x/nwt_S.epub", ""));
  EXPECT_TRUE(looksLikeBibleInRecents("/nwt/Biblia.epub", "Biblia"));
  EXPECT_TRUE(looksLikeBibleInRecents("/Libros/Biblia.epub", "La Biblia. Traducción del Nuevo Mundo"));
  EXPECT_TRUE(looksLikeBibleInRecents("/Books/Bible.epub", "New World Translation of the Holy Scriptures"));
}

TEST(LauncherBible, RecentsGuessRejectsABibleThatSaysNeither) {
  EXPECT_FALSE(looksLikeBibleInRecents("/Libros/Biblia.epub", "Biblia"));
  EXPECT_FALSE(looksLikeBibleInRecents("", ""));
}

TEST(LauncherBible, RecentsGuessIsCaseSensitiveAsBefore) {
  EXPECT_FALSE(looksLikeBibleInRecents("/Books/Bible.epub", "new world"));
  EXPECT_FALSE(looksLikeBibleInRecents("/NWT.epub", ""));
}

// Registration runs on every open of a Bible, so it must write at most once
// per path and never relabel an entry some other writer made.

namespace {

struct FakeRegistry {
  std::vector<std::string> entries;
  int recordCalls = 0;
  int lookupCalls = 0;
  bool refuse = false;

  BibleRegistration open(const bool isBible, const std::string& path) {
    return registerBibleIfUnknown(
        isBible, path,
        [this](const std::string& p) {
          ++lookupCalls;
          return std::find(entries.begin(), entries.end(), p) != entries.end();
        },
        [this](const std::string& p) {
          ++recordCalls;
          if (refuse) return false;
          entries.push_back(p);
          return true;
        });
  }
};

}  // namespace

TEST(LauncherBible, ANonBibleNeverTouchesTheRegistry) {
  FakeRegistry registry;
  EXPECT_EQ(registry.open(false, "/Libros/Novela.epub"), BibleRegistration::NotBible);
  EXPECT_EQ(registry.lookupCalls, 0);
  EXPECT_EQ(registry.recordCalls, 0);
}

TEST(LauncherBible, AnUnknownBibleIsRecordedOnceAcrossOpens) {
  FakeRegistry registry;
  EXPECT_EQ(registry.open(true, "/Libros/Biblia.epub"), BibleRegistration::Recorded);
  EXPECT_EQ(registry.open(true, "/Libros/Biblia.epub"), BibleRegistration::AlreadyKnown);
  EXPECT_EQ(registry.recordCalls, 1);
  EXPECT_EQ(registry.entries, std::vector<std::string>{"/Libros/Biblia.epub"});
}

TEST(LauncherBible, AnyExistingEntryWinsWhateverItsSymbol) {
  FakeRegistry registry;
  registry.entries.push_back("/nwtsty_S.epub");
  EXPECT_EQ(registry.open(true, "/nwtsty_S.epub"), BibleRegistration::AlreadyKnown);
  EXPECT_EQ(registry.recordCalls, 0);
}

TEST(LauncherBible, ARefusedWriteIsReportedAndLeavesNoEntry) {
  FakeRegistry registry;
  registry.refuse = true;
  EXPECT_EQ(registry.open(true, "/Libros/Biblia.epub"), BibleRegistration::Refused);
  EXPECT_EQ(registry.recordCalls, 1);
  EXPECT_TRUE(registry.entries.empty());

  // A refused write leaves the path unregistered, so the next open retries.
  EXPECT_EQ(registry.open(true, "/Libros/Biblia.epub"), BibleRegistration::Refused);
  EXPECT_EQ(registry.recordCalls, 2);
}
