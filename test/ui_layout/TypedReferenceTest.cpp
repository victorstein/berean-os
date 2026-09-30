#include <gtest/gtest.h>

#include <cstdio>
#include <cstring>

#include "activities/reader/TypedReference.h"

namespace {

// Book names and nav abbreviations laid out as BibleBookNameTable stores them.
// Short name lists only: this repository is public, so no publisher text.
struct Books {
  static constexpr int COUNT = 66;
  static constexpr size_t NAME_BYTES = 48;
  static constexpr size_t ABBREV_BYTES = 16;
  char names[COUNT][NAME_BYTES] = {};
  char abbreviations[COUNT][ABBREV_BYTES] = {};

  Books& add(const int book, const char* name, const char* abbreviation) {
    snprintf(names[book - 1], NAME_BYTES, "%s", name);
    snprintf(abbreviations[book - 1], ABBREV_BYTES, "%s", abbreviation);
    return *this;
  }

  BookNameSource source() const {
    BookNameSource s;
    s.names = &names[0][0];
    s.nameStride = NAME_BYTES;
    s.abbreviations = &abbreviations[0][0];
    s.abbreviationStride = ABBREV_BYTES;
    s.count = COUNT;
    return s;
  }
};

const Books& spanish() {
  static const Books books = [] {
    Books b;
    b.add(1, "G\xC3\xA9nesis", "G\xC3\xA9n.")
        .add(2, "\xC3\x89xodo", "\xC3\x89x.")
        .add(5, "Deuteronomio", "Deut.")
        .add(7, "Jueces", "Juec.")
        .add(13, "1 Cr\xC3\xB3nicas", "1 Cr\xC3\xB3n.")
        .add(19, "Salmos", "Sal.")
        .add(22, "El Cantar de los Cantares", "Cant.")
        .add(23, "Isa\xC3\xAD" "as", "Is.")
        .add(25, "Lamentaciones", "Lam.")
        .add(30, "Am\xC3\xB3s", "Am\xC3\xB3s")
        .add(40, "Mateo", "Mat.")
        .add(43, "Juan", "Juan")
        .add(62, "1 Juan", "1 Juan")
        .add(65, "Judas", "Jud.")
        .add(66, "Apocalipsis", "Apoc.");
    return b;
  }();
  return books;
}

const Books& english() {
  static const Books books = [] {
    Books b;
    b.add(1, "Genesis", "Gen.")
        .add(5, "Deuteronomy", "Deut.")
        .add(7, "Judges", "Judg.")
        .add(19, "Psalms", "Ps.")
        .add(23, "Isaiah", "Isa.")
        .add(25, "Lamentations", "Lam.")
        .add(43, "John", "John")
        .add(50, "Philippians", "Phil.")
        .add(57, "Philemon", "Philem.")
        .add(62, "1 John", "1 John")
        .add(65, "Jude", "Jude")
        .add(66, "Revelation", "Rev.");
    return b;
  }();
  return books;
}

TypedReference parse(const char* query, const Books& books) { return parseTypedReference(query, books.source()); }

void expectReference(const TypedReference& r, const int book, const int chapter, const int verse = 0,
                     const int verseEnd = 0) {
  EXPECT_EQ(static_cast<int>(r.book), book);
  EXPECT_EQ(static_cast<int>(r.chapter), chapter);
  EXPECT_EQ(static_cast<int>(r.verse), verse);
  EXPECT_EQ(static_cast<int>(r.verseEnd), verseEnd);
}

}  // namespace

TEST(TypedReferenceParse, FullNames) {
  expectReference(parse("Juan 3:16", spanish()), 43, 3, 16);
  expectReference(parse("Isa\xC3\xAD" "as 40:31", spanish()), 23, 40, 31);
  expectReference(parse("El Cantar de los Cantares 2:1", spanish()), 22, 2, 1);
  expectReference(parse("Isaiah 40:31", english()), 23, 40, 31);
  expectReference(parse("1 John 4:8", english()), 62, 4, 8);
}

TEST(TypedReferenceParse, PublicationAbbreviations) {
  expectReference(parse("Isa 40:31", english()), 23, 40, 31);
  expectReference(parse("G\xC3\xA9n 1", spanish()), 1, 1);
  expectReference(parse("Gen. 1", spanish()), 1, 1);
  expectReference(parse("1 Cr\xC3\xB3n 29:11", spanish()), 13, 29, 11);
  expectReference(parse("1Cron 29:11", spanish()), 13, 29, 11);
  expectReference(parse("Jud 5", spanish()), 65, 5);
  expectReference(parse("Is 40:31", spanish()), 23, 40, 31);
  expectReference(parse("Ps 23", english()), 19, 23);
}

TEST(TypedReferenceParse, IgnoresCaseAndAccents) {
  expectReference(parse("g\xC3\xA9n 1", spanish()), 1, 1);
  expectReference(parse("GEN 1", spanish()), 1, 1);
  expectReference(parse("Exodo 3", spanish()), 2, 3);
  expectReference(parse("\xC3\x89xodo 3", spanish()), 2, 3);
}

TEST(TypedReferenceParse, NonAsciiSpacesInANameAreIgnored) {
  Books nbsp;
  nbsp.add(62, "1\xC2\xA0Juan", "1\xE2\x80\xAFJuan");
  expectReference(parse("1 Juan 4:8", nbsp), 62, 4, 8);
  expectReference(parse("1Juan 4:8", nbsp), 62, 4, 8);
}

TEST(TypedReferenceParse, ChapterVerseAndRange) {
  expectReference(parse("Gen 1", english()), 1, 1);
  expectReference(parse("Gen 1:1", english()), 1, 1, 1);
  expectReference(parse("Juan 3:16-18", spanish()), 43, 3, 16, 18);
  expectReference(parse("Juan 3:16 - 18", spanish()), 43, 3, 16, 18);
  expectReference(parse("Juan 3:16\xE2\x80\x93" "18", spanish()), 43, 3, 16, 18);
  expectReference(parse("  Juan 3 : 16  ", spanish()), 43, 3, 16);
}

TEST(TypedReferenceParse, ExactMatchBeatsAPrefixOfAnotherBook) {
  expectReference(parse("Phil 4:13", english()), 50, 4, 13);
  expectReference(parse("Philem 1", english()), 57, 1);
}
