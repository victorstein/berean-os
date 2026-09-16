#include "BookmarkUtil.h"

#include <PathFlatten.h>
#include <Utf8.h>

#include <utility>

#include "BookmarkDoc.h"

std::string BookmarkUtil::getBookmarksDir() { return "/.crosspoint/bookmarks/"; }

std::string BookmarkUtil::getBookmarkPath(const std::string& bookPath) {
  return getBookmarksDir() + pathflatten::toCacheName(bookPath) + ".json";
}

std::string BookmarkUtil::sanitizeBookmarkSummary(std::string summary) {
  // Same bound the load path applies, named rather than inherited from
  // utf8SafeSummary's default: BookmarkDoc::MAX_RECORD_BYTES and the record
  // arithmetic behind the save budget are both calibrated on it.
  return utf8SafeSummary(std::move(summary), BookmarkDoc::MAX_SUMMARY_BYTES);
}
