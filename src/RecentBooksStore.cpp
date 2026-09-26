#include "RecentBooksStore.h"

#include <Epub.h>
#include <FsHelpers.h>
#include <HalStorage.h>
#include <Logging.h>
#include <SdPaths.h>

#include <algorithm>
#include <iterator>

void RecentBooksStore::toJson(JsonDocument& doc) const { RecentBooksDoc::toJson(recentBooks, doc); }

bool RecentBooksStore::fromJson(const JsonVariantConst doc) {
  bool needsResave = false;
  if (!RecentBooksDoc::fromJson(doc, recentBooks, needsResave)) {
    LOG_ERR("RBS", "Refusing %s: unknown format v%d (this build knows v1..v%d)", getFilePath(), doc["v"] | 0,
            RecentBooksDoc::FORMAT_VERSION);
    return false;
  }
  // An entry the load path had to shorten must reach the card, or the file stays
  // over budget and disagrees with what is in memory. loadFromFile performs the
  // save after releasing storeMutex; calling saveToFileAtomic() from here would
  // deadlock on it.
  if (needsResave) requestResave();
  LOG_DBG("RBS", "Recent books loaded from file (%d entries)", getCount());
  return true;
}

void RecentBooksStore::addBook(const std::string& path, const std::string& title, const std::string& author,
                               const std::string& coverBmpPath) {
  // Drop stale entries first so a new add can't evict a valid book in their stead.
  pruneMissing();

  // Remove existing entry if present
  auto it =
      std::find_if(recentBooks.begin(), recentBooks.end(), [&](const RecentBook& book) { return book.path == path; });
  if (it != recentBooks.end()) {
    recentBooks.erase(it);
  }

  // Add to front, bounded: title and author arrive straight from EPUB metadata.
  RecentBook book{path, title, author, coverBmpPath};
  RecentBooksDoc::normalise(book);
  recentBooks.insert(recentBooks.begin(), std::move(book));

  // Trim to max size
  if (recentBooks.size() > RecentBooksDoc::MAX_RECENT_BOOKS) {
    recentBooks.resize(RecentBooksDoc::MAX_RECENT_BOOKS);
  }

  if (!saveToFileAtomic()) {
    LOG_ERR("RBS", "Failed to persist added recent book: %s", path.c_str());
  }
}

void RecentBooksStore::updateBook(const std::string& path, const std::string& title, const std::string& author,
                                  const std::string& coverBmpPath) {
  auto it =
      std::find_if(recentBooks.begin(), recentBooks.end(), [&](const RecentBook& book) { return book.path == path; });
  if (it != recentBooks.end()) {
    RecentBook& book = *it;
    book.title = title;
    book.author = author;
    book.coverBmpPath = coverBmpPath;
    RecentBooksDoc::normalise(book);
    if (!saveToFileAtomic()) {
      LOG_ERR("RBS", "Failed to persist metadata update for: %s", path.c_str());
    }
  }
}

bool RecentBooksStore::removeByPath(const std::string& path) {
  auto it =
      std::find_if(recentBooks.begin(), recentBooks.end(), [&](const RecentBook& book) { return book.path == path; });
  if (it == recentBooks.end()) {
    return false;
  }
  recentBooks.erase(it);
  if (!saveToFileAtomic()) {
    LOG_ERR("RBS", "Failed to persist removal of recent book: %s", path.c_str());
  }
  return true;
}

void RecentBooksStore::updatePath(const std::string& oldPath, const std::string& newPath,
                                  const std::string& oldCachePath, const std::string& newCachePath) {
  auto it = std::find_if(recentBooks.begin(), recentBooks.end(),
                         [&](const RecentBook& book) { return book.path == oldPath; });
  if (it == recentBooks.end()) {
    return;
  }
  it->path = newPath;
  if (!oldCachePath.empty() && !it->coverBmpPath.empty() && it->coverBmpPath.rfind(oldCachePath, 0) == 0) {
    it->coverBmpPath = newCachePath + it->coverBmpPath.substr(oldCachePath.size());
  }
  if (!saveToFileAtomic()) {
    LOG_ERR("RBS", "Failed to persist path change: %s -> %s", oldPath.c_str(), newPath.c_str());
  }
}

bool RecentBooksStore::isMissing(const RecentBook& book) { return !Storage.exists(book.path.c_str()); }

bool RecentBooksStore::pruneMissing() {
  const size_t before = recentBooks.size();
  recentBooks.erase(std::remove_if(recentBooks.begin(), recentBooks.end(), &isMissing), recentBooks.end());
  return recentBooks.size() != before;
}

RecentBook RecentBooksStore::getDataFromBook(std::string path) const {
  std::string lastBookFileName = "";
  const size_t lastSlash = path.find_last_of('/');
  if (lastSlash != std::string::npos) {
    lastBookFileName = path.substr(lastSlash + 1);
  }

  LOG_DBG("RBS", "Loading recent book: %s", path.c_str());

  // If epub, try to load the metadata for title/author and cover.
  // Use buildIfMissing=false to avoid heavy epub loading on boot; getTitle()/getAuthor() may be
  // blank until the book is opened, and entries with missing title are omitted from recent list.
  if (FsHelpers::hasEpubExtension(lastBookFileName)) {
    Epub epub(path, sdpaths::CROSSPOINT_DIR);
    epub.load(false, true);
    return RecentBook{path, epub.getTitle(), epub.getAuthor(), epub.getThumbBmpPath()};
  }
  return RecentBook{path, "", "", ""};
}

static_assert(RecentBooksStore::saveBudget() == RecentBooksDoc::SAVE_BUDGET,
              "RecentBooksStore's budget is RecentBooksDoc::worstCaseBytes() -- derived from the field caps, "
              "not a round number to be tidied");
static_assert(RecentBooksDoc::SAVE_BUDGET < persist::DEFAULT_SAVE_BUDGET,
              "a store whose fields are bounded must claim less than the shared default");
