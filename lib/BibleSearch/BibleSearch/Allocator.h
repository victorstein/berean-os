#pragma once

#include <cstddef>
#include <cstdlib>
#include <utility>

namespace BibleSearch {

// Where the build's and the reader's large blocks come from. The firmware
// passes a PSRAM allocator: the build holds megabytes and a query up to
// ~600 KB, and the ~4 KB threshold that routes plain malloc to PSRAM is a
// build setting, not a guarantee. The default is malloc/free, for the host.
struct BuildAllocator {
  void* (*allocate)(size_t bytes);
  void (*release)(void* block);
};

inline BuildAllocator defaultBuildAllocator() {
  return {[](const size_t bytes) { return std::malloc(bytes); }, [](void* block) { std::free(block); }};
}

// One block from a BuildAllocator, released on destruction. Empty when the
// allocation failed, so every caller checks get() before use.
class AllocatedBuffer {
 public:
  AllocatedBuffer() = default;
  AllocatedBuffer(const BuildAllocator allocator, const size_t bytes) { allocate(allocator, bytes); }
  ~AllocatedBuffer() { reset(); }
  AllocatedBuffer(const AllocatedBuffer&) = delete;
  AllocatedBuffer& operator=(const AllocatedBuffer&) = delete;
  AllocatedBuffer(AllocatedBuffer&& other) noexcept { *this = std::move(other); }
  AllocatedBuffer& operator=(AllocatedBuffer&& other) noexcept {
    if (this != &other) {
      reset();
      allocator_ = other.allocator_;
      block_ = std::exchange(other.block_, nullptr);
      bytes_ = std::exchange(other.bytes_, 0);
    }
    return *this;
  }

  bool allocate(const BuildAllocator allocator, const size_t bytes) {
    reset();
    allocator_ = allocator;
    block_ = bytes > 0 ? allocator.allocate(bytes) : nullptr;
    bytes_ = block_ ? bytes : 0;
    return block_ != nullptr;
  }

  void reset() {
    if (block_) allocator_.release(block_);
    block_ = nullptr;
    bytes_ = 0;
  }

  template <typename T>
  T* as() const {
    return static_cast<T*>(block_);
  }
  void* get() const { return block_; }
  size_t size() const { return bytes_; }

 private:
  BuildAllocator allocator_{};
  void* block_ = nullptr;
  size_t bytes_ = 0;
};

}  // namespace BibleSearch
