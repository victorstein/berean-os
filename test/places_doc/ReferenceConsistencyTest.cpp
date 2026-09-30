// One place, formatted the way each surface formats it, must read the same everywhere: the reader
// title, a stored place (Home), a Recent chip, the search "Go to" row and a search hit row.
//
// Every surface is handed the same book name here, so this guards the chapter derivation and
// the numbering and joining only. Agreement between the two book-name paths (the covering TOC
// entry, and BibleBookNameTable's nav join) is pinned by test/bible_book_join and by the spec's
// NWT measurement (docs/superpowers/specs/2026-09-30-issue-225-design.md, Appendix A).

#include <BibleChapterNumber.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <string>

#include "activities/reader/TypedReference.h"
#include "util/BibleReference.h"
#include "util/PlacesDoc.h"

namespace {

using BibleReference::Verses;

struct Book {
  uint8_t number;
  const char* name;
  const char* abbreviation;
};

// Synthetic markup in the shape the NWT uses; no publisher text (this repository is public).
std::string verse(const int chapter, const int verseNumber) {
  return "<span id=\"chapter" + std::to_string(chapter) + "_verse" + std::to_string(verseNumber) +
         "\"></span><strong><sup>" + std::to_string(verseNumber) + "</sup></strong> text ";
}

std::string chapterDoc(const int chapter) {
  return "<html><body><p>" + verse(chapter, 30) + verse(chapter, 31) + "</p></body></html>";
}

// The mappings the reader, BibleSearchActivity::runSearch and ::ensureRows use; those files pull
// Arduino and cannot be linked here.
std::string readerTitle(const char* name, const int chapter) {
  return BibleReference::format(name, Verses{static_cast<uint16_t>(std::max(chapter, 0))});
}

std::string goToRow(const char* name, const TypedReference& ref) {
  char place[64];
  BibleReference::format(place, sizeof(place), name, Verses{ref.chapter, ref.verse, ref.verseEnd});
  return place;
}

std::string hitRow(const char* name, const BibleSearch::VerseEntry& entry) {
  char reference[64];
  BibleReference::format(reference, sizeof(reference), name, Verses{entry.chapter, entry.verse});
  return reference;
}

void expectOnePlaceReadsTheSameEverywhere(const Book& book) {
  const std::string doc = chapterDoc(40);

  const int readerChapter = BibleChapterNumber::scan(doc.data(), doc.size());
  study::DocumentUnits units = study::scanUnits(doc.data(), doc.size());
  units.book = book.number;
  ASSERT_EQ(units.anchors.size(), 2u);
  const auto placeUnit = PlacesDoc::placeUnit(units, units.anchors[1].offset);
  ASSERT_TRUE(placeUnit.has_value());
  ASSERT_FALSE(placeUnit->chapterOnly);
  EXPECT_EQ(readerChapter, placeUnit->unit.major);
  EXPECT_EQ(placeUnit->unit.minor, 31);

  Place place;
  place.unit = placeUnit->unit;
  place.chapterOnly = false;
  place.reference = PlacesDoc::formatReference(book.name, place.unit, false);

  const std::string expected = book.name[0] == '\0' ? "40:31" : std::string(book.name) + " 40:31";
  EXPECT_EQ(place.reference, expected);

  TypedReference typed;
  typed.book = book.number;
  typed.chapter = 40;
  typed.verse = 31;
  EXPECT_EQ(goToRow(book.name, typed), place.reference);

  BibleSearch::VerseEntry entry;
  entry.book = book.number;
  entry.chapter = 40;
  entry.verse = 31;
  EXPECT_EQ(hitRow(book.name, entry), place.reference);

  const std::string chapterPlace = PlacesDoc::formatReference(book.name, place.unit, true);
  EXPECT_EQ(readerTitle(book.name, readerChapter), chapterPlace);
  EXPECT_EQ(place.reference.rfind(chapterPlace, 0), 0u);

  char chip[PlacesDoc::MAX_REFERENCE_BYTES + 1];
  PlacesDoc::formatChipLabel(book.abbreviation, place, chip, sizeof(chip));
  const std::string expectedChip =
      book.abbreviation[0] == '\0' ? place.reference : std::string(book.abbreviation) + " 40:31";
  EXPECT_EQ(std::string(chip), expectedChip);

  PlacesDoc::formatChipLabel("", place, chip, sizeof(chip));
  EXPECT_EQ(std::string(chip), place.reference);
}

}  // namespace

TEST(ReferenceConsistency, IsaiahReadsTheSameEverywhere) {
  expectOnePlaceReadsTheSameEverywhere({23,
                                        "Isa\xC3\xAD"
                                        "as",
                                        "Is."});
}

TEST(ReferenceConsistency, ALongNameReadsTheSameEverywhere) {
  expectOnePlaceReadsTheSameEverywhere({22, "El Cantar de los Cantares", "Cant."});
}

TEST(ReferenceConsistency, AMissingNameReadsTheSameEverywhere) { expectOnePlaceReadsTheSameEverywhere({23, "", ""}); }

TEST(ReferenceConsistency, AnUnknownChapterLeavesTheTitleAtTheBook) { EXPECT_EQ(readerTitle("Isaiah", -1), "Isaiah"); }
