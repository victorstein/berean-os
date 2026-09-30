#pragma once

#include <string>
#include <vector>

#include "components/themes/BaseTheme.h"

class GfxRenderer;

// A screen's own cover as its header: a CoverBand across the top with the
// theme header, title and battery, on an opaque plate along its foot.
namespace Masthead {

// Where content starts below the band.
int contentTop(const GfxRenderer& renderer);

// The cached thumbnail height CoverBand needs for the band.
int thumbHeight(const GfxRenderer& renderer);

// Whether the thumbnail at coverPath exists and is large enough to fill the
// band without upscaling.
bool fits(const GfxRenderer& renderer, const std::string& coverPath);

// Whether bookPath's band-sized thumbnail is already cached, i.e. whether
// pickCover can reach it without opening the EPUB.
bool isCached(const GfxRenderer& renderer, const std::string& bookPath);

// The first book, in order, whose cover fills the band; "" when none does.
// Generates at most one missing thumbnail per call, since generating opens the
// EPUB. Later candidates are taken only if their thumbnail is already cached.
std::string pickCover(const GfxRenderer& renderer, const std::vector<std::string>& bookPaths);

// Draws the band and its header plate. When the cover is absent or cannot be
// drawn, draws the compact header in its usual place instead and returns false.
bool draw(const GfxRenderer& renderer, const std::string& coverPath, float focusBand, const char* title);

}  // namespace Masthead
