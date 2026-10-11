#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <HalStorage.h>
#include <esp_heap_caps.h>
#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <vector>

#include "GfxRendererFake.h"
#include "HalStorageFake.h"
#include "components/BandInk.h"
#include "components/CoverBand.h"
#include "components/CoverBandCache.h"

// A replayed band must leave the framebuffer exactly as a fresh draw of the same
// band does. Every comparison starts from the same seeded, non-blank frame: on a
// white frame a replay that stores whole bytes, or restores paper, looks
// identical to one that only adds ink.

namespace {

constexpr int COVER_WIDTH = 480;
constexpr int COVER_HEIGHT = 773;
const std::string COVER = "/.crosspoint/epub_1/thumb_773.bmp";
const std::string OTHER_COVER = "/.crosspoint/epub_2/thumb_773.bmp";

const CoverBand::Style MASTHEAD{CoverBandGeometry::BOOK_TITLE_BAND, 0, 46};
const CoverBand::Style HOME{CoverBandGeometry::BOOK_TITLE_BAND, 8, 90};
const CoverBand::Style MEETINGS{CoverBandGeometry::MAGAZINE_MASTHEAD_BAND, 0, 46};

uint32_t nextRandom(uint32_t& state) {
  state = state * 1664525u + 1013904223u;
  return state >> 8;
}

// Solid black at the top, a checkerboard in the middle, noise below: long runs,
// alternating bits and arbitrary ones all reach the band.
bool coverInk(const int x, const int y, const uint32_t seed) {
  if (y < 150) return true;
  if (y < 260) return ((x + y) & 1) == 0;
  uint32_t state = seed ^ (static_cast<uint32_t>(y) * 2654435761u) ^ static_cast<uint32_t>(x) * 40503u;
  return (nextRandom(state) & 1) != 0;
}

void put16(std::string& out, const size_t at, const uint32_t value) {
  out[at] = static_cast<char>(value & 0xFF);
  out[at + 1] = static_cast<char>((value >> 8) & 0xFF);
}

void put32(std::string& out, const size_t at, const uint32_t value) {
  put16(out, at, value & 0xFFFF);
  put16(out, at + 2, value >> 16);
}

// A 1-bit BMP laid out as the thumbnail converters write one
// (JpegToBmpConverter.cpp:86-112): palette 0 black, 1 white.
std::string makeCover(const uint32_t seed, const bool topDown, const int height = COVER_HEIGHT) {
  const int rowBytes = (COVER_WIDTH + 31) / 32 * 4;
  std::string bmp(62 + static_cast<size_t>(rowBytes) * height, '\0');
  bmp[0] = 'B';
  bmp[1] = 'M';
  put32(bmp, 2, static_cast<uint32_t>(bmp.size()));
  put32(bmp, 10, 62);
  put32(bmp, 14, 40);
  put32(bmp, 18, COVER_WIDTH);
  put32(bmp, 22, static_cast<uint32_t>(topDown ? -height : height));
  put16(bmp, 26, 1);
  put16(bmp, 28, 1);
  put32(bmp, 34, static_cast<uint32_t>(rowBytes * height));
  put32(bmp, 46, 2);
  put32(bmp, 50, 2);
  bmp[58] = static_cast<char>(0xFF);
  bmp[59] = static_cast<char>(0xFF);
  bmp[60] = static_cast<char>(0xFF);
  for (int y = 0; y < height; ++y) {
    const int fileRow = topDown ? y : height - 1 - y;
    char* row = bmp.data() + 62 + static_cast<size_t>(fileRow) * rowBytes;
    for (int x = 0; x < COVER_WIDTH; ++x) {
      if (!coverInk(x, y, seed)) row[x / 8] = static_cast<char>(row[x / 8] | (0x80 >> (x % 8)));
    }
  }
  return bmp;
}

void seedFrame(std::vector<uint8_t>& bytes, uint32_t seed) {
  for (uint8_t& byte : bytes) byte = static_cast<uint8_t>(nextRandom(seed));
}

// Whether the frame holds ink at logical (x, y) in Portrait: GfxRenderer::drawPixel's
// mapping, phyX = y and phyY = panelHeight - 1 - x, black as a clear bit.
bool inkAt(const int x, const int y) {
  const int phyX = y;
  const int phyY = gfx_fake::PANEL_HEIGHT - 1 - x;
  const uint8_t byte = gfx_fake::frame()[static_cast<size_t>(phyY) * gfx_fake::PANEL_WIDTH_BYTES + phyX / 8];
  return (byte & (1 << (7 - phyX % 8))) == 0;
}

struct Paint {
  CoverBand::Result result;
  std::vector<uint8_t> frame;
  std::vector<std::string> calls;
};

class CoverBandReplay : public ::testing::Test {
 protected:
  HalDisplay display;
  GfxRenderer renderer{display};

