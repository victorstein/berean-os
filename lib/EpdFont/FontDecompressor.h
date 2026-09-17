#pragma once

#include <InflateReader.h>

#include "EpdFontData.h"

class FontDecompressor {
 public:
  static constexpr uint16_t MAX_PAGE_GLYPHS = 512;
  // One slot per distinct EpdFontData prewarmed in a scan pass — NOT one per
  // style. Four is enough because only the compressed reading families take a
  // slot at all: the status bar and UI fonts are uncompressed (groups ==
  // nullptr) and are filtered out in FontCacheManager::prewarmCache, SD fonts
  // take the SdCardFont path, and one reading family is on screen at a time, so
  // one family x four styles is the ceiling. Drawing a second compressed family
  // on one screen breaks that; FontCacheManager logs which font it refused.
  static constexpr uint8_t MAX_PAGE_SLOTS = 4;

  FontDecompressor() = default;
  ~FontDecompressor();

  bool init();
  void deinit();

  // Returns pointer to decompressed bitmap data for the given glyph.
  // Checks the page buffer (from prewarm) first, then falls back to the hot group slot.
  const uint8_t* getBitmap(const EpdFontData* fontData, const EpdGlyph* glyph, uint32_t glyphIndex);

  // Free all cached data (page buffer + hot group).
  void clearCache();

  // Page slots currently held. Live state, deliberately not in Stats: resetStats()
  // would wipe it. Used by the host slot-accounting suite and for diagnosis.
  uint8_t usedPageSlots() const { return pageSlotCount; }

  // Pre-scan UTF-8 text and extract needed glyph bitmaps into a flat page buffer.
  // Each group is decompressed once into a temp buffer; only needed glyphs are kept.
  // Returns:
  //   -1  no free slot. Nothing allocated; the caller logs it (it knows the font).
  //    0  nothing to do (uncompressed font, empty text, or no needed glyph),
  //       already warm, or fully prewarmed. "Already warm" does NOT re-scan the
  //       new text, so glyphs the first call did not need come from the hot group.
  //   >0  number of glyphs that couldn't be loaded.
  int prewarmCache(const EpdFontData* fontData, const char* utf8Text);

  struct Stats {
    uint32_t cacheHits = 0;
    uint32_t cacheMisses = 0;
    uint32_t decompressTimeMs = 0;
    uint16_t uniqueGroupsAccessed = 0;
    uint32_t pageBufferBytes = 0;  // pageBuffer allocation
    uint32_t pageGlyphsBytes = 0;  // pageGlyphs lookup table allocation
    uint32_t hotGroupBytes = 0;    // current hot group allocation
    uint32_t peakTempBytes = 0;    // largest temp buffer in prewarm
    uint32_t getBitmapTimeUs = 0;  // cumulative getBitmap time (micros)
    uint32_t getBitmapCalls = 0;   // number of getBitmap calls
  };
  void logStats(const char* label = "FDC");
  void resetStats();
  const Stats& getStats() const { return stats; }

 private:
  Stats stats;
  InflateReader inflateReader;

  // Page buffer slots: each distinct font gets its own flat glyph buffer with
  // sorted lookup. Up to MAX_PAGE_SLOTS distinct fonts can be prewarmed
  // simultaneously; prewarmCache() returns early if one is already resident.
  struct PageGlyphEntry {
    uint32_t glyphIndex;
    uint32_t bufferOffset;
    uint32_t alignedOffset;  // byte-aligned offset within its decompressed group (set during prewarm pre-scan)
  };
  struct PageSlot {
    uint8_t* buffer = nullptr;
    const EpdFontData* fontData = nullptr;
    PageGlyphEntry* glyphs = nullptr;
    uint16_t glyphCount = 0;
  };
  PageSlot pageSlots[MAX_PAGE_SLOTS] = {};
  uint8_t pageSlotCount = 0;

  // Hot group: last decompressed group (byte-aligned) for non-prewarmed fallback path.
  // Kept in byte-aligned format; individual glyphs are compacted on demand into hotGlyphBuf.
  // Nothrow high-water malloc buffers, NOT std::vector: getBitmap() runs on the render path,
  // and under -fno-exceptions a vector resize that hits OOM abort()s the firmware instead of
  // failing (field crash: hotGroup.resize() -> std::bad_alloc -> abort with ~11 KB free).
  // ensureCapacity() returns false on OOM so the caller can skip the glyph gracefully.
  const EpdFontData* hotGroupFont = nullptr;
  uint16_t hotGroupIndex = UINT16_MAX;
  uint8_t* hotGroup = nullptr;  // owned; freed in freeHotGroup()/dtor
  uint32_t hotGroupCapacity = 0;

  // Scratch buffer for compacting a single glyph from the hot group.
  // Valid until the next getBitmap() call. Same ownership/OOM contract as hotGroup.
  uint8_t* hotGlyphBuf = nullptr;
  uint32_t hotGlyphBufCapacity = 0;

  // Grow (never shrink) an owned buffer to at least `needed` bytes; false on OOM, buffer freed.
  static bool ensureCapacity(uint8_t*& buf, uint32_t& capacity, uint32_t needed);

  void freePageBuffer();
  void freeHotGroup();
  uint16_t getGroupIndex(const EpdFontData* fontData, uint32_t glyphIndex);
  uint32_t getAlignedOffset(const EpdFontData* fontData, uint16_t groupIndex, uint32_t glyphIndex);
  bool decompressGroup(const EpdFontData* fontData, uint16_t groupIndex, uint8_t* outBuf, uint32_t outSize);
  static void compactSingleGlyph(const uint8_t* alignedSrc, uint8_t* packedDst, uint8_t width, uint8_t height);
  static int32_t findGlyphIndex(const EpdFontData* fontData, uint32_t codepoint);
};
