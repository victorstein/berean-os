#pragma once

#include <algorithm>

// Crop and sizing arithmetic for CoverBand, free of the renderer so it can be
// host-tested (test/cover_band/CoverBandGeometryTest.cpp).
namespace CoverBandGeometry {

// A cover must be at least as large as the band in both axes to fill it without
// upscaling. Covers run roughly 0.6-0.75 wide-to-tall, so asking for a
// thumbnail this many times the band's width in height clears the band's width
// for anything in that range; a narrower cover is declined rather than blown up.
constexpr float NARROWEST_COVER_ASPECT = 0.6f;

// Where a cover's identifying mark sits, as a fraction of its height -- what the
// crop aims to put in the middle of the visible artwork. A book prints its title
// a little below the top edge; a magazine's masthead runs right along the top,
// so it wants 0 and the crop simply starts at the first row.
constexpr float BOOK_TITLE_BAND = 0.25f;
constexpr float MAGAZINE_MASTHEAD_BAND = 0.0f;

struct Crop {
  // False when the cover is smaller than the band on either axis; offsets are then 0.
  bool fits;
  int xOffset;
  int yOffset;
};

// Vertical crop is aimed rather than anchored. A book puts its title in the
// upper part of the cover, so centring the whole cover buries the title above
// the crop and anchoring at the top strands it down against the plate;
// centring the focus band on the artwork that is actually visible -- the band
// less the plate covering its foot -- keeps it where the eye lands.
constexpr Crop crop(const int coverWidth, const int coverHeight, const int bandWidth, const int bandHeight,
                    const int visibleHeight, const float focusBand) {
  if (coverWidth < bandWidth || coverHeight < bandHeight) return Crop{false, 0, 0};
  const int xOffset = (coverWidth - bandWidth) / 2;
  const int focusRow = static_cast<int>(static_cast<float>(coverHeight) * focusBand);
  const int yOffset = std::clamp(focusRow - visibleHeight / 2, 0, coverHeight - bandHeight);
  return Crop{true, xOffset, yOffset};
}

// The band's WIDTH is what usually binds: covers are portrait, so a thumbnail
// tall enough to fill the height is still far too narrow. The result names the
// cached thumb_<h>.bmp, so its float truncation is part of the cache key.
constexpr int thumbHeightFor(const int bandWidth, const int bandHeight) {
  return std::max(bandHeight, static_cast<int>(static_cast<float>(bandWidth) / NARROWEST_COVER_ASPECT));
}

constexpr int plateTop(const int bandY, const int bandHeight, const int plateHeight) {
  return bandY + bandHeight - plateHeight;
}

}  // namespace CoverBandGeometry
