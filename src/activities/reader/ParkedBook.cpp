#include "ParkedBook.h"

#include <HalStorage.h>
#include <Logging.h>

#include <utility>

namespace ParkedBook {
namespace {

constexpr const char* MODULE = "PARK";

Slot& slot() {
  static Slot instance;
  return instance;
}

// 0 when the file cannot be opened, which no parked size matches.
uint64_t epubSize(const std::string& path) {
  HalFile file;
  if (!Storage.openFileForRead(MODULE, path, file)) return 0;
  return file.fileSize64();
}

[[maybe_unused]] const char* missReason(const Slot::Miss miss) {
  switch (miss) {
    case Slot::Miss::None:
      return "none";
    case Slot::Miss::Empty:
      return "nothing parked";
    case Slot::Miss::OtherPath:
      return "another book";
    case Slot::Miss::FileChanged:
      return "the EPUB changed";
    case Slot::Miss::CacheChanged:
      return "its cache changed";
  }
  return "unknown";
}

}  // namespace

void park(const std::string& path, Held held) {
  const uint64_t size = held.book ? epubSize(path) : 0;
  if (size == 0) {
    slot().clear();
    return;
  }
  slot().park(path, size, std::move(held));
  LOG_DBG(MODULE, "Parked %s", path.c_str());
}

void clear() { slot().clear(); }

Held take(const std::string& path, const bool skipLoadingCss) {
  if (slot().empty()) {
    LOG_DBG(MODULE, "No parked book");
    return {};
  }
  Slot::Miss miss = Slot::Miss::None;
  Held held = slot().take(path, epubSize(path), skipLoadingCss, miss);
  if (held.book) {
    LOG_DBG(MODULE, "Reusing the parked book");
  } else {
    LOG_DBG(MODULE, "Parked book dropped: %s", missReason(miss));
  }
  return held;
}

}  // namespace ParkedBook
