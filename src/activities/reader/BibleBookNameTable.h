#pragma once

#include <Epub.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

class GfxRenderer;

// Copies `source` into `dest`, cut on a UTF-8 boundary so drawText never sees
// an incomplete sequence (which renders as a replacement character).
void copyUtf8Truncated(char* dest, size_t destBytes, std::string_view source);

// Full book names in canonical order (book 1 at index 0), taken from the TOC
// entry each biblebooknav.xhtml link resolves to.
class BibleBookNameTable {
 public:
  static constexpr int MAX_BOOKS = 66;
  // Sized in UTF-8 BYTES, not characters. The longest joined TOC name measured
  // across the shipped publications is "El Cantar de los Cantares" at 25 B, and
  // Cyrillic/Greek renderings of the same books run to ~42 B.
  static constexpr int NAME_BYTES = 48;

  // Streams the book-nav page and joins its links to the TOC. Slow: it can
  // inflate the page from the EPUB, so it runs on the loop task.
  bool load(const std::shared_ptr<Epub>& epub, GfxRenderer& renderer);
  // The TOC join alone, for a caller that has already scanned the book-nav
  // page. A book with no matching TOC entry keeps an empty name.
  void joinToc(const Epub& epub, const std::string* targets, int targetCount);

  int count() const { return bookCount; }
  // Name at a 0-based book-nav index, or "" out of range.
  const char* at(int index) const;
  // Name of a 1-based canonical book number, or "" out of range.
  const char* forBook(uint8_t book) const { return at(static_cast<int>(book) - 1); }

 private:
  char names[MAX_BOOKS][NAME_BYTES] = {};
  int bookCount = 0;
};
