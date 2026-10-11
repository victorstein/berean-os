#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

#include "components/BandInk.h"

// A replayed band has to be the cover the blit drew, and in Portrait each
// column's run has to sit over exactly the framebuffer bits drawPixel would
// touch: a bit landing one place off shears the artwork on every repaint.

namespace {

// GfxRenderer::drawPixel in Portrait: physical column phyX = y, bit 7 - phyX % 8.
size_t framebufferByte(const int phyX) { return static_cast<size_t>(phyX) / 8; }
uint8_t framebufferBit(const int phyX) { return static_cast<uint8_t>(1u << (7 - phyX % 8)); }

}  // namespace

TEST(BandInk, SizesTheMastheadAndHomeBands) {
  EXPECT_EQ(BandInk::bytesFor(BandInk::layoutFor(14, 464, 120)), 7424u);
  EXPECT_EQ(BandInk::bytesFor(BandInk::layoutFor(22, 464, 272)), 16240u);
  EXPECT_EQ(BandInk::bytesFor(BandInk::layoutFor(16, 464, 120)), 6960u);
  EXPECT_EQ(BandInk::bytesFor(BandInk::layoutFor(23, 464, 120)), 7424u);
}

TEST(BandInk, AnEmptyBandHoldsNothing) {
  EXPECT_EQ(BandInk::bytesFor(BandInk::layoutFor(14, 0, 120)), 0u);
  EXPECT_EQ(BandInk::bytesFor(BandInk::layoutFor(14, 464, 0)), 0u);
  EXPECT_EQ(BandInk::bytesFor(BandInk::layoutFor(14, -1, 120)), 0u);
  EXPECT_EQ(BandInk::bytesFor(BandInk::layoutFor(14, 464, -1)), 0u);
}

TEST(BandInk, ASetPixelReadsBackAndItsNeighboursDoNot) {
  for (int lead = 0; lead < 8; ++lead) {
    const BandInk::Layout layout = BandInk::layoutFor(40 + lead, 10, 20);
    std::vector<uint8_t> bits(BandInk::bytesFor(layout), 0);
    BandInk::set(bits.data(), layout, 3, 9);
    EXPECT_TRUE(BandInk::test(bits.data(), layout, 3, 9)) << "lead " << lead;
    EXPECT_FALSE(BandInk::test(bits.data(), layout, 2, 9)) << "lead " << lead;
    EXPECT_FALSE(BandInk::test(bits.data(), layout, 4, 9)) << "lead " << lead;
    EXPECT_FALSE(BandInk::test(bits.data(), layout, 3, 8)) << "lead " << lead;
    EXPECT_FALSE(BandInk::test(bits.data(), layout, 3, 10)) << "lead " << lead;
  }
}

TEST(BandInk, TheCornersOfABandAreAddressable) {
  for (int lead = 0; lead < 8; ++lead) {
    const BandInk::Layout layout = BandInk::layoutFor(lead, 464, 120);
    std::vector<uint8_t> bits(BandInk::bytesFor(layout), 0);
    BandInk::set(bits.data(), layout, 0, 0);
    BandInk::set(bits.data(), layout, 463, 119);
    EXPECT_TRUE(BandInk::test(bits.data(), layout, 0, 0)) << "lead " << lead;
    EXPECT_TRUE(BandInk::test(bits.data(), layout, 463, 119)) << "lead " << lead;
  }
}

TEST(BandInk, BitsOutsideTheBandRowsStayClear) {
  for (int lead = 0; lead < 8; ++lead) {
    const BandInk::Layout layout = BandInk::layoutFor(lead, 3, 13);
    std::vector<uint8_t> bits(BandInk::bytesFor(layout), 0);
    for (int column = 0; column < layout.width; ++column) {
      for (int row = 0; row < layout.height; ++row) BandInk::set(bits.data(), layout, column, row);
    }
    for (int column = 0; column < layout.width; ++column) {
      const uint8_t* run = bits.data() + static_cast<size_t>(column) * layout.stride;
      for (int bit = 0; bit < layout.stride * 8; ++bit) {
        const bool inBand = bit >= lead && bit < lead + layout.height;
        const bool set = (run[bit / 8] & (0x80u >> (bit % 8))) != 0;
        EXPECT_EQ(set, inBand) << "lead " << lead << " column " << column << " bit " << bit;
      }
    }
  }
}

TEST(BandInk, EachRowSitsOverTheFramebufferBitDrawPixelWrites) {
  for (int lead = 0; lead < 8; ++lead) {
    const int y = 40 + lead;
    const BandInk::Layout layout = BandInk::layoutFor(y, 1, 120);
    for (int row = 0; row < layout.height; ++row) {
      EXPECT_EQ(BandInk::byteIndex(layout, 0, row), framebufferByte(y + row) - framebufferByte(y))
          << "lead " << lead << " row " << row;
      EXPECT_EQ(BandInk::bitMask(layout, row), framebufferBit(y + row)) << "lead " << lead << " row " << row;
    }
  }
}
