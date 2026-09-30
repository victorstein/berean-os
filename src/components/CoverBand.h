#pragma once

#include <string>

#include "components/CoverBandGeometry.h"
#include "components/themes/BaseTheme.h"

class GfxRenderer;

// A publication's cover drawn into a rectangle at 1:1: cropped and never scaled,
// because the cached thumbnails are dithered 1-bit and any resampling turns them
// to static. Aimed at a focus band, with an optional opaque plate for a label.
namespace CoverBand {

struct Style {
  float focusBand = CoverBandGeometry::BOOK_TITLE_BAND;
  // 0: square corners, nothing masked.
  int cornerRadius = 0;
  // 0: no plate; the whole band is artwork.
  int plateHeight = 0;
};

// Draws the cover into band, masks the corners and lays the opaque plate across
// the band's foot. False, with the band showing nothing of the cover, when it is
// missing, unreadable, or smaller than the band: a read that fails mid-stream
// clears the band to white first. The caller draws the label and any border.
bool draw(const GfxRenderer& renderer, const std::string& coverPath, const Rect& band, const Style& style);

// Where the caller draws its label. Zero height when style has no plate.
Rect plateRect(const Rect& band, const Style& style);

// The cached thumbnail sized to fill this band, generated when missing.
std::string thumbPathFor(const std::string& bookPath, int bandWidth, int bandHeight, bool& generatedAny);

}  // namespace CoverBand
