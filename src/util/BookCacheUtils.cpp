#include "BookCacheUtils.h"

#include <Epub.h>
#include <FsHelpers.h>
#include <HalStorage.h>
#include <Logging.h>
#include <SdPaths.h>

#include "BookProgress.h"

bool isBookCacheDirectoryName(const char* name) {
  if (!name) {
    return false;
  }

  constexpr char EPUB_PREFIX[] = "epub_";

  return strncmp(name, EPUB_PREFIX, std::size(EPUB_PREFIX) - 1) == 0;
}

std::string bookCachePath(const std::string& path) { return Epub(path, sdpaths::CROSSPOINT_DIR).getCachePath(); }

void clearBookCache(const std::string& path) {
  if (FsHelpers::hasEpubExtension(path)) {
    Epub(path, sdpaths::CROSSPOINT_DIR).clearCache();
  } else {
    return;
  }
  LOG_DBG("BookCache", "Done checking metadata cache for: %s", path.c_str());
}

std::optional<int> readBookProgressPercent(const std::string& bookPath) {
  if (!FsHelpers::hasEpubExtension(bookPath)) return std::nullopt;

  Epub epub(bookPath, sdpaths::CROSSPOINT_DIR);
  // No build: a missing book.bin means no bar, never a zip parse to draw a list.
  if (!epub.load(false, true)) return std::nullopt;

  const std::string progressPath = epub.getCachePath() + "/progress.bin";
  uint8_t data[10];
  int size = 0;
  if (Storage.exists(progressPath.c_str())) {
    HalFile file;
    if (Storage.openFileForRead("BookCache", progressPath, file)) size = file.read(data, sizeof(data));
  }

  SavedProgress saved;
  if (size <= 0 || !parseProgressBytes(data, static_cast<size_t>(size), saved)) return 0;
  // Past the last chapter, calculateProgress wraps an unsigned subtraction.
  if (static_cast<int>(saved.spine) >= epub.getSpineItemsCount()) {
    LOG_ERR("BookCache", "Saved chapter %u is past the %d chapters of %s", static_cast<unsigned>(saved.spine),
            epub.getSpineItemsCount(), bookPath.c_str());
    return std::nullopt;
  }
  return roundPercent(epub.calculateProgress(saved.spine, chapterFraction(saved)));
}
