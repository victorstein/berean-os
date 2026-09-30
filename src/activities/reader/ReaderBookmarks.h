#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include "BookmarkEntry.h"
#include "ProgressMapper.h"

class Epub;
class GfxRenderer;
class Section;

// The open book's bookmarks: the resident list, the read-failure save latch,
// the toggle-with-rollback rule and the toast reporting each outcome. The
// owning activity keeps the RenderLock, the reading position and every
// requestUpdate(); nothing here reaches back into it.
class ReaderBookmarks {
 public:
  void load(const GfxRenderer& renderer, const std::shared_ptr<Epub>& epub, const Section* section, int spineIndex);

  // Arms the toast. False when saving is disabled: the toast then says so and
  // the caller must not go on to toggle().
  bool beginToggle(unsigned long nowMs);

  // `currentPage` and `pageCount` are the caller's RenderLock-guarded read of `section`.
  void toggle(const std::shared_ptr<Epub>& epub, Section& section, int spineIndex, int currentPage, int pageCount,
              std::optional<uint32_t> visibleOffset, const SavedProgressPosition& progress);

  void refreshPageFlag(const std::shared_ptr<Epub>& epub, const Section* section, int spineIndex);

  // True when it hid the toast, so the caller should redraw.
  bool expireToast(unsigned long nowMs);

  bool toastVisible() const { return toastVisible_; }
  const char* toastText() const;
  bool currentPageBookmarked() const { return currentPageBookmarked_; }
  bool empty() const { return cachedBookmarks_.empty(); }
  const std::vector<BookmarkEntry>& entries() const { return cachedBookmarks_; }

 private:
  // Which message the bookmark popup shows. beginToggle() and toggle() are the only writers.
  enum class BookmarkToast : uint8_t { Added, Removed, TooLarge, SaveFailed, LoadDisabled };
  static const char* bookmarkToastString(BookmarkToast toast);

  std::vector<BookmarkEntry> cachedBookmarks_;
  // Latched when a bookmark file failed to READ: the bytes may still hold the
  // user's data, so nothing may be written over them for as long as this book
  // is open. Never cleared. Unlike StudyStore::saveDisabled_, which is a
  // singleton's and survives closePublication, the owning activity is
  // constructed per book open -- which is right, because a bookmark file is per
  // book and one book's unreadable file must not silence another's.
  bool saveDisabled_ = false;
  BookmarkToast toast_ = BookmarkToast::Added;
  bool toastVisible_ = false;
  unsigned long toastShownAtMs_ = 0UL;
  bool currentPageBookmarked_ = false;
};
