#pragma once

#include <cstddef>
#include <cstdint>

// The reader's saved position, read without opening the reader. The layout and
// its quirks are EpubReaderActivity's: u16 spine, u16 page, then an optional u16
// page count and u32 text offset, little-endian, 4, 6 or 10 bytes.
struct SavedProgress {
  uint16_t spine = 0;
  uint16_t page = 0;
  uint16_t pageCount = 0;
};

inline bool parseProgressBytes(const uint8_t* data, const size_t len, SavedProgress& out) {
  if (data == nullptr || (len != 4 && len != 6 && len != 10)) return false;
  out = SavedProgress{};
  out.spine = static_cast<uint16_t>(data[0] | (data[1] << 8));
  const auto page = static_cast<uint16_t>(data[2] | (data[3] << 8));
  // The reader's stale last-page sentinel; it reopens such a file at page 0.
  out.page = page == UINT16_MAX ? 0 : page;
  if (len >= 6) out.pageCount = static_cast<uint16_t>(data[4] | (data[5] << 8));
  return true;
}

// 0 when there is no page count: a legacy file, or the reader's save after a
// footnote return, which records the chapter and not the page.
inline float chapterFraction(const SavedProgress& progress) {
  if (progress.pageCount == 0) return 0.0f;
  if (progress.page >= progress.pageCount) return 1.0f;
  return static_cast<float>(progress.page) / static_cast<float>(progress.pageCount);
}

// Clamped as a float first: a corrupt spine can make calculateProgress enormous,
// and converting an out-of-range float to int is undefined.
inline int roundPercent(float bookFraction) {
  if (!(bookFraction > 0.0f)) return 0;
  if (bookFraction > 1.0f) bookFraction = 1.0f;
  return static_cast<int>(bookFraction * 100.0f + 0.5f);
}
