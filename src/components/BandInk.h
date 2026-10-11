#pragma once

#include <cstddef>
#include <cstdint>

// A cover band's ink as a bitset, so a repaint can replay it without streaming
// the cover from SD. One run of `stride` bytes per band column, MSB first; a set
// bit is a pixel the blit inked. In Portrait a band column is one physical
// framebuffer row and band row r is physical column y + r, so each run starts
// `lead = y & 7` bits into its first byte: the run then lies byte for byte over
// the framebuffer. Pure, for test/cover_band.
namespace BandInk {

struct Layout {
  int width = 0;
  int height = 0;
  int lead = 0;
  int stride = 0;
};

constexpr Layout layoutFor(const int y, const int width, const int height) {
  if (width <= 0 || height <= 0) return Layout{};
  const int lead = y & 7;
  return Layout{width, height, lead, (lead + height + 7) / 8};
}

constexpr size_t bytesFor(const Layout& layout) {
  return static_cast<size_t>(layout.width) * static_cast<size_t>(layout.stride);
}

constexpr size_t byteIndex(const Layout& layout, const int column, const int row) {
  return static_cast<size_t>(column) * static_cast<size_t>(layout.stride) +
         static_cast<size_t>((layout.lead + row) >> 3);
}

constexpr uint8_t bitMask(const Layout& layout, const int row) {
  return static_cast<uint8_t>(0x80u >> ((layout.lead + row) & 7));
}

inline void set(uint8_t* bits, const Layout& layout, const int column, const int row) {
  bits[byteIndex(layout, column, row)] |= bitMask(layout, row);
}

inline bool test(const uint8_t* bits, const Layout& layout, const int column, const int row) {
  return (bits[byteIndex(layout, column, row)] & bitMask(layout, row)) != 0;
}

}  // namespace BandInk
