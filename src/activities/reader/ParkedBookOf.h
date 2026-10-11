#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <utility>

// One book kept across a Home round trip: the reader parks its loaded book on exit, and the next
// reader takes it back instead of loading it again. Nothing reads through a parked book, so a
// change on the card is caught when it is taken: the path and the EPUB's size must match, and the
// book must still agree with its cache on SD (Book::reopenCache). take() always empties the slot.
//
// A template over the book and the Go-to cache so the host suite can exercise the rules without
// Epub (test/parked_book); the firmware instantiates it once, in ParkedBook.h.
template <typename Book, typename NavCache>
class ParkedBookOf {
 public:
  struct Held {
    std::shared_ptr<Book> book;
    std::shared_ptr<NavCache> nav;
  };

  // Why a take() handed nothing back, for the log.
  enum class Miss : uint8_t { None, Empty, OtherPath, FileChanged, CacheChanged };

  void park(std::string path, const uint64_t fileSize, Held held) {
    path_ = std::move(path);
    fileSize_ = fileSize;
    held_ = std::move(held);
  }

  void clear() {
    held_ = {};
    path_.clear();
    fileSize_ = 0;
  }

  bool empty() const { return held_.book == nullptr; }

  // `fileSize` 0 means the EPUB could not be sized, which never matches.
  Held take(const std::string& path, const uint64_t fileSize, const bool skipLoadingCss, Miss& miss) {
    Held held = std::move(held_);
    const std::string parkedPath = std::move(path_);
    const uint64_t parkedSize = fileSize_;
    clear();

    if (!held.book) {
      miss = Miss::Empty;
      return {};
    }
    if (parkedPath != path) {
      miss = Miss::OtherPath;
      return {};
    }
    if (fileSize == 0 || fileSize != parkedSize) {
      miss = Miss::FileChanged;
      return {};
    }
    if (!held.book->reopenCache(skipLoadingCss)) {
      miss = Miss::CacheChanged;
      return {};
    }
    miss = Miss::None;
    return held;
  }

 private:
  std::string path_;
  uint64_t fileSize_ = 0;
  Held held_;
};
