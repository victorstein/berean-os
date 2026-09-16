#include "PublicationDownloader.h"

// clang-format off
// HttpDownloader.h pulls Arduino/SdFat, whose macros collide with lwip's
// ip4_addr.h unless seen first. Pin this order; clang-format would otherwise
// sort the local header last and break the build.
#include "HttpDownloader.h"
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <MD5Builder.h>
#include <Memory.h>
#include <strings.h>
// clang-format on

#include <functional>

#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "RecentBooksStore.h"
#include "network/MeetingFilename.h"
#include "network/PubMediaJson.h"
#include "network/WolWeekScan.h"
#include "study/PubKeyRegistry.h"
#include "util/BookCacheUtils.h"
#include "util/TaskWatchdog.h"

namespace publication {
namespace {

constexpr const char* MODULE = "PUBDL";
constexpr size_t HASH_CHUNK_BYTES = 2048;
// Hashing several MB off the SD card runs well past the task watchdog window.
constexpr int HASH_CHUNKS_PER_WATCHDOG_RESET = 64;

void reportPhase(const Hooks& hooks, const char* message) {
  if (hooks.onPhase) hooks.onPhase(hooks.ctx, message);
}

bool matchesChecksum(const std::string& path, const char* expectedMd5) {
  if (!expectedMd5 || expectedMd5[0] == '\0') {
    LOG_INF(MODULE, "No checksum published for %s", path.c_str());
    return true;
  }

  HalFile file;
  if (!Storage.openFileForRead(MODULE, path, file)) return false;

  auto buffer = makeUniqueNoThrow<uint8_t[]>(HASH_CHUNK_BYTES);
  if (!buffer) {
    LOG_ERR(MODULE, "OOM: %u byte hash buffer", static_cast<unsigned>(HASH_CHUNK_BYTES));
    return false;
  }

  MD5Builder md5;
  md5.begin();
  int chunks = 0;
  while (true) {
    const int read = file.read(buffer.get(), HASH_CHUNK_BYTES);
    if (read < 0) {
      LOG_ERR(MODULE, "Read error while hashing %s", path.c_str());
      return false;
    }
    if (read == 0) break;
    md5.add(buffer.get(), static_cast<size_t>(read));
    if (++chunks % HASH_CHUNKS_PER_WATCHDOG_RESET == 0) resetTaskWatchdogIfSubscribed();
  }
  md5.calculate();

  char actual[33];
  md5.getChars(actual);
  return strcasecmp(actual, expectedMd5) == 0;
}

bool alreadyOnCard(const std::string& path, const uint64_t advertisedSize) {
  // filesize is absent from some responses and reads back as 0, which a 0-byte
  // file on the card would match forever with no way to repair itself.
  if (advertisedSize == 0 || !Storage.exists(path.c_str())) return false;

  HalFile file;
  if (!Storage.openFileForRead(MODULE, path, file)) return false;
  const auto actualSize = static_cast<uint64_t>(file.fileSize());
  // The caller renames or downloads over this path next, and SdFat needs it
  // closed for either.
  file.close();

  if (actualSize == advertisedSize) return true;
  LOG_INF(MODULE, "%s is %llu bytes, expected %llu: downloading again", path.c_str(),
          static_cast<unsigned long long>(actualSize), static_cast<unsigned long long>(advertisedSize));
  return false;
}

void migrateCdnNamedCopy(const std::string& folder, const std::string& url, const std::string& destPath,
                         const Hooks& hooks) {
  const std::string cdnName = filenameFromUrl(url);
  if (cdnName.empty()) return;
  const std::string srcPath = folder + "/" + cdnName;
  if (srcPath == destPath || !Storage.exists(srcPath.c_str())) return;

  if (!Storage.rename(srcPath.c_str(), destPath.c_str())) {
    LOG_ERR(MODULE, "Rename %s -> %s failed, downloading under the new name", srcPath.c_str(), destPath.c_str());
    return;
  }

  // Anything already keyed to the new path belongs to an earlier file of the
  // same name — one deleted over the web server or WebDAV, neither of which
  // clears the cache the way the file browser does. Clearing it before the cache
  // directory moves in both frees the destination for the rename and stops the
  // migrated book rendering from another issue's sections.
  clearBookCache(destPath);

  const std::string oldCachePath = bookCachePath(srcPath);
  const std::string newCachePath = bookCachePath(destPath);
  // Moving the directory carries progress.bin with it, so reading position and
  // the cover survive the rename.
  if (Storage.exists(oldCachePath.c_str()) && !Storage.rename(oldCachePath.c_str(), newCachePath.c_str())) {
    LOG_ERR(MODULE, "Failed to rename cache dir %s -> %s (non-fatal)", oldCachePath.c_str(), newCachePath.c_str());
  }

  RECENT_BOOKS.updatePath(srcPath, destPath, oldCachePath, newCachePath);
  if (APP_STATE.openEpubPath == srcPath) {
    APP_STATE.openEpubPath = destPath;
    APP_STATE.saveToFileAtomic();
  }

  LOG_INF(MODULE, "Renamed %s -> %s", srcPath.c_str(), destPath.c_str());
  reportPhase(hooks, tr(STR_RENAMED_EXISTING));
}

}  // namespace

std::string resolveDownloadFolder() {
  const char* folder = SETTINGS.downloadFolder;
  if (folder[0] == '\0') return {};
  if (!Storage.exists(folder) && !Storage.mkdir(folder)) {
    LOG_ERR(MODULE, "mkdir failed for %s, using SD root", folder);
    return {};
  }
  return folder;
}

Result download(const Request& request, const Hooks& hooks, std::string& outPath) {
  // ~900 bytes between the tokenizer's buffer and the extracted fields: too much
  // to put on the loop task's stack.
  auto media = makeUniqueNoThrow<PubMediaJsonParser>(request.language);
  if (!media) {
    LOG_ERR(MODULE, "OOM: pub-media parser");
    return Result::OutOfMemory;
  }

  const std::string mediaUrl = pubMediaUrlForSymbol(request.symbol, request.issue, request.language);
  const bool fetched = HttpDownloader::fetchUrl(mediaUrl, [&media](const uint8_t* data, const size_t len) {
    media->feed(reinterpret_cast<const char*>(data), len);
    return true;
  });
  if (!fetched || !media->found()) {
    LOG_ERR(MODULE, "No EPUB link for %s issue '%s' (%s)", request.symbol, request.issue, request.language);
    return Result::NoMediaLink;
  }

  // No issue means a book or brochure, which is named after itself; an issue
  // means a periodical, where the issue is what keeps two of them apart.
  const bool hasIssue = request.issue != nullptr && request.issue[0] != '\0';
  const std::string filename = hasIssue ? meetingPublicationFilename(media->pubName(), request.issue, media->url())
                                        : publicationFilename(media->pubName(), media->url());
  if (filename.empty()) {
    LOG_ERR(MODULE, "Unusable media url: %s", media->url());
    return Result::NoMediaLink;
  }

  outPath.clear();
  outPath.reserve(request.folder.size() + filename.size() + 1);
  outPath += request.folder;
  outPath += '/';
  outPath += filename;

  if (hooks.onResolved) hooks.onResolved(hooks.ctx, filename.c_str());

  if (!Storage.exists(outPath.c_str())) migrateCdnNamedCopy(request.folder, media->url(), outPath, hooks);
  if (!request.force && alreadyOnCard(outPath, media->filesize())) {
    LOG_INF(MODULE, "Skipping %s, already on the card", outPath.c_str());
    // The identity is recorded even on the skip path: the file may predate the
    // registry, and an unregistered publication loses its tags when it moves.
    PubKeyRegistry::record(outPath, study::RegisteredPub{request.symbol, request.issue, request.language});
    return Result::AlreadyOnCard;
  }

  HttpDownloader::ProgressCallback progress = nullptr;
  if (hooks.onProgress) {
    const auto* hooksPtr = &hooks;
    progress = [hooksPtr](const size_t downloaded, const size_t total) {
      hooksPtr->onProgress(hooksPtr->ctx, downloaded, total);
    };
  }

  // downloadToFile writes straight to the path it is given, so replacing a copy
  // already on the card is staged: a transfer that dies halfway would otherwise
  // leave a truncated file where a working publication used to be. A first
  // download has nothing to lose and writes in place.
  const bool replacing = Storage.exists(outPath.c_str());
  const std::string writePath = replacing ? outPath + ".part" : outPath;

  const auto result = HttpDownloader::downloadToFile(media->url(), writePath, progress, hooks.cancelFlag);
  if (result == HttpDownloader::ABORTED) {
    LOG_INF(MODULE, "Download cancelled");
    if (replacing) Storage.remove(writePath.c_str());
    return Result::Cancelled;
  }
  if (result != HttpDownloader::OK) {
    LOG_ERR(MODULE, "Download failed: %d", static_cast<int>(result));
    if (replacing) Storage.remove(writePath.c_str());
    return Result::DownloadFailed;
  }

  // These transfers run over unverified TLS (the wolfSSL transport has no CA
  // bundle wired up, so setInsecure() is unconditional), which makes the MD5 the
  // API publishes the only integrity check available.
  if (!matchesChecksum(writePath, media->checksum())) {
    LOG_ERR(MODULE, "Checksum mismatch for %s", writePath.c_str());
    Storage.remove(writePath.c_str());
    return Result::ChecksumMismatch;
  }

  // Only now, with the replacement downloaded and its checksum verified, is the
  // existing copy given up.
  if (replacing) {
    Storage.remove(outPath.c_str());
    if (!Storage.rename(writePath.c_str(), outPath.c_str())) {
      LOG_ERR(MODULE, "Could not put %s in place", outPath.c_str());
      return Result::DownloadFailed;
    }
  }

  // The reading cache is keyed on the path hash and book.bin records no size or
  // mtime, so a revised issue downloaded over an existing copy would otherwise
  // be rendered from the previous issue's sections.
  clearBookCache(outPath);

  PubKeyRegistry::record(outPath, study::RegisteredPub{request.symbol, request.issue, request.language});

  LOG_INF(MODULE, "Saved %s (%llu bytes advertised)", outPath.c_str(),
          static_cast<unsigned long long>(media->filesize()));
  return Result::Ok;
}

const char* failureMessage(const Result result) {
  switch (result) {
    case Result::NoMediaLink:
      return tr(STR_PUBLICATION_UNAVAILABLE);
    case Result::ChecksumMismatch:
      return tr(STR_CHECKSUM_MISMATCH);
    case Result::DownloadFailed:
    case Result::OutOfMemory:
      return tr(STR_DOWNLOAD_FAILED);
    case Result::Ok:
    case Result::AlreadyOnCard:
    case Result::Cancelled:
      return nullptr;
  }
  return nullptr;
}

}  // namespace publication
