#pragma once

#include <cstdint>

// Where BibleNavigationActivity opens for the reader's spine item: the chapter
// grid of the book being read, that book selected on the book grid, or the
// unselected book grid. Free of FreeInkUI, Arduino and Epub so the host suite
// can exercise it (test/number_grid).
//
// The book is only a guess from spine order until the caller confirms the
// chapter against that book's resolved chapter list, so a publication laid out
// differently falls back instead of opening the wrong chapter.
namespace BibleEntry {

enum class Kind : uint8_t {
  None,
  // The spine is the book's own entry page: its chapter-nav page, or the only
  // chapter of a book too short to have one.
  SelectBook,
  // Load the book's chapter list, then look the spine up with chapterRowFor.
  LoadChapters,
};

struct Entry {
  Kind kind = Kind::None;
  int book = -1;
};

// Unresolved (< 0) targets never win, and neither sortedness nor uniqueness is
// assumed.
inline int bookFor(const int16_t* bookTargetSpine, const int bookCount, const int spine) {
  if (!bookTargetSpine || spine < 0) return -1;
  int best = -1;
  for (int i = 0; i < bookCount; i++) {
    const int target = bookTargetSpine[i];
    if (target < 0 || target > spine) continue;
    if (best < 0 || target > bookTargetSpine[best]) best = i;
  }
  return best;
}

inline Entry classify(const int16_t* bookTargetSpine, const bool* bookIsDirect, const int bookCount,
                      const int spine) {
  const int book = bookFor(bookTargetSpine, bookCount, spine);
  if (book < 0) return {};
  if (spine == bookTargetSpine[book]) return {Kind::SelectBook, book};
  // A single-chapter book has no chapter list to confirm against, and its one
  // chapter is its target, so anything past it is an outline or other page.
  if (bookIsDirect && bookIsDirect[book]) return {};
  return {Kind::LoadChapters, book};
}

inline int chapterRowFor(const int16_t* chapterSpine, const int chapterCount, const int spine) {
  if (!chapterSpine || spine < 0) return -1;
  for (int k = 0; k < chapterCount; k++) {
    if (chapterSpine[k] == spine) return k;
  }
  return -1;
}

}  // namespace BibleEntry
