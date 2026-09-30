#pragma once

#include <cstddef>

// Geometry of the reader-menu sheet: bottom-anchored, sized to its content, a
// title row with a close control, a row of quick-action tiles and two columns
// of rows. Plain ints, free of FreeInkUI, Arduino and GfxRenderer so the host
// suite can exercise it (test/ui_layout).
namespace ReaderMenuSheetLayout {

constexpr int MAX_TILES = 4;
// The mockup's Recent row shows three places.
constexpr int MAX_RECENT_CHIPS = 3;
// A chip label: PlacesDoc::MAX_REFERENCE_BYTES plus a terminator (EpubReaderActivity.cpp asserts it).
constexpr int RECENT_LABEL_BYTES = 49;

// Labels for the Recent row, newest first, built by the reader when it opens the menu.
struct RecentChipLabels {
  char text[MAX_RECENT_CHIPS][RECENT_LABEL_BYTES] = {};
  int count = 0;
};

struct Box {
  int x = 0;
  int y = 0;
  int w = 0;
  int h = 0;
  constexpr int right() const { return x + w; }
  constexpr int bottom() const { return y + h; }
};

struct Inputs {
  int safeX = 0;
  int safeY = 0;
  int safeW = 0;
  int safeH = 0;
  int titleHeight = 0;  // ThemeMetrics::headerHeight
  int gap = 0;          // ThemeMetrics::verticalSpacing: between bands, tiles and columns
  int iconSize = 0;
  int labelLineHeight = 0;
  int minTouchSize = 0;
  int rowHeight = 0;  // ListRowHeight::resolve
  int ruleWidth = 0;
  int quickCount = 0;
  int rowCount = 0;
  int recentCount = 0;   // places to offer; 0 draws no band
  int recentHeight = 0;  // the band: at least a touch target
};

struct Layout {
  Box page;
  Box plate;
  Box rule;
  Box title;
  Box close;
  Box tiles[MAX_TILES];
  int tileCount = 0;
  Box recent;
  Box columns[2];
  int rowsPerColumn = 0;
  int rowHeight = 0;
  int tileHeight = 0;
  int gap = 0;
  int iconSize = 0;
  bool fitsOverPage = false;
  bool fitsAlone = false;
};

// Icon over label: a gap above the icon, half a gap between icon and label, a
// gap below the label.
constexpr int tileHeightFor(const Inputs& in) {
  const int stacked = in.iconSize + in.gap / 2 + in.labelLineHeight + 2 * in.gap;
  return stacked > in.minTouchSize ? stacked : in.minTouchSize;
}

inline Layout compute(const Inputs& in) {
  Layout out;
  const int quick = in.quickCount < 0 ? 0 : (in.quickCount > MAX_TILES ? MAX_TILES : in.quickCount);
  const int rows = in.rowCount < 0 ? 0 : in.rowCount;
  out.gap = in.gap;
  out.iconSize = in.iconSize;
  out.rowHeight = in.rowHeight;
  out.tileHeight = tileHeightFor(in);
  out.tileCount = quick;
  out.rowsPerColumn = (rows + 1) / 2;

  const int tilesBand = quick > 0 ? out.tileHeight + in.gap : 0;
  const int recentBand = in.recentCount > 0 ? in.recentHeight + in.gap : 0;
  const int sheetHeight =
      in.ruleWidth + in.titleHeight + in.gap + tilesBand + recentBand + out.rowsPerColumn * in.rowHeight + in.gap;
  const int safeBottom = in.safeY + in.safeH;
  int sheetTop = safeBottom - sheetHeight;
  out.fitsAlone = sheetTop >= in.safeY;
  if (!out.fitsAlone) sheetTop = in.safeY;
  // A third of the height stays page: enough reading context to be worth
  // keeping, and a tap target to close on.
  out.fitsOverPage = out.fitsAlone && (sheetTop - in.safeY) * 3 >= in.safeH;

  out.page = Box{in.safeX, in.safeY, in.safeW, sheetTop - in.safeY};
  out.plate = Box{in.safeX, sheetTop, in.safeW, safeBottom - sheetTop};
  out.rule = Box{in.safeX, sheetTop, in.safeW, in.ruleWidth};

  const int innerX = in.safeX + in.gap;
  const int innerW = in.safeW - 2 * in.gap;
  const int titleY = sheetTop + in.ruleWidth;
  out.close = Box{innerX + innerW - in.titleHeight, titleY, in.titleHeight, in.titleHeight};
  out.title = Box{innerX, titleY, innerW - in.titleHeight - in.gap, in.titleHeight};

  int y = titleY + in.titleHeight + in.gap;
  if (quick > 0) {
    const int tileW = (innerW - (quick - 1) * in.gap) / quick;
    for (int i = 0; i < quick; ++i) {
      out.tiles[i] = Box{innerX + i * (tileW + in.gap), y, tileW, out.tileHeight};
    }
    y += out.tileHeight + in.gap;
  }
  if (in.recentCount > 0) {
    out.recent = Box{innerX, y, innerW, in.recentHeight};
    y += in.recentHeight + in.gap;
  }
  const int columnW = (innerW - in.gap) / 2;
  const int columnH = out.rowsPerColumn * in.rowHeight;
  out.columns[0] = Box{innerX, y, columnW, columnH};
  out.columns[1] = Box{innerX + columnW + in.gap, y, columnW, columnH};
  return out;
}

// How many chips fit one line after the caption, in order: a chip that does not fit ends the row
// rather than letting a later, narrower one jump ahead of it.
constexpr int fitChips(const int captionWidth, const int* chipWidths, const int count, const int bandWidth,
                       const int gap) {
  const int limit = count < MAX_RECENT_CHIPS ? count : MAX_RECENT_CHIPS;
  int x = captionWidth + gap;
  int fitted = 0;
  while (fitted < limit && x + chipWidths[fitted] <= bandWidth) {
    x += chipWidths[fitted] + gap;
    ++fitted;
  }
  return fitted;
}

// readFramebufferRegion copies the panel-oriented, byte-aligned bounding box of
// a screen rect, so a w x h rect needs room for either orientation plus a byte
// of alignment slack on the packed axis.
constexpr size_t snapshotBytes(const int w, const int h) {
  const size_t packedAlongWidth = static_cast<size_t>((w + 15) / 8) * static_cast<size_t>(h);
  const size_t packedAlongHeight = static_cast<size_t>((h + 15) / 8) * static_cast<size_t>(w);
  return packedAlongWidth > packedAlongHeight ? packedAlongWidth : packedAlongHeight;
}

}  // namespace ReaderMenuSheetLayout
