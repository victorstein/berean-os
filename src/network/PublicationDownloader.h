#pragma once

#include <cstddef>
#include <string>

// Publication symbol -> EPUB on the card, with the publication's identity
// recorded.
//
// Two screens need this chain and it must behave identically for both: the
// weekly meeting downloader, which arrives with a symbol and an issue recovered
// from wol.jw.org, and Buscar, which arrives with a symbol and an issue read out
// of the catalog index. The step that must never be skipped is the
// PubKeyRegistry::record at the end -- the EPUB carries no symbol of its own
// (its dc:identifier is a random urn:uuid), so without it the study store falls
// back to a path-derived key that dies when the file moves.
namespace publication {

enum class Result {
  Ok,
  AlreadyOnCard,
  Cancelled,
  NoMediaLink,  // the API published no EPUB for this symbol, issue and language
  DownloadFailed,
  ChecksumMismatch,
  OutOfMemory,
};

struct Request {
  const char* symbol = "";
  const char* issue = "";  // empty for a non-periodical
  const char* language = "S";
  std::string folder;  // "" means the SD root
};

// Context-carrying function pointers rather than std::function: both callers
// already own their progress state, and a std::function per hook would cost
// ~2-4 KB of binary and a heap allocation for the closure.
struct Hooks {
  void* ctx = nullptr;
  // A phase worth repainting for ("Resolving", "Renamed existing copy").
  void (*onPhase)(void* ctx, const char* message) = nullptr;
  // The filename the download will be written under, once known.
  void (*onResolved)(void* ctx, const char* filename) = nullptr;
  // Called between body chunks. Both callers pump input from here, since the
  // transfer blocks the loop task for its whole duration.
  void (*onProgress)(void* ctx, size_t downloaded, size_t total) = nullptr;
  // Read by HttpDownloader between chunks.
  bool* cancelFlag = nullptr;
};

// The configured download folder, created on demand, falling back to the SD root
// so a download is never lost to a failed mkdir.
std::string resolveDownloadFolder();

// Resolves the media link, downloads, verifies the published MD5, clears any
// stale reading cache for the path, and records the publication identity.
// `outPath` is set as soon as the destination is known, so a caller can report
// which file a failure was about.
Result download(const Request& request, const Hooks& hooks, std::string& outPath);

// Translated one-line explanation for a failed result. Ok and AlreadyOnCard
// return nullptr: neither is an error to report.
const char* failureMessage(Result result);

}  // namespace publication
