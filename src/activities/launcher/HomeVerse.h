#pragma once

#include <EpdFontFamily.h>

#include <cstdint>

#include "HomeVerseCache.h"
#include "fontIds.h"

class GfxRenderer;

// Home's "From your tags" verse (issue #203): one whole tagged Bible passage a
// day. The date seeds the pick, so every wake of the same day shows the same
// verse. This singleton holds it for the rest of the wake, so later Home entries
// do not scan, and HomeVerseFile keeps the last completed scan on the card, so a
// wake on the same day, file and build does not scan either. Modelled on
// CatalogIndexStore::getInstance: RAM state that outlives the screen that filled
// it. Never reads or advances the sleep screen's recent ring. Owned by the loop
// task.
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
  // The first call of a boot also restores the saved scan; every call costs a
  // clock read and a file stat, never a scan.
  bool isCurrent();

  // Scans bible.json when the held result is not current, then holds a pick or
  // why there is none, and saves it. False on OOM, with nothing held or saved,
  // so the next entry retries. The text box is the card's, in pixels.
  bool ensure(const GfxRenderer& renderer, int textWidth, int textHeight);

  bool hasPick() const { return entry.valid && entry.hasPick; }
  const home_verse::Pick& pick() const { return entry.pick; }
  home_verse::Empty empty() const { return entry.empty; }

 private:
  HomeVerse() = default;

  // Before any scan of the boot, so a restore never replaces a fresher result.
  void restoreOnce(const home_verse::Key& key);
  // False on OOM, with the entry invalid.
  bool scan(const GfxRenderer& renderer, int textWidth, int textHeight, const home_verse::Key& key);
  void save() const;

  home_verse::Entry entry;
  bool restoreAttempted = false;
};

#define HOME_VERSE HomeVerse::getInstance()
