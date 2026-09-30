#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

// The one way this firmware writes a Bible reference: "Isaías 40:31".
//
// The caller supplies the book name. A full name is the publication's TOC title for the book:
// the TOC entry covering the spine item in the reader, or BibleBookNameTable::forBook by book
// number in search. The two paths are measured to agree on every NWT book. The short form is the
// publication's own abbreviation, BibleBookNameTable::abbreviationFor, never a truncated name or
// a built-in table. Every number comes from the chapter<N>_verse<M> markers VerseAnchors parses.
//
// Free of Arduino and the Epub library so the host tests can exercise it.
namespace BibleReference {

// chapter 0: the book name alone. verse 0: the whole chapter. verseEnd 0: a single verse.
struct Verses {
  uint16_t chapter = 0;
  uint16_t verse = 0;
  uint16_t verseEnd = 0;
};

// "65535:65535-65535" plus NUL.
inline constexpr size_t MAX_NUMBERS_BYTES = 18;

// "Isaías 40:31", "Génesis 1", "Juan 3:16-18"; "40:31" when `book` is empty; `book` alone when
// verses.chapter is 0. Cut on a UTF-8 boundary to fit `outBytes`, never leaving a partial
// sequence. No-op when outBytes is 0.
void format(char* out, size_t outBytes, std::string_view book, const Verses& verses);

// The same, uncapped, for labels drawn by width: the reader title and the status bar.
std::string format(std::string_view book, const Verses& verses);

}  // namespace BibleReference
