#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "Fold.h"

namespace {

std::string fold(std::string_view in) {
  std::string out;
  BibleSearch::foldAppend(out, in);
  return out;
}

void collect(void* ctx, std::string_view token) { static_cast<std::vector<std::string>*>(ctx)->emplace_back(token); }

std::vector<std::string> tokens(std::string_view folded) {
  std::vector<std::string> out;
  BibleSearch::tokenize(folded, collect, &out);
  return out;
}

using Tokens = std::vector<std::string>;

}  // namespace

TEST(BibleSearchFold, StripsSpanishDiacriticsAndLowerCases) {
  EXPECT_EQ(fold("Señor"), "senor");
  EXPECT_EQ(fold("corazón"), "corazon");
  EXPECT_EQ(fold("PINGÜINO"), "pinguino");
  EXPECT_EQ(fold("Ça"), "ca");
  EXPECT_EQ(fold("ÁÉÍÓÚÑ áéíóúñ"), "aeioun aeioun");
}

TEST(BibleSearchFold, FoldsMixedCase) { EXPECT_EQ(fold("JeHoVá DiOs"), "jehova dios"); }

TEST(BibleSearchFold, ExpandsLigaturesAndSharpS) {
  EXPECT_EQ(fold("Straße"), "strasse");
  EXPECT_EQ(fold("Æon Œuvre"), "aeon oeuvre");
}

TEST(BibleSearchFold, IsIdempotent) {
  const std::string once = fold("“¡Quiten todo esto de aquí!” Señor, PINGÜINO");
  EXPECT_EQ(fold(once), once);
}

TEST(BibleSearchFold, DropsCombiningMarksSoDecomposedTextFoldsLikePrecomposed) {
  EXPECT_EQ(fold("coraz\x6f\xcc\x81n"), "corazon") << "o + U+0301 must fold like ó";
}

TEST(BibleSearchFold, LeavesOtherScriptsUntouched) {
  // The choice: no case mapping outside Latin. The NWT this indexes is Spanish,
  // and a partial Greek or Cyrillic case table would fold some letters and not
  // others, which is worse than folding none.
  EXPECT_EQ(fold("ΑΓΑΠΗ ἀγάπη"), "ΑΓΑΠΗ ἀγάπη");
  EXPECT_EQ(fold("Любовь"), "Любовь");
}

TEST(BibleSearchFold, ReplacesMalformedUtf8WithASpace) {
  EXPECT_EQ(fold("ab\xff"
                 "cd"),
            "ab cd");
  EXPECT_EQ(fold("ab\xc3"), "ab ") << "a truncated sequence must not leak a lone lead byte";
}

TEST(BibleSearchTokenize, SplitsOnTypographicPunctuation) {
  EXPECT_EQ(tokens(fold("“¡Quiten todo esto de aquí!”")), (Tokens{"quiten", "todo", "esto", "de", "aqui"}));
  EXPECT_EQ(tokens(fold("«vino»—agua…‘sal’ ¿pan?")), (Tokens{"vino", "agua", "sal", "pan"}));
}

TEST(BibleSearchTokenize, SplitsOnNarrowAndNoBreakSpaces) {
  // The NWT markup carries U+202F and U+00A0 between words.
  EXPECT_EQ(tokens(fold("Juan\xe2\x80\xaf"
                        "dijo\xc2\xa0"
                        "esto")),
            (Tokens{"juan", "dijo", "esto"}));
}

TEST(BibleSearchTokenize, DropsOneLetterTokens) {
  EXPECT_EQ(tokens(fold("y a los que vendían")), (Tokens{"los", "que", "vendian"}));
}

TEST(BibleSearchTokenize, KeepsDigits) { EXPECT_EQ(tokens(fold("Tomó 46 años")), (Tokens{"tomo", "46", "anos"})); }

TEST(BibleSearchTokenize, CutsALongWordOnACodepointBoundary) {
  // 31 ASCII bytes then a 2-byte letter: a 32-byte cut would split it.
  const std::string word = std::string(31, 'x') + "Ω" + std::string(7, 'y');
  ASSERT_EQ(word.size(), 40u);
  const Tokens got = tokens(word);
  ASSERT_EQ(got.size(), 1u);
  EXPECT_LE(got[0].size(), BibleSearch::MAX_TOKEN_BYTES);
  EXPECT_EQ(got[0], std::string(31, 'x'));
}

TEST(BibleSearchTokenize, KeepsNonLatinLettersAsTokenCharacters) {
  EXPECT_EQ(tokens("ἀγάπη amor"), (Tokens{"ἀγάπη", "amor"}));
}

TEST(BibleSearchTokenize, EmptyAndPunctuationOnlyInputGiveNoTokens) {
  EXPECT_TRUE(tokens("").empty());
  EXPECT_TRUE(tokens(fold(" ¡¿…!? — ")).empty());
}

TEST(BibleSearchQueryTokens, FoldsAndSplitsAQuery) {
  EXPECT_EQ(BibleSearch::queryTokens("  amor   PACIENTE "), (Tokens{"amor", "paciente"}));
  EXPECT_EQ(BibleSearch::queryTokens("Señor"), (Tokens{"senor"}));
  EXPECT_TRUE(BibleSearch::queryTokens("  y ").empty());
}
