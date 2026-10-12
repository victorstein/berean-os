#pragma once

#include <algorithm>

// Geometry for BaseTheme::drawHeader and drawHeaderWithAction, free of the
// renderer and FreeInkUI so it can be host-tested
// (test/ui_layout/HeaderLayoutTest.cpp). All coordinates are absolute.
namespace HeaderLayout {

struct Box {
  int x;
  int y;
  int width;
  int height;
};

// FreeInkUI's own trailing header button is the band height less twice this,
// inset this far from the band's top (header.h).
constexpr int ACTION_INSET = 4;
// A detached battery hugs the corner of its strip at the legacy inset.
constexpr int DETACHED_BATTERY_INSET = 12;

// TagChipView::measure's width, reproduced because the theme cannot include
// reader code: the bold label plus spaceLg each side, never narrower than a
// chip is tall (TagChips::chipHeight).
constexpr int actionWidth(const int boldTextWidth, const int textLineHeight, const int spaceLg, const int spaceMd,
                          const int minTouchSize) {
  const int chipHeight = std::max(textLineHeight + 2 * spaceMd, minTouchSize);
  return std::max(boldTextWidth + 2 * spaceLg, chipHeight);
}

struct Inputs {
  Box band{0, 0, 0, 0};
  int sidePadding = 0;
  int underline = 0;
  int spaceMd = 0;
  int titleLineHeight = 0;
  int batteryReserve = 0;
  int batteryBarHeight = 0;
  bool batteryLeft = false;
  bool batteryDetached = false;
  bool titleCentred = false;
  // 0 lays out drawHeader's header, with no action.
  int actionWidth = 0;
};

struct Result {
  int leftReserve = 0;
  int rightReserve = 0;
  int titleOffsetY = 0;
  Box battery{0, 0, 0, 0};
  Box action{0, 0, 0, 0};
};

constexpr Result layout(const Inputs& in) {
  const Box& band = in.band;
  const int bandRight = band.x + band.width;
  Result out;

  if (in.batteryDetached) {
    // The battery has its own corner strip, so the title owns the lower
    // sub-band's full width, anchored with clearance above the bottom rule;
    // naive sub-band centring left the glyphs nearly touching it.
    const int titleTop = band.height - in.underline - in.spaceMd - in.titleLineHeight;
    out.titleOffsetY = titleTop - (band.height - in.titleLineHeight) / 2;
  } else if (in.batteryLeft) {
    out.leftReserve = in.batteryReserve + in.spaceMd;
  } else {
    out.rightReserve = in.batteryReserve + in.spaceMd;
  }

  const int batteryInset = in.batteryDetached ? DETACHED_BATTERY_INSET : in.sidePadding;
  const int batteryX = in.batteryLeft ? band.x + batteryInset : bandRight - batteryInset - in.batteryReserve;
  out.battery = Box{batteryX, band.y, in.batteryReserve, in.batteryBarHeight};

  if (in.actionWidth <= 0) return out;

  const bool batteryRight = !in.batteryLeft;
  const bool sharesTitleLine = batteryRight && !in.batteryDetached;
  const bool corner = sharesTitleLine && in.batteryBarHeight < band.height;
  const bool besideBattery = sharesTitleLine && !corner;
  const int roomBottom = band.y + band.height - in.underline;
  const int roomTop = (corner || (in.batteryDetached && batteryRight)) ? band.y + in.batteryBarHeight : band.y;

  int actionRight = bandRight - in.sidePadding;
  int growth = in.actionWidth + in.spaceMd;
  if (besideBattery) {
    actionRight -= in.batteryReserve + in.spaceMd;
  } else if (corner) {
    // The strip above already reserves the battery's columns; the action
    // shares them at a different height, so the title gives up only the excess.
    growth = std::max(0, in.actionWidth - in.batteryReserve);
  }
  out.rightReserve += growth;
  // Equal growth on both sides keeps a centred title where drawHeader centres it.
  if (in.titleCentred) out.leftReserve += growth;

  const int height = std::max(0, std::min(band.height - 2 * ACTION_INSET, roomBottom - roomTop));
  int y = band.y + ACTION_INSET;
  if (corner) {
    y = roomBottom - height;
  } else if (in.batteryDetached) {
    const int titleLineTop = band.y + band.height - in.underline - in.spaceMd - in.titleLineHeight;
    y = titleLineTop + (in.titleLineHeight - height) / 2;
  }
  y = std::min(std::max(y, roomTop), roomBottom - height);
  out.action = Box{actionRight - in.actionWidth, y, in.actionWidth, height};
  return out;
}

}  // namespace HeaderLayout
