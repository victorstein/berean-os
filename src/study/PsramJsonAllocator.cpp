#include "PsramJsonAllocator.h"

#include <Logging.h>
#include <esp_heap_caps.h>

namespace {

constexpr uint32_t PSRAM_CAPS = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;

class PsramJsonDocumentAllocator : public ArduinoJson::Allocator {
 public:
  void* allocate(const size_t size) override { return heap_caps_malloc(size, PSRAM_CAPS); }
  void deallocate(void* pointer) override { heap_caps_free(pointer); }
  void* reallocate(void* pointer, const size_t newSize) override {
    return heap_caps_realloc(pointer, newSize, PSRAM_CAPS);
  }
};

void* allocateText(const size_t bytes) { return heap_caps_malloc(bytes, PSRAM_CAPS); }
void releaseText(void* block) { heap_caps_free(block); }

}  // namespace

namespace PsramJsonAllocator {

ArduinoJson::Allocator* json() {
  static PsramJsonDocumentAllocator instance;
  return &instance;
}

study::TextAllocator text() { return {allocateText, releaseText}; }

study::PassageDoc::Allocators passageDoc() { return {text(), json()}; }

void logMemory(const char* when) {
  LOG_DBG("MEM", "%s: internal free %u (min %u), PSRAM free %u", when,
          static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
          static_cast<unsigned>(heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL)),
          static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)));
}

}  // namespace PsramJsonAllocator
