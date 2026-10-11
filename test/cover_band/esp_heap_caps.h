#pragma once

// Host stand-in for ESP-IDF's <esp_heap_caps.h>, for CoverBandCache.cpp. PSRAM is
// plain heap here; heap_caps_stub::failAll makes every allocation fail, for the
// out-of-PSRAM case.

#include <cstddef>
#include <cstdint>
#include <cstdlib>

#define MALLOC_CAP_8BIT (1 << 2)
#define MALLOC_CAP_SPIRAM (1 << 10)

namespace heap_caps_stub {
inline bool failAll = false;
}  // namespace heap_caps_stub

inline void* heap_caps_malloc(const size_t size, uint32_t) { return heap_caps_stub::failAll ? nullptr : malloc(size); }

inline void heap_caps_free(void* p) { free(p); }
