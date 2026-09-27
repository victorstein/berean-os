#pragma once

#include <string>

class GfxRenderer;

// A book's cover as a 1-bit thumbnail, cached per height in its reading cache.
namespace CoverThumb {

// The cached thumbnail at exactly `height`, generating it when missing. Empty
// when the book has no usable cover. Sets generatedAny when it had to build one.
std::string pathFor(const std::string& bookPath, int height, bool& generatedAny);

// The thumbnail's stored size, read from its BMP header.
bool sizeOf(const std::string& coverPath, int& width, int& height);

// Draws the cover at its stored size centred in the box, or returns 0 without
// drawing. Never rescales: the thumbnails are dithered 1-bit and resampling
// destroys them.
int drawNative(const GfxRenderer& renderer, const std::string& coverPath, int x, int y, int boxWidth, int boxHeight,
               int cornerRadius);

}  // namespace CoverThumb
