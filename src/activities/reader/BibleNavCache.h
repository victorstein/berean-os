#pragma once

#include <cstdint>
#include <cstring>

// The Bible navigator's limits, shared with the reader's cache of what it resolved.
namespace BibleNavLimits {
constexpr int MAX_BOOKS = 66;
constexpr int MAX_CHAPTERS = 150;  // Psalms
// Sized in UTF-8 bytes; see BibleBookNameTable::NAME_BYTES.
constexpr int BOOK_NAME_BYTES = 48;
constexpr int BOOK_ABBREV_BYTES = 16;
}  // namespace BibleNavLimits

// What the Bible navigator read from SD, kept by the reader so a second Go to reads none of it.
// Only a successful load is kept. Nothing invalidates it during a reader's life: the book never
// changes under a live reader. The navigator reads and writes it on the loop task only.
//
// A template over the book table so the host suite can exercise the logic without Epub
// (test/number_grid); the firmware instantiates it once, in BibleBookIndex.h.
template <typename Books>
class BibleNavCacheOf {
 public:
  bool hasBooks() const { return booksStored; }
  const Books* books() const { return booksStored ? &storedBooks : nullptr; }

  // Filled in place because a book table is far too large for a task stack. Withdraws any stored
  // copy until commitBooks().
  Books& booksToFill() {
    booksStored = false;
    return storedBooks;
  }
  void commitBooks() { booksStored = storedBooks.bookCount > 0; }

  // The stored chapter spines when `book` is the book they were stored for, else nullptr.
  const int16_t* chaptersFor(const int book, int& count) const {
    if (chapterBook < 0 || book != chapterBook) {
      count = 0;
      return nullptr;
    }
    count = chapterCount;
    return chapterSpine;
  }

  // Replaces the stored chapters. Only the book last opened is kept: that is the one the next
  // Go to most likely enters at.
  void storeChapters(const int book, const int16_t* spines, const int count) {
    if (book < 0 || spines == nullptr || count <= 0 || count > BibleNavLimits::MAX_CHAPTERS) return;
    memcpy(chapterSpine, spines, static_cast<size_t>(count) * sizeof(int16_t));
    chapterCount = count;
    chapterBook = book;
  }

 private:
  Books storedBooks{};
  bool booksStored = false;
  int chapterBook = -1;
  int chapterCount = 0;
  int16_t chapterSpine[BibleNavLimits::MAX_CHAPTERS] = {};
};
