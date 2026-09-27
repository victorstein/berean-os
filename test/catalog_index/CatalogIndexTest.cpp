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
  const int version = catalog::parseHeader(index).version;
  size_t cursor = catalog::recordsBegin(index);
  catalog::Entry e;
  while (catalog::nextEntry(index, version, cursor, e)) out.push_back(e);
  return out;
}

// Three states in one index: probed with an EPUB, probed without, not yet probed.
const std::string kIndexV2 =
    "berean-catalog\t2\tS\tid\t2026-10-05\n"
    "g\t19800422\t1980\tperiodical\t1\t¡Despertad! 1980\n"
    "km\t198001\t1980\tperiodical\t0\tNuestro Servicio del Reino 1980\n"
    "lff\t\t2021\tbook\t\tDisfrute de la vida para siempre\n";

struct Visited {
  std::vector<std::string> symbols;
  std::vector<std::string> titles;
};

void collect(void* ctx, const catalog::Entry& entry) {
  auto* visited = static_cast<Visited*>(ctx);
  visited->symbols.emplace_back(entry.symbol);
  visited->titles.emplace_back(entry.title);
}

Visited searchAll(const std::string& index, const char* query, const size_t maxResults, bool& truncated) {
  Visited visited;
  catalog::search(index, query, maxResults, truncated, &collect, &visited);
  return visited;
}

TEST(CatalogHeader, ParsesTheProvenanceFields) {
  const auto h = catalog::parseHeader(kIndex);
  ASSERT_TRUE(h.valid());
  EXPECT_EQ(h.version, 1);
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
  while (catalog::nextEntry(big, 1, cursor, e)) {
    if (catalog::matches(e, "atalaya 3767")) hits++;
  }
  EXPECT_EQ(hits, 1u) << "the real Spanish catalog is 3,768 rows";
}

TEST(CatalogRecordsV2, ReadsTheFlagInAllThreeStates) {
  const auto entries = allEntries(kIndexV2);
  ASSERT_EQ(entries.size(), 3u);
  EXPECT_EQ(entries[0].epub, catalog::EpubAvailability::Available);
  EXPECT_EQ(entries[1].epub, catalog::EpubAvailability::Unavailable);
  EXPECT_EQ(entries[2].epub, catalog::EpubAvailability::Unknown) << "an unprobed row is listed, not hidden";
  EXPECT_EQ(entries[0].title, "¡Despertad! 1980") << "the flag must not leak into the title";
  EXPECT_EQ(entries[2].year, "2021");
}

TEST(CatalogRecordsV2, ATitleContainingATabStaysWhole) {
  const std::string index = "berean-catalog\t2\tS\tid\t2026-10-05\nw\t202607\t2026\tperiodical\t1\tLa Atalaya\tedicion\n";
  const auto entries = allEntries(index);
  ASSERT_EQ(entries.size(), 1u);
  EXPECT_EQ(entries[0].title, "La Atalaya\tedicion");
}

TEST(CatalogRecordsV2, AV1RecordHasNoFlag) {
  EXPECT_EQ(allEntries(kIndex)[0].epub, catalog::EpubAvailability::Unknown)
      << "an index built before the flag existed keeps showing everything";
}

TEST(CatalogRecordsV2, AV2LineMissingItsFlagIsSkipped) {
  const std::string index =
      "berean-catalog\t2\tS\tid\t2026-10-05\n"
      "w\t202607\t2026\tperiodical\tLa Atalaya\n"
      "lff\t\t2021\tbook\t1\tDisfrute de la vida\n";
  const auto entries = allEntries(index);
  ASSERT_EQ(entries.size(), 1u);
  EXPECT_EQ(entries[0].symbol, "lff");
}

TEST(CatalogRecordsV2, AnUnknownVersionYieldsNoRecords) {
  size_t cursor = catalog::recordsBegin(kIndexV2);
  catalog::Entry e;
  EXPECT_FALSE(catalog::nextEntry(kIndexV2, catalog::FORMAT_VERSION + 1, cursor, e));
  cursor = catalog::recordsBegin(kIndexV2);
  EXPECT_FALSE(catalog::nextEntry(kIndexV2, 0, cursor, e));
}

TEST(CatalogListable, OnlyAKnownMissingEpubIsHidden) {
  catalog::Entry e;
  e.epub = catalog::EpubAvailability::Available;
  EXPECT_TRUE(catalog::listable(e));
  e.epub = catalog::EpubAvailability::Unknown;
  EXPECT_TRUE(catalog::listable(e));
  e.epub = catalog::EpubAvailability::Unavailable;
  EXPECT_FALSE(catalog::listable(e));
}

TEST(CatalogSearchScan, FlaggedEntriesAreNeverListed) {
  bool truncated = true;
  const auto visited = searchAll(kIndexV2, "1980", 64, truncated);
  ASSERT_EQ(visited.symbols.size(), 1u);
  EXPECT_EQ(visited.symbols[0], "g");
  EXPECT_FALSE(truncated);
}

TEST(CatalogSearchScan, HiddenRowsDoNotUseTheCap) {
  const std::string index =
      "berean-catalog\t2\tS\tid\t2026-10-05\n"
      "km\t198001\t1980\tperiodical\t0\tNuestro Servicio del Reino 1980\n"
      "km\t198002\t1980\tperiodical\t0\tNuestro Servicio del Reino 1980\n"
      "km\t198003\t1980\tperiodical\t0\tNuestro Servicio del Reino 1980\n"
      "g\t19800108\t1980\tperiodical\t1\t¡Despertad! 1980\n"
      "g\t19800122\t1980\tperiodical\t\t¡Despertad! 1980\n";
  bool truncated = true;
  auto visited = searchAll(index, "1980", 2, truncated);
  EXPECT_EQ(visited.symbols, (std::vector<std::string>{"g", "g"}));
  EXPECT_FALSE(truncated) << "the three hidden rows must not count against the cap";

  visited = searchAll(index, "1980", 1, truncated);
  EXPECT_EQ(visited.symbols.size(), 1u);
  EXPECT_TRUE(truncated);
}

TEST(CatalogSearchScan, EachBufferIsReadByItsOwnHeader) {
  // Buscar scans again straight after installing a different index; a version
  // remembered from the old buffer would read v2 as v1 and leak the flag into
  // every title.
  bool truncated = false;
  auto visited = searchAll(kIndex, "atalaya", 64, truncated);
  EXPECT_EQ(visited.symbols, (std::vector<std::string>{"w"}));

  visited = searchAll(kIndexV2, "despertad", 64, truncated);
  ASSERT_EQ(visited.titles.size(), 1u);
  EXPECT_EQ(visited.titles[0], "¡Despertad! 1980");
}

TEST(CatalogSearchScan, AnUnreadableVersionVisitsNothing) {
  bool truncated = true;
  const auto visited =
      searchAll("berean-catalog\t3\tS\tid\t2026-10-05\ng\t19800422\t1980\tperiodical\t1\t\tx 1980\n", "1980", 64,
                truncated);
  EXPECT_TRUE(visited.symbols.empty());
  EXPECT_FALSE(truncated);
  EXPECT_TRUE(searchAll("no header at all\n", "no", 64, truncated).symbols.empty());
}

TEST(CatalogSearchScan, AnEmptyQueryVisitsNothing) {
  bool truncated = true;
  EXPECT_TRUE(searchAll(kIndexV2, "", 64, truncated).symbols.empty());
}

}  // namespace
