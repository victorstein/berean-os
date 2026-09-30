#include "CoverBand.h"

#include <Bitmap.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>

#include "util/CoverThumb.h"

namespace {

constexpr const char* MODULE = "COVER_BAND";

// GfxRenderer has no clip region and drawBitmap only ever scales DOWN, so the
// row walk is done here, and walking only the band's rows and columns is what
// keeps the cover from bleeding into its neighbours. Rows are streamed through
// two row-sized buffers; nothing band-sized is held.
bool blitCropped(const GfxRenderer& renderer, const std::string& coverPath, const Rect& band, const int visibleHeight,
                 const float focusBand) {
  if (coverPath.empty()) return false;
  HalFile file;
  if (!Storage.openFileForRead(MODULE, coverPath, file)) return false;

  Bitmap bitmap(file);
  if (bitmap.parseHeaders() != BmpReaderError::Ok) return false;
  const int width = bitmap.getWidth();
  const int height = bitmap.getHeight();
  const auto crop = CoverBandGeometry::crop(width, height, band.width, band.height, visibleHeight, focusBand);
  if (!crop.fits) return false;

  auto packedRow = makeUniqueNoThrow<uint8_t[]>((width + 3) / 4);
  auto rowScratch = makeUniqueNoThrow<uint8_t[]>(bitmap.getRowBytes());
  if (!packedRow || !rowScratch) {
    LOG_ERR(MODULE, "OOM: cover row buffers");
    return false;
  }

  for (int row = 0; row < height; ++row) {
    if (bitmap.readNextRow(packedRow.get(), rowScratch.get()) != BmpReaderError::Ok) {
      LOG_ERR(MODULE, "Cover read failed at row %d: %s", row, coverPath.c_str());
      renderer.fillRect(band.x, band.y, band.width, band.height, false);
      return false;
    }
    // Rows arrive in file order; a bottom-up BMP delivers the cover's last row
    // first, so the source row has to be resolved before it can be discarded.
    const int sourceRow = bitmap.isTopDown() ? row : height - 1 - row;
    if (sourceRow < crop.yOffset || sourceRow >= crop.yOffset + band.height) continue;

    const int screenY = band.y + sourceRow - crop.yOffset;
    for (int column = 0; column < band.width; ++column) {
      const int sourceColumn = column + crop.xOffset;
      const uint8_t value = packedRow[sourceColumn / 4] >> (6 - ((sourceColumn * 2) % 8)) & 0x3;
      if (value < 3) renderer.drawPixel(band.x + column, screenY, true);
    }
  }
  return true;
}

}  // namespace

namespace CoverBand {

bool draw(const GfxRenderer& renderer, const std::string& coverPath, const Rect& band, const Style& style) {
  if (!blitCropped(renderer, coverPath, band, band.height - style.plateHeight, style.focusBand)) return false;

  // The cover was blitted as a rectangle, so its corners sit outside the rounded
  // border the caller is about to draw. Masking them back to paper first is what
  // stops the art poking out past the arc.
  if (style.cornerRadius > 0) {
    renderer.maskRoundedRectOutsideCorners(band.x, band.y, band.width, band.height, style.cornerRadius);
  }

  // Opaque, because a dithered cover underneath would shred the label's glyphs
  // on a 1-bit panel.
  if (style.plateHeight > 0) {
    const Rect plate = plateRect(band, style);
    renderer.fillRoundedRect(plate.x, plate.y, plate.width, plate.height, style.cornerRadius, /*roundTopLeft=*/false,
                             /*roundTopRight=*/false, /*roundBottomLeft=*/true, /*roundBottomRight=*/true,
                             Color::White);
    renderer.drawLine(plate.x, plate.y, plate.x + plate.width - 1, plate.y, true);
  }
  return true;
}

Rect plateRect(const Rect& band, const Style& style) {
  return Rect{band.x, CoverBandGeometry::plateTop(band.y, band.height, style.plateHeight), band.width,
              style.plateHeight};
}

std::string thumbPathFor(const std::string& bookPath, const int bandWidth, const int bandHeight, bool& generatedAny) {
  return CoverThumb::pathFor(bookPath, CoverBandGeometry::thumbHeightFor(bandWidth, bandHeight), generatedAny);
}

}  // namespace CoverBand
