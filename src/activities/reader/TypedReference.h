#pragma once

#include <BibleSearch/IndexReader.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

// A Bible reference typed into verse search: "Isa 40:31", "Gén 1",
// "Juan 3:16-18". Book names come from the publication, never from a table
// here. Free of Arduino and the Epub library so test/ui_layout can exercise it.

struct TypedReference {
  uint8_t book = 0;  // canonical 1-66; 0 = not a reference
  uint8_t chapter = 0;
  uint8_t verse = 0;     // 0 = the whole chapter
  uint8_t verseEnd = 0;  // 0 = no range

  bool valid() const { return book != 0; }
};

// Per-book names laid out as fixed-stride rows, as BibleBookNameTable stores
// them: row i is canonical book i + 1, each NUL-terminated, possibly empty. A
// stride view costs no pointer array on the caller's stack.
struct BookNameSource {
  const char* names = nullptr;
  size_t nameStride = 0;
  const char* abbreviations = nullptr;
  size_t abbreviationStride = 0;
  int count = 0;
};

// `<book> <chapter>[:<verse>[-<verse>]]`, the book matched against the full
// names and abbreviations with search's fold. Returns an invalid reference for
// anything else, including a book that more than one name could mean.
TypedReference parseTypedReference(std::string_view query, const BookNameSource& books);

// "Isaías 40:31", "Génesis 1", "Juan 3:16-18"; "40:31" when `bookName` is
// empty, as the search hit rows fall back.
void formatTypedReference(char* out, size_t outBytes, const char* bookName, const TypedReference& ref);

struct ResolvedReference {
  bool found = false;
  // As typed, except a single-chapter book's lone number, which becomes its
  // verse: "Jude 3" resolves to Jude 1:3.
  TypedReference reference;
  uint16_t spine = 0;
  std::optional<uint32_t> offset;  // empty for a whole chapter: open at its top
};

// Looks `ref` up in the index's verse table. A place the index lacks is
// `found == false`; the return is false only when the index cannot be read.
bool resolveTypedReference(const BibleSearch::IndexReader& reader, const TypedReference& ref, ResolvedReference& out);
