#include "Masthead.h"

#include <Arduino.h>
#include <Epub.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <Logging.h>
#include <SdPaths.h>

#include "components/CoverBand.h"
#include "components/CoverBandGeometry.h"
#include "components/MastheadLayout.h"
#include "components/UITheme.h"
#include "util/CoverThumb.h"

namespace {

constexpr const char* MODULE = "MASTHEAD";

MastheadLayout::Box bandBox(const GfxRenderer& renderer) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  int marginTop = 0;
  int marginRight = 0;
  int marginBottom = 0;
  int marginLeft = 0;
  renderer.getOrientedViewableTRBL(&marginTop, &marginRight, &marginBottom, &marginLeft);
  return MastheadLayout::band(renderer.getScreenWidth(), marginTop, marginRight, marginLeft, metrics.topPadding,
                              metrics.mastheadHeight);
}

Rect toRect(const MastheadLayout::Box& box) { return Rect{box.x, box.y, box.width, box.height}; }

// The thumbnail's path if it is already cached. Never opens the EPUB.
std::string cachedThumb(const std::string& bookPath, const int height) {
  if (bookPath.empty()) return {};
  const std::string path = Epub(bookPath, sdpaths::CROSSPOINT_DIR).getThumbBmpPath(height);
  return Storage.exists(path.c_str()) ? path : std::string{};
}

}  // namespace

namespace Masthead {

int contentTop(const GfxRenderer& renderer) { return MastheadLayout::contentTop(bandBox(renderer)); }

int thumbHeight(const GfxRenderer& renderer) {
  const MastheadLayout::Box band = bandBox(renderer);
  return CoverBandGeometry::thumbHeightFor(band.width, band.height);
}

bool fits(const GfxRenderer& renderer, const std::string& coverPath) {
  // Checked first because a failed open logs an error, and a missing
  // thumbnail is an ordinary case here.
  if (coverPath.empty() || !Storage.exists(coverPath.c_str())) return false;
  int width = 0;
  int height = 0;
  if (!CoverThumb::sizeOf(coverPath, width, height)) return false;
  const MastheadLayout::Box band = bandBox(renderer);
  return CoverBandGeometry::crop(width, height, band.width, band.height, band.height, 0.0f).fits;
}

bool isCached(const GfxRenderer& renderer, const std::string& bookPath) {
  return !cachedThumb(bookPath, thumbHeight(renderer)).empty();
}

std::string pickCover(const GfxRenderer& renderer, const std::vector<std::string>& bookPaths) {
  const int height = thumbHeight(renderer);
  bool generated = false;
  for (const std::string& bookPath : bookPaths) {
    const std::string thumb =
        generated ? cachedThumb(bookPath, height) : CoverThumb::pathFor(bookPath, height, generated);
    if (fits(renderer, thumb)) return thumb;
    if (!thumb.empty()) LOG_DBG(MODULE, "Cover does not fill the band: %s", thumb.c_str());
  }
  return {};
}

bool draw(const GfxRenderer& renderer, const std::string& coverPath, const float focusBand, const char* title) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const MastheadLayout::Box band = bandBox(renderer);
  const CoverBand::Style style{focusBand, /*cornerRadius=*/0, metrics.headerHeight + 1};

  const unsigned long startMs = millis();
  const bool drawn = CoverBand::draw(renderer, coverPath, toRect(band), style);
  LOG_DBG(MODULE, "Band %s in %lu ms", drawn ? "drawn" : "declined", millis() - startMs);

  if (!drawn) {
    // CoverBand has left the band as paper, so the header never sits on dither.
    GUI.drawHeader(renderer, Rect{0, metrics.topPadding, renderer.getScreenWidth(), metrics.headerHeight}, title);
    return false;
  }
  GUI.drawHeader(renderer, toRect(MastheadLayout::plateHeader(MastheadLayout::plate(band, metrics.headerHeight))),
                 title);
  return true;
}

}  // namespace Masthead
