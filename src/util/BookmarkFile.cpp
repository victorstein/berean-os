#include "BookmarkFile.h"

#include <ArduinoJson.h>
#include <HalStorage.h>
#include <Logging.h>
#include <PersistableStore.h>
#include <SaveBudget.h>

#include "BookmarkDoc.h"
#include "BookmarkSaveAction.h"
#include "BookmarkUtil.h"

namespace {

constexpr const char* MODULE = "BKM";

size_t existingFileSize(const std::string& path) {
  // Check first: openFileForRead logs an SD failure line, and "no file yet" is
  // the ordinary case on a first over-budget save, not a fault worth printing
  // immediately before the refusal a tester is trying to read.
  if (!Storage.exists(path.c_str())) return 0;
  HalFile file;
  if (!Storage.openFileForRead(MODULE, path, file)) return 0;
  return file.size();
}

}  // namespace

namespace BookmarkFile {

LoadResult load(const std::string& bookPath, std::vector<BookmarkEntry>& bookmarks) {
  bookmarks.clear();

  // Read/write go through PersistableStoreBase so the JSON parser and
  // serializer stay instantiated once, in PersistableStore.cpp.
  const std::string path = BookmarkUtil::getBookmarkPath(bookPath);
  const LoadResult result = PersistableStoreBase::loadAdopting(
      path.c_str(), &PersistableStoreBase::readDocFromFileChecked,
      [](void* target, JsonVariantConst json) {
        return BookmarkDoc::fromJson(json, *static_cast<std::vector<BookmarkEntry>*>(target));
      },
      &bookmarks);
  if (result == LoadResult::Loaded) LOG_DBG(MODULE, "Loaded %zu bookmarks from file", bookmarks.size());
  return result;
}

SaveResult save(const std::string& bookPath, const std::vector<BookmarkEntry>& bookmarks) {
  JsonDocument json;
  BookmarkDoc::toJson(bookmarks, json);
  LOG_DBG(MODULE, "Saving %zu bookmarks to file", bookmarks.size());

  const std::string path = BookmarkUtil::getBookmarkPath(bookPath);
  const size_t measured = measureJson(json);
  // Only the over-budget branch can be changed by what is already there, so the
  // common path never opens the file.
  const size_t bytesOnDisk = (measured > BookmarkDoc::SAVE_BYTE_BUDGET) ? existingFileSize(path) : 0;
  if (bookmarkSaveAction(measured, bytesOnDisk, BookmarkDoc::SAVE_BYTE_BUDGET, persist::SD_READ_TRUNCATION_CAP) ==
      BookmarkSaveAction::RefuseTooLarge) {
    LOG_ERR(MODULE, "Bookmarks for %s measure %u bytes; not written", bookPath.c_str(),
            static_cast<unsigned>(measured));
    return SaveResult::TooLarge;
  }

  // writeDocToFileAtomic ensures /.crosspoint; the bookmarks subdirectory is ours.
  Storage.mkdir(BookmarkUtil::getBookmarksDir().c_str());
  return PersistableStoreBase::writeDocToFileAtomic(path.c_str(), json) ? SaveResult::Ok : SaveResult::WriteFailed;
}

}  // namespace BookmarkFile
