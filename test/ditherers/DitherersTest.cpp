#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

#include "FailingNothrowNew.h"
#include "GfxRenderer/BitmapHelpers.h"

namespace {

constexpr int kWidth = 8;
constexpr int kRows = 4;

template <typename Ditherer>
std::vector<uint8_t> ditherGradient(Ditherer& ditherer) {
  std::vector<uint8_t> out;
  out.reserve(kWidth * kRows);
  for (int y = 0; y < kRows; y++) {
    for (int x = 0; x < kWidth; x++) {
      out.push_back(ditherer.processPixel((x * 255 / (kWidth - 1) + y * 37) % 256, x));
    }
    ditherer.nextRow();
  }
  return out;
}

// Captured from the raw-pointer implementation before the rows became nullable
// unique_ptrs; any drift here means the refactor changed the output.
const std::vector<uint8_t> kAtkinson1BitGolden = {0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 1, 0,
                                                  0, 1, 1, 1, 1, 0, 0, 0, 0, 1, 0, 1, 0, 0, 0, 0};
const std::vector<uint8_t> kAtkinsonGolden = {0, 1, 2, 2, 3, 3, 3, 3, 1, 2, 2, 3, 3, 3, 3, 1,
                                              2, 2, 2, 3, 3, 0, 1, 2, 2, 3, 3, 3, 0, 0, 2, 2};
const std::vector<uint8_t> kFloydSteinbergGolden = {0, 0, 2, 2, 3, 3, 3, 3, 1, 2, 2, 3, 3, 3, 3, 1,
                                                    2, 2, 3, 3, 3, 0, 1, 2, 2, 3, 3, 3, 0, 0, 2, 2};

template <typename Ditherer>
void expectGoldenAcrossReset(const std::vector<uint8_t>& golden) {
  Ditherer ditherer(kWidth);
  EXPECT_EQ(ditherGradient(ditherer), golden);
  ditherer.reset();
  EXPECT_EQ(ditherGradient(ditherer), golden);
}

}  // namespace

TEST(DitherersGolden, Atkinson1BitOutputIsUnchanged) {
  expectGoldenAcrossReset<Atkinson1BitDitherer>(kAtkinson1BitGolden);
}

TEST(DitherersGolden, AtkinsonOutputIsUnchanged) { expectGoldenAcrossReset<AtkinsonDitherer>(kAtkinsonGolden); }

TEST(DitherersGolden, FloydSteinbergOutputIsUnchanged) {
  expectGoldenAcrossReset<FloydSteinbergDitherer>(kFloydSteinbergGolden);
}
