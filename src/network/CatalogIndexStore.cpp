#include "CatalogIndexStore.h"

// clang-format off
// HttpDownloader.h pulls Arduino/SdFat, whose macros collide with lwip's
// ip4_addr.h unless seen first. Pin this order; clang-format would otherwise
// sort the local header last and break the build.
#include "CrossPointSettings.h"
#include "HttpDownloader.h"
#include <Catalog/CatalogArchive.h>
#include <HalStorage.h>
#include <InflateReader.h>
#include <Logging.h>
#include <SdPaths.h>
#include <esp_heap_caps.h>
// clang-format on

#include <cstring>

#include "util/TaskWatchdog.h"

#ifndef OTA_RELEASE_REPO
#define OTA_RELEASE_REPO "victorstein/berean-os"
#endif

namespace {

constexpr const char* MODULE = "CATIDX";
constexpr size_t READ_CHUNK_BYTES = 4096;
// Inflating ~217 KB into PSRAM through uzlib's bit-by-bit Huffman decoder runs
// long enough to matter against the 5 s watchdog window.
constexpr int READ_CHUNKS_PER_WATCHDOG_RESET = 16;

}  // namespace

CatalogIndexStore& CatalogIndexStore::getInstance() {
  static CatalogIndexStore instance;
  return instance;
}

void CatalogIndexStore::PsramFree::operator()(uint8_t* p) const { heap_caps_free(p); }

CatalogIndexStore::PsramBuffer CatalogIndexStore::allocatePsram(const size_t bytes) {
  // PSRAM only, deliberately: 217 KB in internal SRAM would take most of what
  // the framebuffer and the render path need. A board without PSRAM gets a
  // clean OOM rather than a crash somewhere later.
  return PsramBuffer(static_cast<uint8_t*>(heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)));
}

const char* CatalogIndexStore::language() { return CrossPointSettings::langWritten(SETTINGS.publicationLanguage); }

std::string CatalogIndexStore::indexPath() {
  return std::string(sdpaths::BEREAN_DIR) + "/catalog-" + language() + ".idx";
}

std::string CatalogIndexStore::stagedPath() { return indexPath() + ".part"; }

std::string CatalogIndexStore::assetUrl() {
  return std::string("https://github.com/" OTA_RELEASE_REPO "/releases/download/catalog/catalog-") + language() +
         ".txt.gz";
}

std::string_view CatalogIndexStore::view() const {
  if (!index) return {};
  return std::string_view(reinterpret_cast<const char*>(index.get()), indexSize);
}

CatalogIndexStore::Status CatalogIndexStore::readCompressed(const char* path, PsramBuffer& out, size_t& outSize) {
  HalFile file;
  if (!Storage.openFileForRead(MODULE, path, file)) return Status::NotHeld;

  const size_t size = file.fileSize();
  if (size == 0) {
    LOG_ERR(MODULE, "%s is empty", path);
    return Status::BadFormat;
  }
  if (size > MAX_COMPRESSED_BYTES) {
    LOG_ERR(MODULE, "%s is %zu bytes, budget is %zu", path, size, MAX_COMPRESSED_BYTES);
    return Status::TooLarge;
  }

  out = allocatePsram(size);
  if (!out) {
    LOG_ERR(MODULE, "OOM: %zu PSRAM bytes for the compressed index", size);
    return Status::OutOfMemory;
  }

  uint8_t* const destination = out.get();
  size_t done = 0;
  int chunks = 0;
  while (done < size) {
    const size_t want = size - done < READ_CHUNK_BYTES ? size - done : READ_CHUNK_BYTES;
    const int got = file.read(destination + done, want);
    if (got <= 0) {
      LOG_ERR(MODULE, "Read error at %zu of %zu in %s", done, size, path);
      return Status::ReadFailed;
    }
    done += static_cast<size_t>(got);
    if (++chunks % READ_CHUNKS_PER_WATCHDOG_RESET == 0) resetTaskWatchdogIfSubscribed();
  }

  outSize = size;
  return Status::Ok;
}

CatalogIndexStore::Status CatalogIndexStore::inflate(const uint8_t* source, const size_t sourceSize, PsramBuffer& out,
                                                     size_t& outSize) {
  if (!catalog::looksGzipped(source, sourceSize)) {
    // The CI job publishes both forms; a plain asset is copied rather than
    // rejected so the device keeps working if the .gz ever stops being built.
    if (sourceSize > MAX_INFLATED_BYTES) return Status::TooLarge;
    out = allocatePsram(sourceSize);
    if (!out) return Status::OutOfMemory;
    memcpy(out.get(), source, sourceSize);
    outSize = sourceSize;
    return Status::Ok;
  }

  size_t payload = 0;
  uint32_t declared = 0;
  if (!catalog::gzipPayloadOffset(source, sourceSize, payload) ||
      !catalog::gzipDeclaredSize(source, sourceSize, declared)) {
    LOG_ERR(MODULE, "Not a usable gzip member (%zu bytes)", sourceSize);
    return Status::BadFormat;
  }
  if (declared == 0 || declared > MAX_INFLATED_BYTES) {
    LOG_ERR(MODULE, "Declared size %lu outside the budget", static_cast<unsigned long>(declared));
    return Status::TooLarge;
  }

  out = allocatePsram(declared);
  if (!out) {
    LOG_ERR(MODULE, "OOM: %lu PSRAM bytes for the inflated index", static_cast<unsigned long>(declared));
    return Status::OutOfMemory;
  }

  // One-shot mode: the destination holds the entire output, so back-references
  // resolve inside it and no 32 KB window is allocated. uzlib rather than
  // InflateStream because this runs once per Buscar entry, not on a throughput
  // path, and uzlib's state is ~1 KB against tinfl's ~11 KB.
  InflateReader reader;
  if (!reader.init(false)) return Status::OutOfMemory;
  reader.setSource(source + payload, sourceSize - payload);

  resetTaskWatchdogIfSubscribed();
  const bool ok = reader.read(out.get(), declared);
  resetTaskWatchdogIfSubscribed();
  if (!ok) {
    LOG_ERR(MODULE, "Inflate failed short of the declared %lu bytes", static_cast<unsigned long>(declared));
    return Status::BadFormat;
  }

  outSize = declared;
  return Status::Ok;
}

