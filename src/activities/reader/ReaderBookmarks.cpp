#include "ReaderBookmarks.h"

#include <Epub.h>
#include <Epub/Section.h>
#include <I18n.h>
#include <Logging.h>

#include <algorithm>
#include <utility>

#include "ReaderUtils.h"
// ReaderUtils.h pulls in ActivityManager.h, which only forward-declares Activity while holding
// std::unique_ptr<Activity> members. Destroying that unique_ptr needs the complete type, so the
// definition must be visible here.
#include "activities/Activity.h"
#include "util/BookmarkFile.h"
#include "util/BookmarkMatch.h"
#include "util/BookmarkUtil.h"

namespace {
constexpr size_t initialBookmarkCacheCapacity = 16;

ProgressRange getPageProgressRange(const std::shared_ptr<Epub>& epub, const int spineIndex, const int page,
                                   const int pageCount) {
  if (pageCount <= 1) {
    return {epub->calculateProgress(spineIndex, 0.0f), epub->calculateProgress(spineIndex, 1.0f)};
  }

  const float step = 1.0f / static_cast<float>(pageCount - 1);
  const float anchor = std::clamp(static_cast<float>(page) * step, 0.0f, 1.0f);
  const float start = std::max(0.0f, anchor - (step * 0.5f));
  const float end = std::min(1.0f, anchor + (step * 0.5f));
  return {epub->calculateProgress(spineIndex, start), epub->calculateProgress(spineIndex, end)};
}
}  // namespace

const char* ReaderBookmarks::bookmarkToastString(const BookmarkToast toast) {
  switch (toast) {
    case BookmarkToast::Added:
      return tr(STR_BOOKMARK_ADDED);
    case BookmarkToast::Removed:
      return tr(STR_BOOKMARK_REMOVED);
    case BookmarkToast::TooLarge:
      return tr(STR_BOOKMARKS_TOO_LARGE);
    case BookmarkToast::SaveFailed:
    case BookmarkToast::LoadDisabled:
      break;
  }
  return tr(STR_ERROR_GENERAL_FAILURE);
}

const char* ReaderBookmarks::toastText() const { return bookmarkToastString(toast_); }

void ReaderBookmarks::load(const GfxRenderer& renderer, const std::shared_ptr<Epub>& epub, const Section* section,
                           const int spineIndex) {
  cachedBookmarks_.clear();
  if (cachedBookmarks_.capacity() < initialBookmarkCacheCapacity) {
    cachedBookmarks_.reserve(initialBookmarkCacheCapacity);
  }
  if (!epub) {
    currentPageBookmarked_ = false;
    return;
  }

  // Toast on the TRANSITION, not the result: this runs again on every return
  // from the bookmarks list, and every toast costs an e-ink refresh.
  if (BookmarkFile::load(epub->getPath(), cachedBookmarks_) == BookmarkFile::LoadResult::Failed && !saveDisabled_) {
    saveDisabled_ = true;
    LOG_ERR("ERS", "Bookmarks unreadable; saving disabled while this book is open");
    ReaderUtils::showMessage(renderer, bookmarkToastString(BookmarkToast::LoadDisabled));
  }
  refreshPageFlag(epub, section, spineIndex);
}

bool ReaderBookmarks::beginToggle(const unsigned long nowMs) {
  // Every path from here shows a toast, so arm it once.
  toastVisible_ = true;
  toastShownAtMs_ = nowMs;

  if (saveDisabled_) {
    toast_ = BookmarkToast::LoadDisabled;
    return false;
  }
  return true;
}

