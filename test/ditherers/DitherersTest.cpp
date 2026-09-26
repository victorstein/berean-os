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

namespace {

template <typename Ditherer>
void expectEachRowFailureReported(const std::size_t rowCount) {
  for (std::size_t n = 1; n <= rowCount; n++) {
    bool fired = false;
    bool valid = true;
    {
      failing_new::ScopedNthNothrowFailure failure(n);
      const Ditherer ditherer(kWidth);
      fired = failure.fired();
      valid = ditherer.valid();
    }
    EXPECT_TRUE(fired) << "row " << n;
    EXPECT_FALSE(valid) << "row " << n;
  }

  bool fired = true;
  bool valid = false;
  {
    failing_new::ScopedNthNothrowFailure failure(rowCount + 1);
    const Ditherer ditherer(kWidth);
    fired = failure.fired();
    valid = ditherer.valid();
  }
  EXPECT_FALSE(fired) << "the ditherer made more than " << rowCount << " nothrow allocations";
  EXPECT_TRUE(valid);
}

}  // namespace

TEST(DitherersValid, AllocatedAtRealisticWidths) {
  for (const int width : {1, 480, 2048}) {
    EXPECT_TRUE(Atkinson1BitDitherer(width).valid()) << width;
    EXPECT_TRUE(AtkinsonDitherer(width).valid()) << width;
    EXPECT_TRUE(FloydSteinbergDitherer(width).valid()) << width;
  }
}

TEST(DitherersOom, Atkinson1BitReportsEachFailedRow) { expectEachRowFailureReported<Atkinson1BitDitherer>(3); }

TEST(DitherersOom, AtkinsonReportsEachFailedRow) { expectEachRowFailureReported<AtkinsonDitherer>(3); }

TEST(DitherersOom, FloydSteinbergReportsEachFailedRow) { expectEachRowFailureReported<FloydSteinbergDitherer>(2); }
