#include "CoverThumb.h"

#include <Bitmap.h>
#include <Epub.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <Logging.h>
#include <SdPaths.h>

namespace {

constexpr const char* MODULE = "COVER";

}  // namespace

namespace CoverThumb {

// The thumbnail is requested at exactly the height it will be drawn at, and is
// never resampled afterwards. generateThumbBmp emits a DITHERED 1-bit image,
// and drawBitmap1Bit rescales by point-sampling -- picking every Nth pixel out
// of a pattern whose whole meaning is the local density of its pixels, which
// turns a cover into uniform static. Matching the sizes is the only way to
// render one honestly on a 1-bit panel.
//
// Keyed on the book path rather than a recents entry: Epub derives its cache
// path from the path alone, so this works for a publication that has been
// downloaded but never opened.
std::string pathFor(const std::string& bookPath, const int height, bool& generatedAny) {
  if (bookPath.empty() || height <= 0 || !FsHelpers::hasEpubExtension(bookPath)) return {};

  Epub epub(bookPath, sdpaths::CROSSPOINT_DIR);
  const std::string path = epub.getThumbBmpPath(height);
  if (Storage.exists(path.c_str())) return path;

  generatedAny = true;
  // buildIfMissing, not the cached-only load the old home screen could rely on:
  // that one only ever saw books that had been opened, and this one has to cope
  // with a publication downloaded and never read, whose metadata cache does not
  // exist yet. Without it generateThumbBmp fails with "cache not loaded".
  epub.load(true, true);
  if (!epub.generateThumbBmp(height)) {
    LOG_DBG(MODULE, "No cover thumbnail for %s", bookPath.c_str());
    return {};
  }
  return Storage.exists(path.c_str()) ? path : std::string{};
}

bool sizeOf(const std::string& coverPath, int& width, int& height) {
  if (coverPath.empty()) return false;
  HalFile file;
  if (!Storage.openFileForRead(MODULE, coverPath, file)) return false;

  Bitmap bitmap(file);
  if (bitmap.parseHeaders() != BmpReaderError::Ok) return false;
  width = bitmap.getWidth();
  height = bitmap.getHeight();
  return width > 0 && height > 0;
}

int drawNative(const GfxRenderer& renderer, const std::string& coverPath, const int x, const int y, const int boxWidth,
               const int boxHeight, const int cornerRadius) {
  if (coverPath.empty()) return 0;
  HalFile file;
  if (!Storage.openFileForRead(MODULE, coverPath, file)) return 0;

  Bitmap bitmap(file);
  if (bitmap.parseHeaders() != BmpReaderError::Ok) return 0;
  const int width = bitmap.getWidth();
  const int height = bitmap.getHeight();
  // Refuse rather than rescale: see pathFor. A cover that does not fit is a
  // layout change that outran its cached thumbnail, and the caller's fallback is
  // the honest one until the new size is generated.
  if (width <= 0 || height <= 0 || width > boxWidth || height > boxHeight) return 0;

  const int drawX = x + (boxWidth - width) / 2;
  const int drawY = y + (boxHeight - height) / 2;
  renderer.drawBitmap(bitmap, drawX, drawY, width, height);
  renderer.drawRoundedRect(drawX, drawY, width, height, 1, cornerRadius, true);
  return width;
}

}  // namespace CoverThumb
