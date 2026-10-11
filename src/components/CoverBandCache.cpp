#include "CoverBandCache.h"

#include <Logging.h>
#include <esp_heap_caps.h>

#include <atomic>
#include <cstring>

namespace {

constexpr const char* MODULE = "COVER_BAND";

// Namespace scope and constant-initialised, so a bump from another task never
// races the singleton's construction.
std::atomic<uint32_t> coverEpoch{0};

bool sameBand(const Rect& a, const Rect& b) {
  return a.x == b.x && a.y == b.y && a.width == b.width && a.height == b.height;
}

bool sameStyle(const CoverBand::Style& a, const CoverBand::Style& b) {
  return a.focusBand == b.focusBand && a.cornerRadius == b.cornerRadius && a.plateHeight == b.plateHeight;
}

}  // namespace

CoverBandCache& CoverBandCache::getInstance() {
  static CoverBandCache instance;
  return instance;
}

void CoverBandCache::PsramFree::operator()(uint8_t* p) const { heap_caps_free(p); }

void CoverBandCache::noteCoversChanged() { coverEpoch.fetch_add(1); }

bool CoverBandCache::isLive(const Slot& slot) const { return slot.valid && slot.epoch == coverEpoch.load(); }

const uint8_t* CoverBandCache::find(const std::string& coverPath, const Rect& band, const CoverBand::Style& style) {
  for (Slot& slot : slots) {
    if (isLive(slot) && slot.coverPath == coverPath && sameBand(slot.band, band) && sameStyle(slot.style, style)) {
      slot.lastUse = ++useClock;
      return slot.bits.get();
    }
  }
  return nullptr;
}

CoverBandCache::Slot& CoverBandCache::victim() {
  Slot* oldest = &slots[0];
  for (Slot& slot : slots) {
    if (!isLive(slot)) return slot;
    if (slot.lastUse < oldest->lastUse) oldest = &slot;
  }
  return *oldest;
}

uint8_t* CoverBandCache::reserve(const std::string& coverPath, const Rect& band, const CoverBand::Style& style,
                                 const size_t bytes) {
  abandon();
  if (bytes == 0) return nullptr;
  if (bytes > MAX_SLOT_BYTES) {
    LOG_ERR(MODULE, "Band ink %u B over the %u B cap; not held", static_cast<unsigned>(bytes),
            static_cast<unsigned>(MAX_SLOT_BYTES));
    return nullptr;
  }

  // Read before the blit opens the cover: see noteCoversChanged.
  const uint32_t epoch = coverEpoch.load();
  Slot& slot = victim();
  slot.valid = false;
  if (slot.bytes != bytes) {
    slot.bits.reset();
    slot.bytes = 0;
    slot.bits.reset(static_cast<uint8_t*>(heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)));
    if (!slot.bits) {
      LOG_ERR(MODULE, "OOM: %u B band ink in PSRAM", static_cast<unsigned>(bytes));
      return nullptr;
    }
    slot.bytes = bytes;
  }
  memset(slot.bits.get(), 0, bytes);
  slot.coverPath = coverPath;
  slot.band = band;
  slot.style = style;
  slot.epoch = epoch;
  reserved = &slot;
  return slot.bits.get();
}

void CoverBandCache::commit() {
  if (!reserved) return;
  reserved->valid = true;
  reserved->lastUse = ++useClock;
  LOG_DBG(MODULE, "Held band %dx%d in slot %u: %u B, %u B held", reserved->band.width, reserved->band.height,
          static_cast<unsigned>(reserved - slots), static_cast<unsigned>(reserved->bytes),
          static_cast<unsigned>(bytesHeld()));
  reserved = nullptr;
}

void CoverBandCache::abandon() {
  if (!reserved) return;
  reserved->valid = false;
  reserved = nullptr;
}

size_t CoverBandCache::bytesHeld() const {
  size_t total = 0;
  for (const Slot& slot : slots) total += slot.bytes;
  return total;
}

void CoverBandCache::clear() {
  reserved = nullptr;
  for (Slot& slot : slots) slot = Slot();
  useClock = 0;
}
