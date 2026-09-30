#pragma once

#include <algorithm>

#include "components/MastheadLayout.h"
#include "components/themes/BaseTheme.h"

// Where every Home section sits (issue #203), free of the renderer so
// test/home_layout pins it against the real theme tables. The fixed sections are
// sized from line heights and metrics; the cover hero takes what is left, the
// way the launcher's Bible tile absorbed the remainder before it.
namespace HomeLayout {

using Box = MastheadLayout::Box;

constexpr bool contains(const Box& box, const int px, const int py) {
  return px >= box.x && px < box.x + box.width && py >= box.y && py < box.y + box.height;
}

constexpr int bottomOf(const Box& box) { return box.y + box.height; }

inline constexpr int PAD = 8;
inline constexpr int ICON = 32;
// Between the verse card's label, text box and reference.
inline constexpr int CARD_GAP = 4;
inline constexpr int RADIUS = 8;
// Below this the hero's cover is a sliver above its plate.
inline constexpr int MIN_HERO_ART = 96;
inline constexpr int RECENT_SLOTS = 3;
inline constexpr int VERSE_TEXT_LINES = 3;
inline constexpr int STRIP_CELLS = 7;
inline constexpr int STRIP_CELL = 26;
inline constexpr int STRIP_DOT = 6;
inline constexpr int ICON_TILES = 4;

struct LineHeights {
  int small = 0;
  int ui10 = 0;
  int serif12 = 0;
  int serif14 = 0;
};

struct Insets {
  int top = 0;
  int right = 0;
  int bottom = 0;
  int left = 0;
};

struct Layout {
  // The Bible header when there is no cover to put it on.
  Box fallbackHeader;
  Box hero;
  Box plate;
  Box plateHeader;
  Box continueButton;
  Box goToButton;
  // Continue and Go to together, for the single Download button.
  Box buttonRow;
  Box recentLabel;
  Box recent[RECENT_SLOTS];
  Box verseCard;
  Box verseLabel;
  Box verseText;
  Box verseReference;
  Box meetings;
  Box meetingsIcon;
  Box meetingsTitle;
  Box meetingsPercent;
  Box strip;
  Box meetingsRange;
  Box icons[ICON_TILES];
};

constexpr int iconRowHeight(const LineHeights& lines) { return ICON + lines.small + 3 * PAD; }

constexpr int stripHeight(const LineHeights& lines) { return lines.small + lines.ui10 + PAD; }

// Title and % beside the strip, then the full-width range row below the strip's
// foot, so the meeting-day dots never sit over the range text.
constexpr int meetingsHeight(const LineHeights& lines) {
  return PAD + std::max(lines.ui10 + lines.small, stripHeight(lines)) + lines.small + PAD;
}

constexpr int verseTextHeight(const LineHeights& lines) { return VERSE_TEXT_LINES * lines.serif12; }

constexpr int verseCardHeight(const LineHeights& lines) {
  return lines.small + verseTextHeight(lines) + lines.small + 2 * PAD + 2 * CARD_GAP;
}

constexpr int recentHeight(const ThemeMetrics& metrics, const LineHeights& lines) {
  return lines.small + RECENT_SLOTS * metrics.listRowHeight;
}

constexpr int plateHeight(const ThemeMetrics& metrics, const LineHeights& lines) {
  return metrics.headerHeight + 1 + PAD + (lines.ui10 + 2 * PAD) + PAD;
}

constexpr Layout compute(const int screenWidth, const int screenHeight, const Insets& insets,
                         const ThemeMetrics& metrics, const LineHeights& lines) {
  const int gap = metrics.verticalSpacing;
  const int top = insets.top + metrics.topPadding + PAD;
  const int bottom = screenHeight - insets.bottom - metrics.topPadding;
  const int fixed =
      recentHeight(metrics, lines) + verseCardHeight(lines) + meetingsHeight(lines) + iconRowHeight(lines);
  const int heroHeight = bottom - top - fixed - 4 * gap;

  Layout out{};
  // The masthead band's left edge and width, so the hero asks CoverBand for the
  // thumbnail the masthead and the sleep screen already share.
  out.hero =
      MastheadLayout::band(screenWidth, insets.top + PAD, insets.right, insets.left, metrics.topPadding, heroHeight);
  // Full width and inset-free like every other screen's header, but PAD lower so Home breathes at the top.
  out.fallbackHeader = Box{0, metrics.topPadding + PAD, screenWidth, metrics.headerHeight};
  const int left = out.hero.x;
  const int width = out.hero.width;

  const int plate = plateHeight(metrics, lines);
  out.plate = Box{left, bottomOf(out.hero) - plate, width, plate};
  out.plateHeader = Box{left, out.plate.y + 1, width, metrics.headerHeight};
  const int buttonHeight = lines.ui10 + 2 * PAD;
  out.buttonRow = Box{left + PAD, bottomOf(out.plateHeader) + PAD, width - 2 * PAD, buttonHeight};
  const int continueWidth = (width - 3 * PAD) * 2 / 3;
  out.continueButton = Box{left + PAD, out.buttonRow.y, continueWidth, buttonHeight};
  out.goToButton = Box{left + 2 * PAD + continueWidth, out.buttonRow.y, width - 3 * PAD - continueWidth, buttonHeight};

  int y = bottomOf(out.hero) + gap;
  out.recentLabel = Box{left, y, width, lines.small};
  for (int i = 0; i < RECENT_SLOTS; ++i) {
    out.recent[i] = Box{left, y + lines.small + i * metrics.listRowHeight, width, metrics.listRowHeight};
  }
  y += recentHeight(metrics, lines) + gap;

  out.verseCard = Box{left, y, width, verseCardHeight(lines)};
  out.verseLabel = Box{left + PAD, y + PAD, width - 2 * PAD, lines.small};
  out.verseText = Box{left + PAD, bottomOf(out.verseLabel) + CARD_GAP, width - 2 * PAD, verseTextHeight(lines)};
  out.verseReference = Box{left + PAD, bottomOf(out.verseText) + CARD_GAP, width - 2 * PAD, lines.small};
  y += verseCardHeight(lines) + gap;

  out.meetings = Box{left, y, width, meetingsHeight(lines)};
  out.meetingsIcon = Box{left + PAD, y + PAD, ICON, ICON};
  const int stripWidth = STRIP_CELLS * STRIP_CELL;
  out.strip = Box{left + width - PAD - stripWidth, y + PAD, stripWidth, stripHeight(lines)};
  const int textX = left + PAD + ICON + PAD;
  const int textWidth = out.strip.x - PAD - textX;
  out.meetingsTitle = Box{textX, y + PAD, textWidth, lines.ui10};
  out.meetingsPercent = Box{textX, bottomOf(out.meetingsTitle), textWidth, lines.small};
  out.meetingsRange = Box{left + PAD, bottomOf(out.strip), width - 2 * PAD, lines.small};
  y += meetingsHeight(lines) + gap;

  const int tileWidth = (width - (ICON_TILES - 1) * PAD) / ICON_TILES;
  for (int i = 0; i < ICON_TILES; ++i) {
    const int x = left + i * (tileWidth + PAD);
    // The last tile takes the division remainder so the row ends flush.
    const int w = i == ICON_TILES - 1 ? left + width - x : tileWidth;
    out.icons[i] = Box{x, y, w, iconRowHeight(lines)};
  }
  return out;
}

}  // namespace HomeLayout
