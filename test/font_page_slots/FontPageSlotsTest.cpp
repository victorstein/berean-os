#include <EpdFontFamily.h>
#include <FontCacheManager.h>
#include <FontDecompressor.h>
#include <gtest/gtest.h>

#include <iterator>
#include <map>
#include <vector>

#include "builtinFonts/notosans_8_regular.h"
#include "builtinFonts/notoserif_12_regular.h"

// A page slot holds one font's glyph bitmaps for one screenful of text. Four
// exist (FontDecompressor::MAX_PAGE_SLOTS), and the only releaser is
// clearCache(). Issue #58: the Text-settings preview prewarmed on every setting
// change and never released, so the cap was exhausted in four changes.
//
// Fixtures are real generated font headers, not hand-rolled structs:
//   notoserif_12_regular — COMPRESSED (groups, groupCount 13), the smallest
//     header in builtinFonts/ at 270,097 bytes. Takes a slot.
//   notosans_8_regular   — UNCOMPRESSED (groups == nullptr). Never takes a slot;
//     this is the status-bar font, and the fact that it cannot take one is why
//     four slots is enough.
// Cases needing several distinct fonts copy notoserif_12_regular by value: same
// content, distinct addresses, which is all pointer-keyed slot accounting sees.

namespace {

// A Latin-1-bearing pangram, so the glyphs span more than one compressed group
// and the extraction loop is really exercised. Mirrors the Spanish reader-font
// preview string (lib/I18n/translations/spanish.yaml:94).
constexpr const char* SAMPLE = "Benjamín pidió una bebida de kiwi y fresa.";

}  // namespace

TEST(FontPageSlots, UncompressedFontTakesNoSlot) {
  FontDecompressor decompressor;

  // FontDecompressor.cpp:252 returns before the cap check for a null groups
  // pointer, so an uncompressed font never allocates and never consumes a slot.
  EXPECT_EQ(decompressor.prewarmCache(&notosans_8_regular, SAMPLE), 0);
  EXPECT_EQ(decompressor.getStats().pageBufferBytes, 0u);
}

TEST(FontPageSlots, ScopeReleasesEverySlot) {
  FontDecompressor decompressor;
  EpdFont regular(&notoserif_12_regular);
  EpdFontFamily family(&regular);

  std::map<int, EpdFontFamily> fonts;
  fonts.emplace(1, family);
  const std::map<int, SdCardFont*> noSdFonts;

  FontCacheManager manager(fonts, noSdFonts);
  manager.setFontDecompressor(&decompressor);

  {
    auto scope = manager.createPrewarmScope();
    manager.prewarmCache(1, SAMPLE, 0x01);
    EXPECT_EQ(decompressor.usedPageSlots(), 1);
  }

  // PrewarmScope's destructor calls clearCache() (FontCacheManager.cpp:131),
  // which is what keeps the reader's slot count at zero between renders.
  EXPECT_EQ(decompressor.usedPageSlots(), 0);
}

TEST(FontPageSlots, OneSlotPerDistinctFontData) {
  FontDecompressor decompressor;

  ASSERT_EQ(decompressor.prewarmCache(&notoserif_12_regular, SAMPLE), 0);
  ASSERT_EQ(decompressor.usedPageSlots(), 1);

  // getBitmap() breaks after the first slot matching fontData
  // (FontDecompressor.cpp:173), so a second slot for the same font is dead
  // weight: unreachable, and its glyphs fall through to the hot group anyway.
  EXPECT_EQ(decompressor.prewarmCache(&notoserif_12_regular, SAMPLE), 0);
  EXPECT_EQ(decompressor.usedPageSlots(), 1);
}

TEST(FontPageSlots, AlreadyWarmDoesNotReallocate) {
  FontDecompressor decompressor;

  ASSERT_EQ(decompressor.prewarmCache(&notoserif_12_regular, SAMPLE), 0);
  const uint32_t bytesAfterFirst = decompressor.getStats().pageBufferBytes;
  ASSERT_GT(bytesAfterFirst, 0u);

  decompressor.prewarmCache(&notoserif_12_regular, SAMPLE);

  // Asserted as a relation, not an absolute: regenerating a font header would
  // change the byte count but must never make a repeat call allocate again.
  EXPECT_EQ(decompressor.getStats().pageBufferBytes, bytesAfterFirst);
}

