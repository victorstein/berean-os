#include <gtest/gtest.h>

#include <string>

#include "Catalog/CatalogLabel.h"

namespace {

// How the activity passes them: twelve words in one translated string, and the
// two printf formats straight from tr().
constexpr const char* kSpanishMonths =
    "enero febrero marzo abril mayo junio julio agosto septiembre octubre noviembre diciembre";
constexpr const char* kEnglishMonths =
    "January February March April May June July August September October November December";
constexpr const char* kSpanishDay = "%d de %s de %d";
constexpr const char* kSpanishMonth = "%s de %d";
constexpr const char* kEnglishDay = "%d %s %d";
constexpr const char* kEnglishMonth = "%s %d";

std::string spanish(const std::string& issue) {
  char out[48] = "";
  EXPECT_TRUE(catalog::formatIssueDate(issue, kSpanishMonths, kSpanishDay, kSpanishMonth, out, sizeof(out)));
  return out;
}

std::string english(const std::string& issue) {
  char out[48] = "";
  EXPECT_TRUE(catalog::formatIssueDate(issue, kEnglishMonths, kEnglishDay, kEnglishMonth, out, sizeof(out)));
  return out;
}

}  // namespace

TEST(CatalogIssueDate, ASemimonthlyIssueReadsAsADayInEachLanguage) {
  EXPECT_EQ(spanish("19800422"), "22 de abril de 1980") << "the month is a view mid-list; it must end at its word";
  EXPECT_EQ(english("19800422"), "22 April 1980");
}

TEST(CatalogIssueDate, TheLeadingZeroOnTheDayIsDropped) { EXPECT_EQ(spanish("19800108"), "8 de enero de 1980"); }

TEST(CatalogIssueDate, AMonthlyIssueReadsAsACapitalisedMonth) {
  EXPECT_EQ(spanish("198004"), "Abril de 1980");
  EXPECT_EQ(english("202607"), "July 2026");
  EXPECT_EQ(spanish("198012"), "Diciembre de 1980");
}

TEST(CatalogIssueDate, AMalformedCodeIsShownAsWritten) {
  for (const char* code : {"1980042", "19801322", "19800400", "19800432", "1980A4", "198013", "198000", "abcdefgh", "2026"}) {
    EXPECT_EQ(spanish(code), code);
  }
}

TEST(CatalogIssueDate, AShortMonthListFallsBackToTheRawCode) {
  char out[48] = "";
  ASSERT_TRUE(catalog::formatIssueDate("19800422", "enero febrero marzo", kSpanishDay, kSpanishMonth, out, sizeof(out)));
  EXPECT_STREQ(out, "19800422") << "a translation missing months must not print an empty month";
}

TEST(CatalogIssueDate, RefusesToOverflowTheCallersBuffer) {
  char out[4] = "";
  EXPECT_FALSE(catalog::formatIssueDate("19800422", kSpanishMonths, kSpanishDay, kSpanishMonth, out, sizeof(out)));
  EXPECT_FALSE(catalog::formatIssueDate("19800422", kSpanishMonths, kSpanishDay, kSpanishMonth, nullptr, 0));
}

TEST(CatalogDisplayTitle, APeriodicalDropsTheTrailingYear) {
  EXPECT_EQ(catalog::displayTitle("¡Despertad! 1980", "1980", true), "¡Despertad!");
  EXPECT_EQ(catalog::displayTitle("Nuestro Servicio del Reino 1980", "1980", true), "Nuestro Servicio del Reino");
}

TEST(CatalogDisplayTitle, OtherYearFormsAreLeftAlone) {
  EXPECT_EQ(catalog::displayTitle("Guía de actividades para la reunión Vida y Ministerio Cristianos (2016)", "2016", true),
            "Guía de actividades para la reunión Vida y Ministerio Cristianos (2016)");
  EXPECT_EQ(catalog::displayTitle("La Atalaya. Anunciando el Reino de Jehová 2013 (lenguaje sencillo)", "2013", true),
            "La Atalaya. Anunciando el Reino de Jehová 2013 (lenguaje sencillo)");
  EXPECT_EQ(catalog::displayTitle("¡Despertad! 1980", "1981", true), "¡Despertad! 1980");
  EXPECT_EQ(catalog::displayTitle("Revista1980", "1980", true), "Revista1980");
}

TEST(CatalogDisplayTitle, ABookKeepsItsWholeTitle) {
  EXPECT_EQ(catalog::displayTitle("Programa de la asamblea de circuito 2017", "2017", false),
            "Programa de la asamblea de circuito 2017");
}

TEST(CatalogDisplayTitle, ATitleThatIsOnlyTheYearIsKept) {
  EXPECT_EQ(catalog::displayTitle("1980", "1980", true), "1980");
  EXPECT_EQ(catalog::displayTitle(" 1980", "1980", true), " 1980");
}
