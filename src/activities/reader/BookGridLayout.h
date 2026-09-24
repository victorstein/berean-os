#pragma once

#include <algorithm>
#include <cstdint>

#include "NumberGridLayout.h"

// Layout arithmetic for the Bible book level: how many abbreviation columns fit,
// and how books split into pages that never straddle a section (testament).
// Free of FreeInkUI, Arduino and GfxRenderer so the host suite can exercise it
// (test/number_grid); the widest label is measured by the caller.
namespace BookGrid {

constexpr int MIN_COLS = 3;
constexpr int MAX_COLS = 6;
// Horizontal room a key needs around its label so the text never touches the
// key border.
constexpr int LABEL_PADDING = 16;
// Two testaments in the NWT; a publication with more headings than this pages
// continuously instead.
constexpr int MAX_SECTIONS = 4;
// Layout is returned by value onto the render task's stack, so it is kept well
// under the 256-byte local budget: 12 pages of 6 bytes. A content rect thin
// enough to force very few rows can need more pages than this to cover every
// book; appendPages then stops early, leaving layout.coveredBooks short of the
// requested count.
constexpr int MAX_PAGES = 12;

struct Page {
  int16_t first = 0;
  int16_t count = 0;
  // Index into the caller's section titles, or -1 when paging continuously.
  int16_t section = -1;
};

struct Layout {
  int cols = 0;
  int rows = 0;
  Page pages[MAX_PAGES] = {};
  int pageCount = 0;
  // Sum of pages[*].count. Less than the book count passed to layoutFor means
  // MAX_PAGES was hit and trailing books have no page.
  int16_t coveredBooks = 0;
};

inline int columnsFor(const int contentW, const int widestLabelPx, const int gap = NumberGrid::GAP) {
  const int stride = std::max(widestLabelPx, 0) + LABEL_PADDING + gap;
  const int fit = contentW > 0 ? (contentW + gap) / stride : 0;
  return std::clamp(fit, MIN_COLS, MAX_COLS);
}

// Section starts are usable only when they begin at book 0, strictly increase
// and stay inside the book count; anything else is a page we do not understand.
inline bool sectionsUsable(const int bookCount, const int* sectionStarts, const int sectionCount) {
  if (!sectionStarts || sectionCount <= 0 || sectionCount > MAX_SECTIONS) return false;
  if (sectionStarts[0] != 0) return false;
  for (int s = 1; s < sectionCount; s++) {
    if (sectionStarts[s] <= sectionStarts[s - 1] || sectionStarts[s] >= bookCount) return false;
  }
  return true;
}

inline void appendPages(Layout& layout, const int first, const int count, const int section) {
  const int perPage = layout.cols * layout.rows;
  for (int offset = 0; offset < count && layout.pageCount < MAX_PAGES; offset += perPage) {
    const int16_t pageBooks = static_cast<int16_t>(std::min(perPage, count - offset));
    layout.pages[layout.pageCount++] =
        Page{static_cast<int16_t>(first + offset), pageBooks, static_cast<int16_t>(section)};
    layout.coveredBooks += pageBooks;
  }
}

inline Layout layoutFor(const int bookCount, const int* sectionStarts, const int sectionCount, const int contentW,
                        const int contentH, const int widestLabelPx, const int gap = NumberGrid::GAP) {
  Layout layout;
  if (bookCount <= 0) return layout;

  const bool sectioned = sectionsUsable(bookCount, sectionStarts, sectionCount);
  int largestRun = bookCount;
  if (sectioned) {
    largestRun = 0;
    for (int s = 0; s < sectionCount; s++) {
      const int end = s + 1 < sectionCount ? sectionStarts[s + 1] : bookCount;
      largestRun = std::max(largestRun, end - sectionStarts[s]);
    }
  }

  layout.cols = columnsFor(contentW, widestLabelPx, gap);
  const int rowsNeeded = (largestRun + layout.cols - 1) / layout.cols;
  const int rowsFit = contentH > 0 ? (contentH + gap) / (NumberGrid::MIN_CELL + gap) : 0;
  layout.rows = std::max(1, std::min({rowsNeeded, rowsFit, NumberGrid::MAX_CELLS / layout.cols}));

  if (!sectioned) {
    appendPages(layout, 0, bookCount, -1);
    return layout;
  }
  for (int s = 0; s < sectionCount; s++) {
    const int end = s + 1 < sectionCount ? sectionStarts[s + 1] : bookCount;
    appendPages(layout, sectionStarts[s], end - sectionStarts[s], s);
  }
  return layout;
}

inline int pageOf(const Layout& layout, const int book) {
  if (layout.pageCount <= 0) return 0;
  for (int p = layout.pageCount - 1; p > 0; p--) {
    if (book >= layout.pages[p].first) return p;
  }
  return 0;
}

// The book one row down (direction > 0) or up from `index`. Leaving a page
// keeps the column, clamped to the books the destination page has; past the
// first or last covered book the step stops there, like the number levels'
// clamped row step.
inline int stepRow(const Layout& layout, const int index, const int direction) {
  if (layout.pageCount <= 0 || layout.cols <= 0 || layout.coveredBooks <= 0) return 0;
  const int cols = layout.cols;
  const int current = std::clamp(index, 0, layout.coveredBooks - 1);
  const int pageIndex = pageOf(layout, current);
  const Page& page = layout.pages[pageIndex];
  const int offset = current - page.first;
  const int column = offset % cols;
  const int lastOffset = page.count - 1;

  if (direction > 0) {
    if (offset / cols < lastOffset / cols) return page.first + std::min(offset + cols, lastOffset);
    if (pageIndex + 1 >= layout.pageCount) return layout.coveredBooks - 1;
    const Page& next = layout.pages[pageIndex + 1];
    return next.first + std::min(column, next.count - 1);
  }

  if (offset >= cols) return current - cols;
  if (pageIndex == 0) return layout.pages[0].first;
  const Page& previous = layout.pages[pageIndex - 1];
  const int lastRowStart = (previous.count - 1) / cols * cols;
  return previous.first + std::min(lastRowStart + column, previous.count - 1);
}

}  // namespace BookGrid
