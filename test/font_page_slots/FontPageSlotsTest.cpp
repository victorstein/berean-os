#include <gtest/gtest.h>

#include <FontCacheManager.h>
#include <FontDecompressor.h>
#include <EpdFontFamily.h>

#include <map>

#include "builtinFonts/notoserif_12_regular.h"
#include "builtinFonts/notosans_8_regular.h"

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
