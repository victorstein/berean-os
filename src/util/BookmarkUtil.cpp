#include "BookmarkUtil.h"

#include <PathFlatten.h>
#include <Utf8.h>

#include <utility>

std::string BookmarkUtil::getBookmarksDir() { return "/.crosspoint/bookmarks/"; }

std::string BookmarkUtil::getBookmarkPath(const std::string& bookPath) {
  return getBookmarksDir() + pathflatten::toCacheName(bookPath) + ".json";
}

std::string BookmarkUtil::sanitizeBookmarkSummary(std::string summary) { return utf8SafeSummary(std::move(summary)); }