  void SetUp() override {
    storage_fake::reset();
    storage_fake::putFile(COVER, makeCover(1, /*topDown=*/true));
    storage_fake::putFile(OTHER_COVER, makeCover(2, /*topDown=*/true));
    CoverBandCache::getInstance().clear();
    heap_caps_stub::failAll = false;
    renderer.begin();
    renderer.setOrientation(GfxRenderer::Portrait);
    renderer.endStripTarget();
  }

  void TearDown() override {
    heap_caps_stub::failAll = false;
    renderer.endStripTarget();
  }

  Paint paint(const std::string& path, const Rect& band, const CoverBand::Style& style, const uint32_t seed = 7) {
    seedFrame(gfx_fake::frame(), seed);
    gfx_fake::calls().clear();
    const CoverBand::Result result = CoverBand::draw(renderer, path, band, style);
    return Paint{result, gfx_fake::frame(), gfx_fake::calls()};
  }

  void expectReplayMatchesFreshDraw(const std::string& path, const Rect& band, const CoverBand::Style& style) {
    const Paint fresh = paint(path, band, style);
    const Paint replay = paint(path, band, style);
    EXPECT_EQ(fresh.result, CoverBand::Result::Drawn);
    EXPECT_EQ(replay.result, CoverBand::Result::Replayed);
    EXPECT_TRUE(fresh.frame == replay.frame) << "band y " << band.y;
    EXPECT_EQ(fresh.calls, replay.calls) << "band y " << band.y;
  }
};

TEST_F(CoverBandReplay, AReplayLeavesTheFrameAFreshDrawLeavesForEveryStyleAndAlignment) {
  for (const CoverBand::Style* style : {&MASTHEAD, &HOME, &MEETINGS}) {
    for (int lead = 0; lead < 8; ++lead) {
      CoverBandCache::getInstance().clear();
      const int height = style == &HOME ? 272 : 120;
      expectReplayMatchesFreshDraw(COVER, Rect{8, 16 + lead, 464, height}, *style);
    }
  }
}

TEST_F(CoverBandReplay, ABottomUpCoverReplaysAsItDrew) {
  storage_fake::putFile(COVER, makeCover(1, /*topDown=*/false));
  for (int lead = 0; lead < 8; ++lead) {
    CoverBandCache::getInstance().clear();
    expectReplayMatchesFreshDraw(COVER, Rect{8, 16 + lead, 464, 120}, MASTHEAD);
  }
}

TEST_F(CoverBandReplay, InkAlreadyAboveTheBandSurvivesTheReplay) {
  const Rect band{8, 19, 464, 120};
  ASSERT_NE(band.y & 7, 0);
  paint(COVER, band, MASTHEAD);
  std::fill(gfx_fake::frame().begin(), gfx_fake::frame().end(), 0xFF);
  renderer.drawPixel(band.x, band.y - 1, true);
  ASSERT_EQ(CoverBand::draw(renderer, COVER, band, MASTHEAD), CoverBand::Result::Replayed);
  EXPECT_TRUE(inkAt(band.x, band.y - 1));
}

TEST_F(CoverBandReplay, AReplayReadsNothingFromTheCard) {
  const Rect band{8, 14, 464, 120};
  paint(COVER, band, MASTHEAD);
  const size_t readsBefore = storage_fake::readCallsOn(COVER);
  EXPECT_EQ(paint(COVER, band, MASTHEAD).result, CoverBand::Result::Replayed);
  EXPECT_EQ(storage_fake::readCallsOn(COVER), readsBefore);
}

TEST_F(CoverBandReplay, AnotherOrientationReplaysThroughDrawPixel) {
  renderer.setOrientation(GfxRenderer::PortraitInverted);
  expectReplayMatchesFreshDraw(COVER, Rect{8, 14, 464, 120}, MASTHEAD);
}

TEST_F(CoverBandReplay, ABandReachingOffScreenReplaysThroughDrawPixel) {
  expectReplayMatchesFreshDraw(COVER, Rect{-4, 14, 464, 120}, MASTHEAD);
}

TEST_F(CoverBandReplay, ABandReachingPastTheBottomReplaysThroughDrawPixel) {
  expectReplayMatchesFreshDraw(COVER, Rect{8, 700, 464, 120}, MASTHEAD);
}

TEST_F(CoverBandReplay, AStripTargetClipsTheReplayAsItClipsTheDraw) {
  constexpr int STRIP_Y0 = 100;
  constexpr int STRIP_ROWS = 200;
  std::vector<uint8_t> strip(static_cast<size_t>(STRIP_ROWS) * gfx_fake::PANEL_WIDTH_BYTES);
  const Rect band{8, 14, 464, 120};

  seedFrame(strip, 11);
  renderer.beginStripTarget(strip.data(), STRIP_Y0, STRIP_ROWS);
  const Paint fresh = paint(COVER, band, MASTHEAD);
  const std::vector<uint8_t> freshStrip = strip;

  seedFrame(strip, 11);
  const Paint replay = paint(COVER, band, MASTHEAD);

  EXPECT_EQ(fresh.result, CoverBand::Result::Drawn);
  EXPECT_EQ(replay.result, CoverBand::Result::Replayed);
  EXPECT_TRUE(freshStrip == strip);
  EXPECT_TRUE(fresh.frame == replay.frame);
  std::vector<uint8_t> untouched(gfx_fake::FRAME_BYTES);
  seedFrame(untouched, 7);
  EXPECT_TRUE(replay.frame == untouched) << "a strip draw must not reach the framebuffer";
}

TEST_F(CoverBandReplay, EveryArgumentIsPartOfTheKey) {
  const Rect band{8, 14, 464, 120};
  struct Variant {
    std::string path;
    Rect band;
    CoverBand::Style style;
  };
  const Variant variants[] = {
      {OTHER_COVER, band, MASTHEAD},
      {COVER, Rect{9, 14, 464, 120}, MASTHEAD},
      {COVER, Rect{8, 15, 464, 120}, MASTHEAD},
      {COVER, Rect{8, 14, 463, 120}, MASTHEAD},
      {COVER, Rect{8, 14, 464, 121}, MASTHEAD},
      {COVER, band, CoverBand::Style{CoverBandGeometry::MAGAZINE_MASTHEAD_BAND, 0, 46}},
      {COVER, band, CoverBand::Style{CoverBandGeometry::BOOK_TITLE_BAND, 8, 46}},
      {COVER, band, CoverBand::Style{CoverBandGeometry::BOOK_TITLE_BAND, 0, 45}},
  };
  for (const Variant& variant : variants) {
    CoverBandCache::getInstance().clear();
    ASSERT_EQ(paint(COVER, band, MASTHEAD).result, CoverBand::Result::Drawn);
    EXPECT_EQ(paint(variant.path, variant.band, variant.style).result, CoverBand::Result::Drawn)
        << variant.path << " " << variant.band.x << "," << variant.band.y << " " << variant.band.width << "x"
        << variant.band.height << " focus " << variant.style.focusBand << " r" << variant.style.cornerRadius
        << " plate " << variant.style.plateHeight;
  }
}

TEST_F(CoverBandReplay, AChangedCoverIsDrawnFromTheCardAfterTheBump) {
  const Rect band{8, 14, 464, 120};
  paint(COVER, band, MASTHEAD);
  storage_fake::putFile(COVER, makeCover(3, /*topDown=*/true));
  CoverBand::noteCoversChanged();

  const Paint afterBump = paint(COVER, band, MASTHEAD);
  CoverBandCache::getInstance().clear();
  const Paint reference = paint(COVER, band, MASTHEAD);

  EXPECT_EQ(afterBump.result, CoverBand::Result::Drawn);
  EXPECT_TRUE(afterBump.frame == reference.frame);
}

TEST_F(CoverBandReplay, TheLeastRecentlyUsedBandIsEvictedAndThePsramHeldIsBounded) {
  const auto band = [](const int i) { return Rect{8 + i, 14, 464, 120}; };
  for (int i = 0; i < 5; ++i) ASSERT_EQ(paint(COVER, band(i), MASTHEAD).result, CoverBand::Result::Drawn);

  CoverBandCache& cache = CoverBandCache::getInstance();
  EXPECT_LE(cache.bytesHeld(), CoverBandCache::SLOTS * CoverBandCache::MAX_SLOT_BYTES);
  EXPECT_EQ(cache.bytesHeld(), CoverBandCache::SLOTS * BandInk::bytesFor(BandInk::layoutFor(14, 464, 120)));

  EXPECT_EQ(paint(COVER, band(1), MASTHEAD).result, CoverBand::Result::Replayed);
  EXPECT_EQ(paint(COVER, band(5), MASTHEAD).result, CoverBand::Result::Drawn);
  EXPECT_EQ(paint(COVER, band(1), MASTHEAD).result, CoverBand::Result::Replayed);
  EXPECT_EQ(paint(COVER, band(3), MASTHEAD).result, CoverBand::Result::Replayed);
  EXPECT_EQ(paint(COVER, band(4), MASTHEAD).result, CoverBand::Result::Replayed);
  EXPECT_EQ(paint(COVER, band(2), MASTHEAD).result, CoverBand::Result::Drawn);
}

TEST_F(CoverBandReplay, WithoutPsramTheBandIsStillDrawnEveryTime) {
  const Rect band{8, 14, 464, 120};
  const Paint normal = paint(COVER, band, MASTHEAD);
  CoverBandCache::getInstance().clear();
  heap_caps_stub::failAll = true;

  const Paint first = paint(COVER, band, MASTHEAD);
  const Paint second = paint(COVER, band, MASTHEAD);

  EXPECT_EQ(first.result, CoverBand::Result::Drawn);
  EXPECT_EQ(second.result, CoverBand::Result::Drawn);
  EXPECT_TRUE(first.frame == normal.frame);
  EXPECT_TRUE(second.frame == normal.frame);
  EXPECT_EQ(CoverBandCache::getInstance().bytesHeld(), 0u);
}

TEST_F(CoverBandReplay, ACoverThatFailsMidStreamIsNeverReplayed) {
  const std::string truncated = makeCover(1, /*topDown=*/true).substr(0, 62 + 60 * 300);
  storage_fake::putFile(COVER, truncated);
  const Rect band{8, 14, 464, 120};

  const Paint first = paint(COVER, band, MASTHEAD);
  EXPECT_EQ(first.result, CoverBand::Result::Declined);
  ASSERT_FALSE(first.calls.empty());
  EXPECT_EQ(first.calls.front(), "fillRect 8 14 464 120 0");
  EXPECT_EQ(paint(COVER, band, MASTHEAD).result, CoverBand::Result::Declined);
}

TEST_F(CoverBandReplay, AnEmptyPathIsDeclinedWithoutTouchingTheCache) {
  EXPECT_EQ(paint("", Rect{8, 14, 464, 120}, MASTHEAD).result, CoverBand::Result::Declined);
  EXPECT_EQ(CoverBandCache::getInstance().bytesHeld(), 0u);
}

TEST_F(CoverBandReplay, ABandOverTheCapIsDrawnButNotHeld) {
  const Rect band{8, 14, 464, 450};
  ASSERT_GT(BandInk::bytesFor(BandInk::layoutFor(band.y, band.width, band.height)), CoverBandCache::MAX_SLOT_BYTES);
  EXPECT_EQ(paint(COVER, band, MASTHEAD).result, CoverBand::Result::Drawn);
  EXPECT_EQ(paint(COVER, band, MASTHEAD).result, CoverBand::Result::Drawn);
  EXPECT_EQ(CoverBandCache::getInstance().bytesHeld(), 0u);
}

}  // namespace
