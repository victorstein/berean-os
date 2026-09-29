#pragma once

#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <string_view>
#include <utility>

namespace study {

// Where a passage's whole text lives. The firmware passes a PSRAM allocator
// (src/study/PsramJsonAllocator): uncapped texts in internal SRAM would grow its
// footprint with the user's data. The default is malloc/free, for the host. The
// same shape as BibleSearch::BuildAllocator.
struct TextAllocator {
  void* (*allocate)(size_t bytes);
  void (*release)(void* block);

  bool operator==(const TextAllocator&) const = default;
};

inline void* mallocText(const size_t bytes) { return std::malloc(bytes); }
inline void freeText(void* block) { std::free(block); }

inline TextAllocator defaultTextAllocator() { return {mallocText, freeText}; }

// One passage's text in a single NUL-terminated block. Move-only: a copy can run
// out of memory and a copy constructor cannot say so -- copyFrom() does.
class PassageText {
 public:
  PassageText() = default;
  explicit PassageText(const TextAllocator allocator) : allocator_(allocator) {}
  ~PassageText() { clear(); }

  PassageText(const PassageText&) = delete;
  PassageText& operator=(const PassageText&) = delete;

  PassageText(PassageText&& other) noexcept
      : allocator_(other.allocator_),
        data_(std::exchange(other.data_, nullptr)),
        size_(std::exchange(other.size_, 0)) {}

  PassageText& operator=(PassageText&& other) noexcept {
    if (this != &other) {
      clear();
      allocator_ = other.allocator_;
      data_ = std::exchange(other.data_, nullptr);
      size_ = std::exchange(other.size_, 0);
    }
    return *this;
  }

  // False when the block cannot be allocated; the previous text is then kept.
  bool assign(const std::string_view text) {
    if (text.empty()) {
      clear();
      return true;
    }
    auto* block = static_cast<char*>(allocator_.allocate(text.size() + 1));
    if (!block) return false;
    memcpy(block, text.data(), text.size());
    block[text.size()] = '\0';
    clear();
    data_ = block;
    size_ = text.size();
    return true;
  }

  // Copies into this text's own allocator.
  bool copyFrom(const PassageText& other) { return assign(other.view()); }

  void clear() {
    if (data_) allocator_.release(data_);
    data_ = nullptr;
    size_ = 0;
  }

  std::string_view view() const { return data_ ? std::string_view(data_, size_) : std::string_view(); }
  const char* c_str() const { return data_ ? data_ : ""; }
  size_t size() const { return size_; }
  bool empty() const { return size_ == 0; }
  TextAllocator allocator() const { return allocator_; }

 private:
  TextAllocator allocator_ = defaultTextAllocator();
  char* data_ = nullptr;
  size_t size_ = 0;
};

inline bool operator==(const PassageText& a, const std::string_view b) { return a.view() == b; }
inline bool operator==(const PassageText& a, const PassageText& b) { return a.view() == b.view(); }

}  // namespace study
