#include <gtest/gtest.h>

#include "StudyStore/PubKey.h"

namespace {

TEST(PubKeyLadder, ABibleIsAlwaysTheSameKeyRegardlessOfLanguageOrPath) {
  study::PubKeyInputs es{};
  es.isBible = true;
  es.canonVerified = true;
  es.bookPath = "/books/Traduccion del Nuevo Mundo (nwt-S).epub";

  study::PubKeyInputs en{};
  en.isBible = true;
  en.canonVerified = true;
  en.bookPath = "/books/New World Translation (nwt-E).epub";

  EXPECT_EQ(study::resolvePubKey(es), "bible");
  EXPECT_EQ(study::resolvePubKey(en), "bible")
      << "Salmos 119:145 and Psalm 119:145 are one verse; language is a rendering choice";
}

TEST(PubKeyLadder, ARegisteredPublicationUsesItsSymbolIssueAndLanguage) {
  study::PubKeyInputs in{};
  in.bookPath = "/books/La Atalaya (ed. estudio) 2026-07.epub";
  in.registered = study::RegisteredPub{"w", "202607", "S"};
  EXPECT_EQ(study::resolvePubKey(in), "w-202607-S");
}

TEST(PubKeyLadder, ARegisteredPublicationWithNoIssueOmitsIt) {
  study::PubKeyInputs in{};
  in.bookPath = "/books/Disfruta de la vida (lff-S).epub";
  in.registered = study::RegisteredPub{"lff", "", "S"};
  EXPECT_EQ(study::resolvePubKey(in), "lff-S");
}

TEST(PubKeyLadder, TheBibleFlagOutranksTheRegistry) {
  study::PubKeyInputs in{};
  in.isBible = true;
  in.canonVerified = true;
  in.registered = study::RegisteredPub{"nwt", "", "S"};
  in.bookPath = "/books/nwt.epub";
  EXPECT_EQ(study::resolvePubKey(in), "bible");
}

TEST(PubKeyLadder, ABibleWithAnUnverifiedCanonDoesNotShareTheGlobalKey) {
  study::PubKeyInputs in{};
  in.isBible = true;
  in.canonVerified = false;
  in.registered = study::RegisteredPub{"byz", "", "E"};
  in.bookPath = "/books/other.epub";
  EXPECT_NE(study::resolvePubKey(in), "bible")
      << "a different canon shifts every book number and lands marks in the wrong book";
}

TEST(PubKeyLadder, AnUnregisteredBookFallsBackToItsFlattenedPath) {
  study::PubKeyInputs in{};
  in.bookPath = "/books/Some Sideloaded Book.epub";
  EXPECT_EQ(study::resolvePubKey(in), "local-books_Some Sideloaded Book");
}

TEST(PubKeyLadder, AFallbackKeyIsAnnouncedAsUnstable) {
  study::PubKeyInputs in{};
  in.bookPath = "/books/Some Sideloaded Book.epub";
  EXPECT_FALSE(study::pubKeyIsStable(study::resolvePubKey(in)))
      << "a local- key is path-derived and dies when the file moves; the UI must be able to say so";

  study::PubKeyInputs bible{};
  bible.isBible = true;
  bible.canonVerified = true;
  EXPECT_TRUE(study::pubKeyIsStable(study::resolvePubKey(bible)));
}

TEST(PubKeySanitise, StripsSeparatorsThatWouldEscapeTheStoreDirectory) {
  study::PubKeyInputs in{};
  in.registered = study::RegisteredPub{"../../etc", "202607", "S"};
  const std::string key = study::resolvePubKey(in);
  EXPECT_EQ(key.find('/'), std::string::npos);
  EXPECT_EQ(key.find(".."), std::string::npos);
}

}  // namespace
