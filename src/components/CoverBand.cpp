#include "CoverBand.h"

#include <Bitmap.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>

#include "components/BandInk.h"
#include "components/CoverBandCache.h"
#include "util/CoverThumb.h"

namespace {

constexpr const char* MODULE = "COVER_BAND";

// GfxRenderer has no clip region and drawBitmap only ever scales DOWN, so the
// row walk is done here, and walking only the band's rows and columns is what
// keeps the cover from bleeding into its neighbours. Rows are streamed through
// two row-sized buffers; nothing band-sized is held.
bool blitCropped(const GfxRenderer& renderer, const std::string& coverPath, const Rect& band, const int visibleHeight,
                 const float focusBand, const BandInk::Layout& layout, uint8_t* inkOut) {
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
      if (value < 3) {
        renderer.drawPixel(band.x + column, screenY, true);
        if (inkOut) BandInk::set(inkOut, layout, column, screenY - band.y);
      }
    }
  }
  return true;
}

void finishBand(const GfxRenderer& renderer, const Rect& band, const CoverBand::Style& style) {
  // The cover was blitted as a rectangle, so its corners sit outside the rounded
  // border the caller is about to draw. Masking them back to paper first is what
  // stops the art poking out past the arc.
  if (style.cornerRadius > 0) {
    renderer.maskRoundedRectOutsideCorners(band.x, band.y, band.width, band.height, style.cornerRadius);
  }

  // Opaque, because a dithered cover underneath would shred the label's glyphs
  // on a 1-bit panel.
  if (style.plateHeight > 0) {
    const Rect plate = CoverBand::plateRect(band, style);
    renderer.fillRoundedRect(plate.x, plate.y, plate.width, plate.height, style.cornerRadius, /*roundTopLeft=*/false,
                             /*roundTopRight=*/false, /*roundBottomLeft=*/true, /*roundBottomRight=*/true,
                             Color::White);
    renderer.drawLine(plate.x, plate.y, plate.x + plate.width - 1, plate.y, true);
  }
}

bool insideScreen(const GfxRenderer& renderer, const Rect& band) {
  return band.x >= 0 && band.y >= 0 && band.x + band.width <= renderer.getScreenWidth() &&
         band.y + band.height <= renderer.getScreenHeight();
}

// Writes exactly what drawPixel(x, y, true) would for every set bit. In Portrait
// a band column is one physical framebuffer row and BandInk lays each column's
// run over that row's bytes, so the replay is one AND per byte through the
// raw-writer target, clipped to an active strip the way drawPixel clips. Any
// other orientation, or a band reaching off screen, goes through drawPixel.
void replayInk(const GfxRenderer& renderer, const Rect& band, const BandInk::Layout& layout, const uint8_t* ink) {
  if (renderer.getOrientation() == GfxRenderer::Portrait && insideScreen(renderer, band)) {
    uint8_t* target = renderer.getWriteTarget();
    const int originY = renderer.getWriteOriginY();
    const int rows = renderer.getWriteRows();
    const size_t rowBytes = renderer.getDisplayWidthBytes();
    const int panelHeight = renderer.getDisplayHeight();
    for (int column = 0; column < band.width; ++column) {
      const int rowY = panelHeight - 1 - (band.x + column) - originY;
      if (static_cast<unsigned>(rowY) >= static_cast<unsigned>(rows)) continue;
      uint8_t* row = target + static_cast<size_t>(rowY) * rowBytes + static_cast<size_t>(band.y >> 3);
      const uint8_t* run = ink + static_cast<size_t>(column) * static_cast<size_t>(layout.stride);
      for (int i = 0; i < layout.stride; ++i) row[i] &= static_cast<uint8_t>(~run[i]);
    }
    return;
  }
  for (int column = 0; column < band.width; ++column) {
    for (int row = 0; row < band.height; ++row) {
      if (BandInk::test(ink, layout, column, row)) renderer.drawPixel(band.x + column, band.y + row, true);
    }
  }
}

}  // namespace

namespace CoverBand {

const char* resultName(const Result result) {
  switch (result) {
    case Result::Drawn:
      return "drawn";
    case Result::Replayed:
      return "replayed";
    case Result::Declined:
      break;
  }
  return "declined";
}

Result draw(const GfxRenderer& renderer, const std::string& coverPath, const Rect& band, const Style& style) {
  if (coverPath.empty()) return Result::Declined;
  CoverBandCache& cache = CoverBandCache::getInstance();
  const BandInk::Layout layout = BandInk::layoutFor(band.y, band.width, band.height);

  if (const uint8_t* ink = cache.find(coverPath, band, style)) {
    replayInk(renderer, band, layout, ink);
    finishBand(renderer, band, style);
    return Result::Replayed;
  }

  uint8_t* inkOut = cache.reserve(coverPath, band, style, BandInk::bytesFor(layout));
  if (!blitCropped(renderer, coverPath, band, band.height - style.plateHeight, style.focusBand, layout, inkOut)) {
    cache.abandon();
    return Result::Declined;
  }
  cache.commit();
  finishBand(renderer, band, style);
  return Result::Drawn;
}

void noteCoversChanged() { CoverBandCache::noteCoversChanged(); }

Rect plateRect(const Rect& band, const Style& style) {
  return Rect{band.x, CoverBandGeometry::plateTop(band.y, band.height, style.plateHeight), band.width,
              style.plateHeight};
}

std::string thumbPathFor(const std::string& bookPath, const int bandWidth, const int bandHeight, bool& generatedAny) {
  return CoverThumb::pathFor(bookPath, CoverBandGeometry::thumbHeightFor(bandWidth, bandHeight), generatedAny);
}

}  // namespace CoverBand
