#pragma once

#include <Epub/VerseAnchors.h>
#include <StudyStore/Unit.h>

#include <cstdint>

#include "BibleEntryPosition.h"

// Which chapter or verse cells of the Bible navigator hold one of the reader's
// tagged passages or bookmarks. Free of FreeInkUI, Arduino and the StudyStore
// singleton so the host suite can exercise it (test/number_grid).
namespace GridMarks {

// Psalm 119's verse count, the longest list either level shows; chapter lists
// top out at Psalms' 150.
constexpr int CAPACITY = 176;

class Bits {
 public:
  void clear() {
    for (auto& byte : bytes) byte = 0;
  }
  void set(const int index) {
    if (index < 0 || index >= CAPACITY) return;
    bytes[index / 8] = static_cast<uint8_t>(bytes[index / 8] | (1U << (index % 8)));
  }
  bool test(const int index) const {
    if (index < 0 || index >= CAPACITY) return false;
    return ((bytes[index / 8] >> (index % 8)) & 1U) != 0;
  }

 private:
  uint8_t bytes[(CAPACITY + 7) / 8] = {};
};

struct BookmarkPosition {
  uint16_t spine = 0;
  bool hasOffset = false;
  uint32_t offset = 0;
};

struct VerseSpan {
  uint16_t startChapter = 0;
  uint16_t startVerse = 0;
  uint16_t endChapter = 0;
  uint16_t endVerse = 0;
};

inline bool addressBefore(const uint16_t chapterA, const uint16_t verseA, const uint16_t chapterB,
                          const uint16_t verseB) {
  return chapterA < chapterB || (chapterA == chapterB && verseA < verseB);
}

// The verses a passage covers in `book` (1-66). False when its start is not a
// verse of that book. An end that is not a later-or-equal verse of the same
// book collapses the span to the start.
inline bool spanFor(const study::Unit& start, const study::Unit& end, const uint8_t book, VerseSpan& out) {
  if (start.kind != study::UnitKind::Verse || start.book != book) return false;
  out = VerseSpan{start.major, start.minor, start.major, start.minor};
  const bool endUsable = end.kind == study::UnitKind::Verse && end.book == book &&
                         !addressBefore(end.major, end.minor, start.major, start.minor);
  if (endUsable) {
    out.endChapter = end.major;
    out.endVerse = end.minor;
  }
  return true;
}

// Chapter row r is chapter r + 1.
inline void markPassageChapters(Bits& bits, const VerseSpan& span, const int chapterCount) {
  for (int chapter = span.startChapter; chapter <= span.endChapter && chapter <= chapterCount; chapter++) {
    bits.set(chapter - 1);
  }
}

inline void markPassageVerses(Bits& bits, const VerseSpan& span, const VerseAnchors::VerseAnchor* anchors,
                              const int count) {
  if (!anchors) return;
  for (int i = 0; i < count; i++) {
    const auto& anchor = anchors[i];
    if (addressBefore(anchor.chapter, anchor.verse, span.startChapter, span.startVerse)) continue;
    if (addressBefore(span.endChapter, span.endVerse, anchor.chapter, anchor.verse)) continue;
    bits.set(i);
  }
}

inline void markBookmarkChapters(Bits& bits, const BookmarkPosition* bookmarks, const int bookmarkCount,
                                 const int16_t* chapterSpine, const int chapterCount) {
  if (!bookmarks) return;
  for (int b = 0; b < bookmarkCount; b++) {
    bits.set(BibleEntry::chapterRowFor(chapterSpine, chapterCount, bookmarks[b].spine));
  }
}

// The cell of the last anchor at or before `offset`; an offset ahead of the
// first anchor belongs to verse 1's cell. Anchors ascend by offset
// (VerseAnchors.h). -1 when there are none.
inline int verseCellAtOffset(const VerseAnchors::VerseAnchor* anchors, const int count, const uint32_t offset) {
  if (!anchors || count <= 0) return -1;
  int cell = 0;
  for (int i = 1; i < count && anchors[i].offset <= offset; i++) cell = i;
  return cell;
}

// A bookmark saved before offsets were recorded can only mark its chapter.
inline void markBookmarkVerses(Bits& bits, const BookmarkPosition* bookmarks, const int bookmarkCount,
                               const int verseSpine, const VerseAnchors::VerseAnchor* anchors, const int count) {
  if (!bookmarks || verseSpine < 0) return;
  for (int b = 0; b < bookmarkCount; b++) {
    if (!bookmarks[b].hasOffset || bookmarks[b].spine != verseSpine) continue;
    bits.set(verseCellAtOffset(anchors, count, bookmarks[b].offset));
  }
}

}  // namespace GridMarks
