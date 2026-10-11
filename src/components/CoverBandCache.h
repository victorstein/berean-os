#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include "components/CoverBand.h"

// The ink of the last few cover bands drawn, held in PSRAM for the rest of the
// wake, so a repeat paint replays it instead of streaming the cover from SD.
// Modelled on HomeVerse (RAM state that outlives the screen that filled it) and
// CatalogIndexStore (explicit PSRAM). The slots belong to the render task; only
// the epoch, through noteCoversChanged, is touched from other tasks.
class CoverBandCache {
 public:
  // Home's hero and the Go to, Publications and Meetings mastheads.
  static constexpr uint8_t SLOTS = 4;
  // Home's hero needs at most 16,240 B; a band past this is drawn fresh every time.
  static constexpr size_t MAX_SLOT_BYTES = 24 * 1024;

  static CoverBandCache& getInstance();
  CoverBandCache(const CoverBandCache&) = delete;
  CoverBandCache& operator=(const CoverBandCache&) = delete;

  // The ink held for exactly these arguments under the current epoch, or null.
  const uint8_t* find(const std::string& coverPath, const Rect& band, const CoverBand::Style& style);

  // A zeroed buffer of `bytes` to record a draw into, or null when over the cap or
  // out of PSRAM. The slot holds nothing until commit(); abandon() drops it.
  uint8_t* reserve(const std::string& coverPath, const Rect& band, const CoverBand::Style& style, size_t bytes);
  void commit();
  void abandon();

  // PSRAM held by every slot, held band or not.
  size_t bytesHeld() const;

  // Frees every slot. For a test fixture: the firmware never needs it.
  void clear();

  // Every held band stops matching. Any task. Call after the change that can
  // leave a different thumbnail at a path, never before it: a draw that read the
  // epoch first and the pixels second would otherwise hold the old pixels under
  // the new epoch.
  static void noteCoversChanged();

 private:
  CoverBandCache() = default;

  // heap_caps_malloc is the only way to demand PSRAM; makeUniqueNoThrow's
  // operator new takes no capability argument. The deleter keeps RAII.
  struct PsramFree {
    void operator()(uint8_t* p) const;
  };
  using PsramBuffer = std::unique_ptr<uint8_t[], PsramFree>;

  struct Slot {
    std::string coverPath;
    Rect band;
    CoverBand::Style style;
    uint32_t epoch = 0;
    uint32_t lastUse = 0;
    PsramBuffer bits;
    size_t bytes = 0;
    bool valid = false;
  };

  bool isLive(const Slot& slot) const;
  Slot& victim();

  Slot slots[SLOTS];
  Slot* reserved = nullptr;
  uint32_t useClock = 0;
};