CatalogIndexStore::Status CatalogIndexStore::loadFrom(const char* path, PsramBuffer& out, size_t& outSize,
                                                      catalog::Stamp& outStamp) {
  PsramBuffer compressed;
  size_t compressedSize = 0;
  const Status read = readCompressed(path, compressed, compressedSize);
  if (read != Status::Ok) return read;

  PsramBuffer inflated;
  size_t inflatedSize = 0;
  const Status expanded = inflate(compressed.get(), compressedSize, inflated, inflatedSize);
  if (expanded != Status::Ok) return expanded;

  const std::string_view text(reinterpret_cast<const char*>(inflated.get()), inflatedSize);
  const catalog::Header header = catalog::parseHeader(text);
  if (!catalog::indexAcceptable(header, language())) {
    LOG_ERR(MODULE, "%s is not an index this build reads (version %d, language %.*s)", path, header.version,
            static_cast<int>(header.language.size()), header.language.data());
    return Status::BadFormat;
  }

  outStamp = catalog::stampOf(header);
  out = std::move(inflated);
  outSize = inflatedSize;
  return Status::Ok;
}

CatalogIndexStore::Status CatalogIndexStore::load() {
  if (index) return Status::Ok;

  const std::string path = indexPath();
  PsramBuffer loaded;
  size_t loadedSize = 0;
  catalog::Stamp loadedStamp;
  const Status status = loadFrom(path.c_str(), loaded, loadedSize, loadedStamp);
  if (status != Status::Ok) return status;

  index = std::move(loaded);
  indexSize = loadedSize;
  heldStamp = loadedStamp;
  LOG_INF(MODULE, "Held %zu bytes, built %s", indexSize, heldStamp.builtOn.c_str());
  return Status::Ok;
}

void CatalogIndexStore::release() {
  index.reset();
  indexSize = 0;
}

CatalogIndexStore::Status CatalogIndexStore::checkRemote(const ProgressCallback onProgress, void* ctx,
                                                         bool* cancelFlag) {
  if (!Storage.exists(sdpaths::BEREAN_DIR) && !Storage.mkdir(sdpaths::BEREAN_DIR)) {
    LOG_ERR(MODULE, "mkdir %s failed", sdpaths::BEREAN_DIR);
    return Status::FetchFailed;
  }

  const std::string staging = stagedPath();
  discardStaged();

  HttpDownloader::ProgressCallback progress = nullptr;
  if (onProgress) {
    progress = [onProgress, ctx](const size_t downloaded, const size_t total) { onProgress(ctx, downloaded, total); };
  }

  const auto result = HttpDownloader::downloadToFile(assetUrl(), staging, progress, cancelFlag);
  if (result == HttpDownloader::ABORTED) {
    Storage.remove(staging.c_str());
    return Status::Cancelled;
  }
  if (result != HttpDownloader::OK) {
    LOG_ERR(MODULE, "Index fetch failed: %d", static_cast<int>(result));
    Storage.remove(staging.c_str());
    return Status::FetchFailed;
  }

  // Validate before anything can replace the held index: a 404 page saved to the
  // card must not become the catalog.
  PsramBuffer fetched;
  size_t fetchedSize = 0;
  catalog::Stamp fetchedStamp;
  const Status status = loadFrom(staging.c_str(), fetched, fetchedSize, fetchedStamp);
  if (status != Status::Ok) {
    Storage.remove(staging.c_str());
    return status;
  }

  if (catalog::sameRelease(heldStamp, fetchedStamp)) {
    Storage.remove(staging.c_str());
    LOG_INF(MODULE, "Published index is the one already held (%s)", heldStamp.builtOn.c_str());
    return Status::UpToDate;
  }

  staged = true;
  stagedIndexStamp = fetchedStamp;
  LOG_INF(MODULE, "Staged index built %s (holding %s)", stagedIndexStamp.builtOn.c_str(),
          heldStamp.builtOn.empty() ? "nothing" : heldStamp.builtOn.c_str());
  return Status::Ok;
}

CatalogIndexStore::Status CatalogIndexStore::applyStaged() {
  if (!staged) return Status::NotHeld;

  const std::string staging = stagedPath();
  const std::string destination = indexPath();

  // The card has no rename-over, so the previous index goes first. It is
  // reconstructible from the release; the staged file has already been inflated
  // and header-checked, so this never trades a good index for a bad one.
  if (Storage.exists(destination.c_str()) && !Storage.remove(destination.c_str())) {
    LOG_ERR(MODULE, "Could not remove %s", destination.c_str());
    return Status::ReadFailed;
  }
  if (!Storage.rename(staging.c_str(), destination.c_str())) {
    LOG_ERR(MODULE, "Rename %s -> %s failed", staging.c_str(), destination.c_str());
    return Status::ReadFailed;
  }

  staged = false;
  stagedIndexStamp = catalog::Stamp{};
  release();
  return load();
}

void CatalogIndexStore::discardStaged() {
  const std::string staging = stagedPath();
  if (Storage.exists(staging.c_str())) Storage.remove(staging.c_str());
  staged = false;
  stagedIndexStamp = catalog::Stamp{};
}
