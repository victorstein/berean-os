#pragma once

// Where the cover masthead sits and what it leaves the content below it, free
// of the renderer so it can be host-tested (test/masthead/MastheadLayoutTest.cpp).
namespace MastheadLayout {

struct Box {
  int x = 0;
  int y = 0;
  int width = 0;
  int height = 0;
};

// The left edge and width of the launcher's Bible tile
// (LauncherActivity::computeLayout), so the band asks CoverBand for the
// thumbnail height the launcher has already cached.
constexpr Box band(const int screenWidth, const int marginTop, const int marginRight, const int marginLeft,
                   const int topPadding, const int height) {
  const int left = marginLeft + topPadding;
  const int right = screenWidth - marginRight - topPadding;
  return Box{left, marginTop + topPadding, right - left, height};
}

constexpr int contentTop(const Box& band) { return band.y + band.height; }

// The header's rows plus one above them for CoverBand's rule, which the
// header's own fill would otherwise paint over.
constexpr Box plate(const Box& band, const int headerHeight) {
  return Box{band.x, band.y + band.height - headerHeight - 1, band.width, headerHeight + 1};
}

constexpr Box plateHeader(const Box& plate) { return Box{plate.x, plate.y + 1, plate.width, plate.height - 1}; }

}  // namespace MastheadLayout
