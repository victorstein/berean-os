#include <gtest/gtest.h>

#include <string>

#include "Catalog/CatalogStamp.h"

namespace {

// The Spanish abbreviations as one translated string, which is how the activity
// passes them: twelve words rather than twelve STR_ keys.
constexpr const char* kSpanishMonths = "ene feb mar abr may jun jul ago sep oct nov dic";
constexpr const char* kEnglishMonths = "Jan Feb Mar Apr May Jun Jul Aug Sep Oct Nov Dec";

const std::string kIndex = "berean-catalog\t1\tS\t9c7204f8-45e7-43d8-afea-c14ecb035c55\t2026-09-12\nw\t\t2026\tx\ty\n";

catalog::Stamp stamp(const std::string& language, const std::string& manifestId, const std::string& builtOn) {
  return catalog::Stamp{language, manifestId, builtOn};
}

TEST(CatalogStamp, CopiesTheHeaderOutOfTheBufferItPointsInto) {
  // The inflated buffer is freed when Buscar closes; the stamp must not be a
  // view into it.
  catalog::Stamp held;
  {
    const std::string scoped = kIndex;
    held = catalog::stampOf(catalog::parseHeader(scoped));
  }
  EXPECT_EQ(held.language, "S");
  EXPECT_EQ(held.manifestId, "9c7204f8-45e7-43d8-afea-c14ecb035c55");
  EXPECT_EQ(held.builtOn, "2026-09-12");
  EXPECT_TRUE(held.valid());
}

TEST(CatalogStamp, AnUnparseableHeaderYieldsAnInvalidStamp) {
  EXPECT_FALSE(catalog::stampOf(catalog::parseHeader("not an index at all\n")).valid());
}

TEST(CatalogStaleness, TheSameReleaseIsRecognised) {
  const auto a = stamp("S", "id-1", "2026-09-12");
  EXPECT_TRUE(catalog::sameRelease(a, stamp("S", "id-1", "2026-09-12")));
}

TEST(CatalogStaleness, ARebuildUnderTheSameManifestIdStillCountsAsNew) {
  // Measured against the live endpoint: the .gz behind an unchanged manifest id
  // came back newer at byte-identical length, so the id alone cannot decide.
  const auto held = stamp("S", "id-1", "2026-09-12");
  EXPECT_FALSE(catalog::sameRelease(held, stamp("S", "id-1", "2026-10-03")));
}

TEST(CatalogStaleness, ADifferentManifestIdIsNew) {
  const auto held = stamp("S", "id-1", "2026-09-12");
  EXPECT_FALSE(catalog::sameRelease(held, stamp("S", "id-2", "2026-09-12")));
}

TEST(CatalogStaleness, AnInvalidStampNeverCompareEqual) {
  const auto held = stamp("S", "id-1", "2026-09-12");
  EXPECT_FALSE(catalog::sameRelease(held, catalog::Stamp{}))
      << "an unreadable fetch must not be mistaken for 'already up to date'";
  EXPECT_FALSE(catalog::sameRelease(catalog::Stamp{}, held));
}

TEST(CatalogAcceptance, TheMatchingVersionAndLanguageIsAccepted) {
  EXPECT_TRUE(catalog::indexAcceptable(catalog::parseHeader(kIndex), "S"));
}

TEST(CatalogAcceptance, AFutureVersionIsRefusedRatherThanReinterpreted) {
  const std::string future = "berean-catalog\t2\tS\tid\t2026-09-12\nw\t\t2026\tx\ty\n";
  EXPECT_FALSE(catalog::indexAcceptable(catalog::parseHeader(future), "S"));
}

TEST(CatalogAcceptance, AnAssetForAnotherLanguageIsRefused) {
  const std::string english = "berean-catalog\t1\tE\tid\t2026-09-12\nw\t\t2026\tx\ty\n";
  EXPECT_FALSE(catalog::indexAcceptable(catalog::parseHeader(english), "S"))
      << "downloading catalog-E over catalog-S must not silently swap the language";
}

TEST(CatalogAcceptance, GarbageIsRefused) {
  EXPECT_FALSE(catalog::indexAcceptable(catalog::parseHeader("<!DOCTYPE html>\n"), "S"))
      << "a 404 page saved to the card is the realistic failure";
}

TEST(CatalogDate, RendersTheBuildDateInTheUsersLanguage) {
  char out[32] = "";
  ASSERT_TRUE(catalog::formatIndexDate("2026-09-12", kSpanishMonths, out, sizeof(out)));
  EXPECT_STREQ(out, "12 sep 2026");

  ASSERT_TRUE(catalog::formatIndexDate("2026-09-12", kEnglishMonths, out, sizeof(out)));
  EXPECT_STREQ(out, "12 Sep 2026");
}

TEST(CatalogDate, DropsTheLeadingZeroOnTheDay) {
  char out[32] = "";
  ASSERT_TRUE(catalog::formatIndexDate("2026-01-05", kSpanishMonths, out, sizeof(out)));
  EXPECT_STREQ(out, "5 ene 2026");
}

TEST(CatalogDate, HandlesBothEndsOfTheYear) {
  char out[32] = "";
  ASSERT_TRUE(catalog::formatIndexDate("2026-01-01", kSpanishMonths, out, sizeof(out)));
  EXPECT_STREQ(out, "1 ene 2026");
  ASSERT_TRUE(catalog::formatIndexDate("2026-12-31", kSpanishMonths, out, sizeof(out)));
  EXPECT_STREQ(out, "31 dic 2026");
}

TEST(CatalogDate, AnUnparseableDateIsShownAsWritten) {
  char out[32] = "";
  ASSERT_TRUE(catalog::formatIndexDate("next tuesday", kSpanishMonths, out, sizeof(out)));
  EXPECT_STREQ(out, "next tuesday") << "showing what the index actually says beats showing nothing";

  ASSERT_TRUE(catalog::formatIndexDate("2026-13-01", kSpanishMonths, out, sizeof(out)));
  EXPECT_STREQ(out, "2026-13-01") << "month 13 is not a month";

  ASSERT_TRUE(catalog::formatIndexDate("2026-0X-01", kSpanishMonths, out, sizeof(out)));
  EXPECT_STREQ(out, "2026-0X-01");
}

TEST(CatalogDate, AnEmptyDateProducesAnEmptyString) {
  char out[32] = "sentinel";
  ASSERT_TRUE(catalog::formatIndexDate("", kSpanishMonths, out, sizeof(out)));
  EXPECT_STREQ(out, "");
}

TEST(CatalogDate, AShortMonthListFallsBackToTheIsoDate) {
  char out[32] = "";
  ASSERT_TRUE(catalog::formatIndexDate("2026-09-12", "ene feb mar", out, sizeof(out)));
  EXPECT_STREQ(out, "2026-09-12") << "a translation missing months must not print an empty month";
}

TEST(CatalogDate, ExtraSpacesInTheMonthListAreIgnored) {
  char out[32] = "";
  ASSERT_TRUE(catalog::formatIndexDate("2026-02-03", "  ene   feb  mar abr may jun jul ago sep oct nov dic", out,
                                       sizeof(out)));
  EXPECT_STREQ(out, "3 feb 2026");
}

TEST(CatalogDate, RefusesToOverflowTheCallersBuffer) {
  char out[4] = "";
  EXPECT_FALSE(catalog::formatIndexDate("2026-09-12", kSpanishMonths, out, sizeof(out)));
  EXPECT_FALSE(catalog::formatIndexDate("2026-09-12", kSpanishMonths, nullptr, 0));
}

}  // namespace