TEST(FontPageSlots, StyleFallbackCollapsesToOneSlot) {
  FontDecompressor decompressor;
  EpdFont regular(&notoserif_12_regular);
  EpdFontFamily regularOnly(&regular);  // no bold, italic or bold-italic

  std::map<int, EpdFontFamily> fonts;
  fonts.emplace(1, regularOnly);
  const std::map<int, SdCardFont*> noSdFonts;

  FontCacheManager manager(fonts, noSdFonts);
  manager.setFontDecompressor(&decompressor);

  // EpdFontFamily::getFont falls back to regular for an absent style
  // (EpdFontFamily.cpp:8-18), so all four mask bits resolve to one EpdFontData.
  // Not a state this firmware can reach — every compressed family ships four
  // styles — but it pins the FCM -> EpdFontFamily -> FD composition.
  manager.prewarmCache(1, SAMPLE, 0x0F);

  EXPECT_EQ(decompressor.usedPageSlots(), 1);
}

TEST(FontPageSlots, SlotsFullIsVisibleToTheCaller) {
  FontDecompressor decompressor;

  // Distinct addresses, identical content — all pointer-keyed accounting sees.
  EpdFontData fonts[FontDecompressor::MAX_PAGE_SLOTS + 1];
  for (auto& font : fonts) font = notoserif_12_regular;

  for (uint8_t i = 0; i < FontDecompressor::MAX_PAGE_SLOTS; i++) {
    EXPECT_EQ(decompressor.prewarmCache(&fonts[i], SAMPLE), 0) << "slot " << int{i};
  }
  ASSERT_EQ(decompressor.usedPageSlots(), FontDecompressor::MAX_PAGE_SLOTS);

  // The cap check is before the increment (FontDecompressor.cpp:255), so the
  // last slot allocates and only the one past it is refused. -1 is the
  // slots-full sentinel; nothing is allocated and no slot is consumed.
  EXPECT_LT(decompressor.prewarmCache(&fonts[FontDecompressor::MAX_PAGE_SLOTS], SAMPLE), 0);
  EXPECT_EQ(decompressor.usedPageSlots(), FontDecompressor::MAX_PAGE_SLOTS);
}

TEST(FontPageSlots, TheCapCoversOneFamilysFourStyles) {
  // Why four is enough: only the eight compressed Noto reading families take a
  // slot, the status bar and UI fonts are uncompressed, SD fonts take the
  // SdCardFont path, and one reading family is on screen at a time. So the
  // ceiling is one family's four styles. If a second compressed family is ever
  // drawn on one screen this stops being true — and FontCacheManager will log
  // which font it refused.
  static_assert(FontDecompressor::MAX_PAGE_SLOTS >= 4, "one reading family's R/B/I/BI must fit simultaneously");
  EXPECT_GE(FontDecompressor::MAX_PAGE_SLOTS, 4);
}

TEST(FontPageSlots, PreviewLoopDoesNotAccumulate) {
  FontDecompressor decompressor;

  EpdFontData fonts[5];
  for (auto& font : fonts) font = notoserif_12_regular;

  std::map<int, EpdFontFamily> fontMap;
  std::vector<EpdFont> owned;
  owned.reserve(std::size(fonts));
  for (size_t i = 0; i < std::size(fonts); i++) {
    owned.emplace_back(&fonts[i]);
  }
  for (size_t i = 0; i < std::size(fonts); i++) {
    fontMap.emplace(static_cast<int>(i), EpdFontFamily(&owned[i]));
  }
  const std::map<int, SdCardFont*> noSdFonts;

  FontCacheManager manager(fontMap, noSdFonts);
  manager.setFontDecompressor(&decompressor);

  // The Text-settings preview shape: prewarm once per setting change, outside
  // any PrewarmScope. Without a release the count climbs 1,2,3,4 and the fifth
  // change is refused — issue #58. Releasing first bounds it at one generation.
  for (size_t i = 0; i < std::size(fonts); i++) {
    manager.releaseBuiltinGlyphCache();
    manager.prewarmCache(static_cast<int>(i), SAMPLE, 0x01);
    EXPECT_EQ(decompressor.usedPageSlots(), 1) << "after change " << i;
  }
}
