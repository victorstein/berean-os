#pragma once

#include <EpdFontFamily.h>

#include <cstdint>

#include "HomeVerseCache.h"
#include "fontIds.h"

class GfxRenderer;

// Home's "From your tags" verse (issue #203): one whole tagged Bible passage a
// day. The date seeds the pick, so every wake of the same day shows the same
// verse with nothing stored; this singleton holds it for the rest of the wake,
// so later Home entries do not scan. Modelled on CatalogIndexStore::getInstance:
// RAM state that outlives the screen that filled it. Never reads or advances the
// sleep screen's recent ring. Owned by the loop task.
class HomeVerse {
 public:
  // The card's rungs, largest first; the gate admits a row that fits the last.
  static constexpr uint8_t RUNG_COUNT = 3;
  static constexpr int FONT_IDS[RUNG_COUNT] = {NOTOSERIF_14_FONT_ID, NOTOSERIF_12_FONT_ID, UI_10_FONT_ID};
  static constexpr EpdFontFamily::Style FONT_STYLES[RUNG_COUNT] = {EpdFontFamily::ITALIC, EpdFontFamily::ITALIC,
                                                                   EpdFontFamily::REGULAR};

  static HomeVerse& getInstance();
  HomeVerse(const HomeVerse&) = delete;
  HomeVerse& operator=(const HomeVerse&) = delete;

  // Whether the held result still answers for today and bible.json as it is now.
  // Costs a clock read and a file stat, never a parse.
  bool isCurrent() const;

  // Scans bible.json when the held result is not current, then holds a pick or
  // why there is none. False on OOM, with nothing held, so the next entry
  // retries. The text box is the card's, in pixels.
  bool ensure(const GfxRenderer& renderer, int textWidth, int textHeight);

  bool hasPick() const { return entry.valid && entry.hasPick; }
  const home_verse::Pick& pick() const { return entry.pick; }
  home_verse::Empty empty() const { return entry.empty; }

 private:
  HomeVerse() = default;

  home_verse::Entry entry;
};

#define HOME_VERSE HomeVerse::getInstance()
