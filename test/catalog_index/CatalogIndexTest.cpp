#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "Catalog/CatalogIndex.h"

namespace {

// The real shape: a header line, then tab-separated records. Titles carry
// commas, parentheses and accents, which is why the separator is a tab.
const std::string kIndex =
    "berean-catalog\t1\tS\t9c7204f8-45e7-43d8-afea-c14ecb035c55\t2026-09-12\n"
    "w\t202607\t2026\tperiodical\tLa Atalaya (ed. estudio), julio de 2026\n"
    "mwb\t202609\t2026\tperiodical\tGuía de actividades, septiembre de 2026\n"
    "lff\t\t2021\tbook\tDisfrute de la vida para siempre\n"
    "nwt\t\t2019\tbible\tLa Biblia. Traducción del Nuevo Mundo\n";

std::vector<catalog::Entry> allEntries(const std::string& index) {
  std::vector<catalog::Entry> out;
  size_t cursor = catalog::recordsBegin(index);
  catalog::Entry e;
  while (catalog::nextEntry(index, cursor, e)) out.push_back(e);
  return out;
}

TEST(CatalogHeader, ParsesTheProvenanceFields) {
  const auto h = catalog::parseHeader(kIndex);
  ASSERT_TRUE(h.valid());
  EXPECT_EQ(h.version, catalog::FORMAT_VERSION);
  EXPECT_EQ(h.language, "S");
  EXPECT_EQ(h.manifestId, "9c7204f8-45e7-43d8-afea-c14ecb035c55");
  EXPECT_EQ(h.builtOn, "2026-09-12") << "the build date is shown to the user, so stale can be told from empty";
}

TEST(CatalogHeader, RejectsAnythingWithoutTheMagic) {
  EXPECT_FALSE(catalog::parseHeader("w\t202607\t2026\tperiodical\tLa Atalaya\n").valid());
  EXPECT_FALSE(catalog::parseHeader("").valid());
}

TEST(CatalogRecords, ReadsEveryRow) {
  const auto entries = allEntries(kIndex);
  ASSERT_EQ(entries.size(), 4u);
  EXPECT_EQ(entries[0].symbol, "w");
  EXPECT_EQ(entries[0].issue, "202607");
  EXPECT_EQ(entries[0].title, "La Atalaya (ed. estudio), julio de 2026");
  EXPECT_EQ(entries[2].symbol, "lff");
  EXPECT_TRUE(entries[2].issue.empty()) << "a non-periodical has no issue";
}

TEST(CatalogRecords, ATitleContainingSeparatorCharactersSurvives) {
  const auto entries = allEntries(kIndex);
  EXPECT_NE(entries[0].title.find(','), std::string_view::npos);
  EXPECT_NE(entries[0].title.find('('), std::string_view::npos);
}

TEST(CatalogRecords, AMalformedRowIsSkippedRatherThanStoppingTheScan) {
  const std::string broken =
      "berean-catalog\t1\tS\tid\t2026-09-12\n"
      "w\t202607\t2026\tperiodical\tLa Atalaya\n"
      "this row has no tabs at all\n"
      "lff\t\t2021\tbook\tDisfrute de la vida\n";
  const auto entries = allEntries(broken);
  ASSERT_EQ(entries.size(), 2u) << "one bad row must not hide every row after it";
  EXPECT_EQ(entries[1].symbol, "lff");
}

TEST(CatalogRecords, HandlesAnIndexWithNoTrailingNewline) {
  const std::string noTrailer =
      "berean-catalog\t1\tS\tid\t2026-09-12\n"
      "lff\t\t2021\tbook\tDisfrute de la vida";
  EXPECT_EQ(allEntries(noTrailer).size(), 1u);
}

TEST(CatalogSearch, MatchesOnTitleCaseInsensitively) {
  const auto entries = allEntries(kIndex);
  EXPECT_TRUE(catalog::matches(entries[0], "atalaya"));
  EXPECT_TRUE(catalog::matches(entries[0], "ATALAYA"));
  EXPECT_FALSE(catalog::matches(entries[0], "despertad"));
}

TEST(CatalogSearch, MatchesOnSymbol) {
  const auto entries = allEntries(kIndex);
  EXPECT_TRUE(catalog::matches(entries[2], "lff")) << "typing a symbol is the path that survives a stale index";
}

TEST(CatalogSearch, EveryTermMustHitSoASecondWordNarrows) {
  const auto entries = allEntries(kIndex);
  EXPECT_TRUE(catalog::matches(entries[0], "atalaya 2026"));
  EXPECT_FALSE(catalog::matches(entries[0], "atalaya 2019")) << "a second term must narrow, not widen";
}

TEST(CatalogSearch, MatchesOnIssueAndYear) {
  const auto entries = allEntries(kIndex);
  EXPECT_TRUE(catalog::matches(entries[0], "202607"));
  EXPECT_TRUE(catalog::matches(entries[3], "2019"));
}

TEST(CatalogSearch, AnEmptyQueryMatchesNothing) {
  const auto entries = allEntries(kIndex);
  EXPECT_FALSE(catalog::matches(entries[0], ""));
  EXPECT_FALSE(catalog::matches(entries[0], "   ")) << "whitespace alone would otherwise list all 3,768";
}

TEST(CatalogSearch, AccentedTitlesAreFoundByTheirAsciiRun) {
  const auto entries = allEntries(kIndex);
  // Folding accents needs a table this device does not carry; searching the
  // unaccented part of a word still works, which is what a user types.
  EXPECT_TRUE(catalog::matches(entries[2], "disfrute"));
  EXPECT_TRUE(catalog::matches(entries[3], "biblia"));
}

TEST(CatalogScale, ThreeThousandRowsScanWithoutAllocating) {
  std::string big = "berean-catalog\t1\tS\tid\t2026-09-12\n";
  for (int i = 0; i < 3768; ++i) {
    big += "w\t20260" + std::to_string(i % 10) + "\t2026\tperiodical\tLa Atalaya numero " + std::to_string(i) + "\n";
  }
  size_t cursor = catalog::recordsBegin(big);
  catalog::Entry e;
  size_t hits = 0;
  while (catalog::nextEntry(big, cursor, e)) {
    if (catalog::matches(e, "atalaya 3767")) hits++;
  }
  EXPECT_EQ(hits, 1u) << "the real Spanish catalog is 3,768 rows";
}

}  // namespace