void ReaderBookmarks::toggle(const std::shared_ptr<Epub>& epub, Section& section, const int spineIndex,
                             const int currentPage, const int pageCount, const std::optional<uint32_t> visibleOffset,
                             const SavedProgressPosition& progress) {
  const ProgressRange pageRange = getPageProgressRange(epub, spineIndex, currentPage, pageCount);

  // Everything a rollback needs: the entries about to be erased, with the index
  // each sat at. Collected in ascending order, so re-inserting in that order
  // restores the original positions. A page matches one bookmark unless the
  // user built overlapping ones, so one slot is the realistic size.
  std::vector<std::pair<size_t, BookmarkEntry>> erased;
  erased.reserve(1);
  for (size_t i = 0; i < cachedBookmarks_.size(); ++i) {
    if (bookmarkMatchesProgress(cachedBookmarks_[i], spineIndex, currentPage, pageCount, pageRange)) {
      erased.emplace_back(i, cachedBookmarks_[i]);
    }
  }
  const bool wasBookmarked = !erased.empty();

  if (wasBookmarked) {
    // Erase by the indices just collected, descending so each erase leaves the
    // lower ones valid. Re-deriving the match here instead is what would let an
    // edit to one copy of the rule make the rollback restore a different set
    // than the one removed.
    for (auto it = erased.rbegin(); it != erased.rend(); ++it) {
      cachedBookmarks_.erase(cachedBookmarks_.begin() + static_cast<std::ptrdiff_t>(it->first));
    }
  } else {
    std::string pageText;
    if (currentPage >= 0 && currentPage < pageCount) {
      pageText = section.getTextFromSectionFile();
    }
    BookmarkEntry entry;
    entry.percentage = progress.percentage;
    entry.xpath = progress.xpath;
    entry.summary = BookmarkUtil::sanitizeBookmarkSummary(pageText);
    entry.computedSpineIndex = spineIndex;
    entry.computedChapterPageCount = pageCount;
    entry.computedChapterProgress = currentPage;
    std::optional<uint32_t> offset = visibleOffset;
    if (!offset.has_value() && currentPage >= 0 && currentPage < section.pageCount) {
      offset = section.getVisibleTextOffsetForPage(static_cast<uint16_t>(currentPage));
    }
    if (offset.has_value()) {
      entry.visibleTextOffset = *offset;
      entry.hasVisibleTextOffset = true;
    }
    cachedBookmarks_.insert(cachedBookmarks_.begin(), entry);
  }

  const BookmarkFile::SaveResult saved = BookmarkFile::save(epub->getPath(), cachedBookmarks_);
  if (saved == BookmarkFile::SaveResult::Ok) {
    currentPageBookmarked_ = !wasBookmarked;
    toast_ = wasBookmarked ? BookmarkToast::Removed : BookmarkToast::Added;
    return;
  }

  // Nothing reached the card, so the resident list and the page's flag must go
  // back to what the card still holds -- otherwise the page reads as bookmarked
  // for something that will not be there after a reopen.
  if (wasBookmarked) {
    for (const auto& [index, entry] : erased) {
      cachedBookmarks_.insert(cachedBookmarks_.begin() + static_cast<std::ptrdiff_t>(index), entry);
    }
  } else {
    cachedBookmarks_.erase(cachedBookmarks_.begin());
  }
  refreshPageFlag(epub, &section, spineIndex);  // derive the flag from the vector the rollback just restored
  toast_ = (saved == BookmarkFile::SaveResult::TooLarge) ? BookmarkToast::TooLarge : BookmarkToast::SaveFailed;
  LOG_ERR("ERS", "Bookmark save refused; rolled the change back");
}

void ReaderBookmarks::refreshPageFlag(const std::shared_ptr<Epub>& epub, const Section* section, const int spineIndex) {
  if (!section || !epub || cachedBookmarks_.empty()) {
    currentPageBookmarked_ = false;
    return;
  }
  const int pageCount = section->estimatedTotalPages();
  const ProgressRange pageRange = getPageProgressRange(epub, spineIndex, section->currentPage, pageCount);
  currentPageBookmarked_ = std::any_of(cachedBookmarks_.begin(), cachedBookmarks_.end(), [&](const BookmarkEntry& b) {
    return bookmarkMatchesProgress(b, spineIndex, section->currentPage, pageCount, pageRange);
  });
}

bool ReaderBookmarks::expireToast(const unsigned long nowMs) {
  if (!toastVisible_ || (nowMs - toastShownAtMs_) < ReaderUtils::BOOKMARK_MESSAGE_DURATION_MS) {
    return false;
  }
  toastVisible_ = false;
  return true;
}
