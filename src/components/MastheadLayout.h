#pragma once

#include <algorithm>
#include <cstddef>
#include <string>
#include <vector>

// Where the cover masthead sits, what it leaves the content below it, and
// which books it tries for a cover -- free of the renderer so it can be
// host-tested (test/masthead/MastheadLayoutTest.cpp).
namespace MastheadLayout {

struct Box {
  int x = 0;
  int y = 0;
  int width = 0;
  int height = 0;
};

// Home's hero is placed with this same band (HomeLayout.h), so both ask
// CoverBand for one cached thumbnail.
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

// Books to try for a library's masthead: the listed books that were opened,
// most recent first (recentPaths is newest first), then the rest in list order.
// No duplicates, and at most `cap`, since each costs a BMP header read.
inline std::vector<std::string> coverCandidates(const std::vector<std::string>& recentPaths,
                                                const std::vector<std::string>& listedPaths, const size_t cap) {
  const auto contains = [](const std::vector<std::string>& paths, const std::string& path) {
    return std::find(paths.begin(), paths.end(), path) != paths.end();
  };
  std::vector<std::string> candidates;
  candidates.reserve(std::min(cap, listedPaths.size()));
  for (const std::string& path : recentPaths) {
    if (candidates.size() >= cap) return candidates;
    if (contains(listedPaths, path) && !contains(candidates, path)) candidates.push_back(path);
  }
  for (const std::string& path : listedPaths) {
    if (candidates.size() >= cap) return candidates;
    if (!contains(candidates, path)) candidates.push_back(path);
  }
  return candidates;
}

}  // namespace MastheadLayout
