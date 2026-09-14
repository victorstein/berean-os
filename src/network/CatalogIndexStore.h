#pragma once

#include <Catalog/CatalogIndex.h>
#include <Catalog/CatalogStamp.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

// The publication index, as the device holds it.
//
// The published asset is ~18 KB gzipped and inflates to ~217 KB. That working
// set lives in PSRAM for as long as Buscar is open and is released on exit;
// Storage.readFile could not load it in any case, capping silently at 50,000
// bytes (SDCardManager.cpp:202) and reporting "not found" for three quarters of
// the catalog with nothing logged.
//
// The compressed asset is cached on the card, so opening Buscar costs no
// network. Fetching is explicit and two-step: checkRemote() stages a download
// beside the held index and compares provenance, applyStaged() promotes it. A
// failed fetch never disturbs what is held.
class CatalogIndexStore {
 public:
  enum class Status {
    Ok,
    UpToDate,    // the published index is the one already held
    NotHeld,     // nothing cached on the card yet
    ReadFailed,  // the card refused the file
    BadFormat,   // not an index this build accepts (wrong version, language, or not an index at all)
    TooLarge,    // past the byte budget: refused rather than truncated
    OutOfMemory,
    FetchFailed,
    Cancelled,
  };

  using ProgressCallback = void (*)(void* ctx, size_t downloaded, size_t total);

  static CatalogIndexStore& getInstance();

  CatalogIndexStore(const CatalogIndexStore&) = delete;
  CatalogIndexStore& operator=(const CatalogIndexStore&) = delete;

  // Inflates the cached asset into PSRAM and validates its header. Idempotent:
  // a second call while already held is a no-op.
  Status load();
  // Frees the PSRAM working set. The stamp survives, so the screen can still say
  // which index the card holds.
  void release();

  bool held() const { return index != nullptr; }
  // The whole inflated index, header line included. Valid only while held().
  std::string_view view() const;
  const catalog::Stamp& stamp() const { return heldStamp; }

  // Downloads the published index beside the held one and compares provenance.
  // Ok means a different index is staged and applyStaged() will install it;
  // UpToDate means the staged copy matched and was discarded.
  Status checkRemote(ProgressCallback onProgress, void* ctx, bool* cancelFlag);
  bool hasStaged() const { return staged; }
  const catalog::Stamp& stagedStamp() const { return stagedIndexStamp; }

  // Promotes the staged download and reloads. Leaves the held index alone on
  // failure.
  Status applyStaged();
  void discardStaged();

  // Publication language, and therefore which per-language asset is fetched.
  // Read from settings rather than fixed: the cached index path carries it, so
  // changing the setting stops finding the old file rather than reading an
  // index in the wrong language.
  static const char* language();

  // The index is 216,708 B uncompressed in Spanish. The budget refuses a wrong
  // or hostile asset before it is allocated, rather than after.
  static constexpr size_t MAX_COMPRESSED_BYTES = 512 * 1024;
  static constexpr size_t MAX_INFLATED_BYTES = 1024 * 1024;

 private:
  CatalogIndexStore() = default;

  // heap_caps_malloc is the only way to demand PSRAM; makeUniqueNoThrow's
  // operator new takes no capability argument. The deleter keeps the RAII
  // guarantee anyway.
  struct PsramFree {
    void operator()(uint8_t* p) const;
  };
  using PsramBuffer = std::unique_ptr<uint8_t[], PsramFree>;

  static PsramBuffer allocatePsram(size_t bytes);
  // Reads the whole file into PSRAM, streaming in chunks. Refuses anything past
  // MAX_COMPRESSED_BYTES.
  static Status readCompressed(const char* path, PsramBuffer& out, size_t& outSize);
  // Inflates when the bytes carry the gzip magic, copies when they do not: the
  // published asset may be served either way.
  static Status inflate(const uint8_t* source, size_t sourceSize, PsramBuffer& out, size_t& outSize);
  // readCompressed + inflate + header validation, into `out`.
  static Status loadFrom(const char* path, PsramBuffer& out, size_t& outSize, catalog::Stamp& outStamp);

  static std::string indexPath();
  static std::string stagedPath();
  static std::string assetUrl();

  PsramBuffer index;
  size_t indexSize = 0;
  catalog::Stamp heldStamp;

  bool staged = false;
  catalog::Stamp stagedIndexStamp;
};
