#pragma once

#include <algorithm>

#include "../BookmarkEntry.h"

// Whether a bookmark belongs to the page on screen, free of Epub and Arduino so
// it can be host-tested. `pageRange` is the page's span of whole-book progress.
struct ProgressRange {
  float start;
  float end;
};

inline constexpr float BOOKMARK_PROGRESS_EPSILON = 0.0001f;

inline bool bookmarkMatchesProgress(const BookmarkEntry& bookmark, const int spineIndex, const int page,
                                    const int pageCount, const ProgressRange& pageRange) {
  if (bookmark.computedSpineIndex == spineIndex && bookmark.computedChapterPageCount == pageCount &&
      bookmark.computedChapterProgress == page) {
    return true;
  }

  const float bookmarkProgress = std::clamp(bookmark.percentage, 0.0f, 1.0f);
  return bookmarkProgress + BOOKMARK_PROGRESS_EPSILON >= pageRange.start &&
         bookmarkProgress - BOOKMARK_PROGRESS_EPSILON <= pageRange.end;
}
