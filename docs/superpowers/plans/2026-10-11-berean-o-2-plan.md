# R9: cover band cache — implementation plan (berean-o-2)

> **For agentic workers:** use superpowers:subagent-driven-development or superpowers:executing-plans
> to work through this task by task. Steps use checkbox (`- [ ]`) syntax for tracking.

| | |
|---|---|
| Goal | A repeat paint of a cover band replays its ink from PSRAM in a few ms, instead of streaming the thumbnail from SD (~225 ms). Home logs its band time. |
| Design | `docs/superpowers/specs/2026-10-11-berean-o-2-design.md` ("the spec"), cleared by `docs/superpowers/reviews/berean-o-2-spec-review-1.md` |
| Branch | `perf/r9-cover-band-cache`, in this worktree |

**Architecture**

- **Recording.** The first blit records every inked pixel into a column-major bitset (`BandInk`),
  laid out so that each column's run sits byte for byte over its framebuffer row in Portrait.
- **Holding.** A render-task singleton, `CoverBandCache`, holds up to four of these bitsets in
  explicit PSRAM, keyed by every argument of `CoverBand::draw` plus an atomic epoch.
- **Replay.** A later draw with the same arguments ANDs the bytes into the framebuffer through the
  renderer's raw-writer accessors. Any other orientation, or a band reaching off screen, falls back
  to `drawPixel` per bit.
- **Invalidation.** Five call sites bump the epoch after a thumbnail can have changed.

**Tech stack**

- C++20 on ESP32-S3: PlatformIO, Arduino-ESP32 3.3.7.
- Host tests: GoogleTest 1.17 through CMake 4.4.

**Proven before this plan was written.** Every code block below was built and run in this
worktree, then reverted so only the plan is committed. The results:

- `pio run -e x4pro`: SUCCESS, no warning in any touched file.
- `ctest --test-dir build/test`: 1,574/1,574 passed.
- `CoverBandReplayTest`: 14/14 passed. With the replay's `&=` changed to `=`, four of those tests
  fail, so the seeded frames do catch a whole-byte store.
- The whole-tree `./bin/clang-format-fix` changed nothing.

## Files

Every file this plan creates or edits:

FILES: src/components/BandInk.h, src/components/CoverBandCache.h, src/components/CoverBandCache.cpp, src/components/CoverBand.h, src/components/CoverBand.cpp, src/components/Masthead.cpp
FILES: src/activities/launcher/LauncherActivity.cpp, src/activities/settings/ClearCacheActivity.cpp, src/activities/reader/EpubReaderActivity.cpp
FILES: src/util/CoverThumb.cpp, src/util/BookCacheUtils.cpp, src/util/CardBooks.cpp, lib/GfxRenderer/GfxRenderer.cpp
FILES: test/cover_band/, test/stubs/HalStorageFake.cpp, test/storage_io/HalStorageFakeTest.cpp

| File | Status | Responsibility |
|---|---|---|
| `src/components/BandInk.h` | new | pure bitset layout (spec §6) |
| `src/components/CoverBandCache.{h,cpp}` | new | four PSRAM slots, LRU, epoch (spec §7) |
| `src/components/CoverBand.{h,cpp}` | edit | `Result`, recording, `replayInk`, `finishBand`, `noteCoversChanged` |
| `src/components/Masthead.cpp` | edit | `replayed` in the existing band log |
| `src/activities/launcher/LauncherActivity.cpp` | edit | Home's new band log |
| `src/util/CoverThumb.cpp`, `src/util/BookCacheUtils.cpp`, `src/util/CardBooks.cpp` | edit | epoch bump (spec A3) |
| `src/activities/settings/ClearCacheActivity.cpp`, `src/activities/reader/EpubReaderActivity.cpp` | edit | epoch bump (spec A3) |
| `lib/GfxRenderer/GfxRenderer.cpp` | edit | constraint comment only, at `drawPixel` |
| `test/cover_band/CMakeLists.txt` | edit | two new executables |
| `test/cover_band/BandInkTest.cpp`, `CoverBandReplayTest.cpp`, `GfxRendererFake.{h,cpp}`, `CoverThumbStub.cpp`, `esp_heap_caps.h` | new | host tests and fakes |
| `test/stubs/HalStorageFake.cpp` | edit | `HalFile::seekCur` |
| `test/storage_io/HalStorageFakeTest.cpp` | edit | a test for it |

`test/CMakeLists.txt` and `src/main.cpp` are **not** touched. `test/cover_band` is already
registered at `test/CMakeLists.txt:126`.

## Ground rules for every task

- Run everything from the worktree root
  `/Volumes/stein/.herdr/worktrees/berean-os/perf-r9-cover-band-cache`.
- `pio` is not on `PATH`. Call it as `/Volumes/stein/.platformio/penv/bin/pio`.
- **Never invoke `clang-format` directly.** Use `./bin/clang-format-fix`. It skips untracked files,
  so `git add` first.
- **Commits.** Conventional, imperative, lowercase. No co-author or generated-by trailer. End each
  message with this line:

  ```
  Claude-Session: https://claude.ai/code/session_01Tku853xEHzdrPyZ8XZm3yW
  ```

- **Comments.** Only a non-obvious why. Do not add comments that restate code (repo `CLAUDE.md`).
- **Before editing any file, re-read it.** A sibling task may have changed it since this plan was
  written. Every edit below gives an exact `old` → `new` snippet. If the `old` snippet is not found
  verbatim, stop and report rather than guess.

---

## Task 0: Prepare the worktree (no commit)

- [ ] **Step 0.1: Bootstrap and configure.**

  ```bash
  ./bin/bootstrap
  cmake -S test -B build/test
  ```

  Expected: either `clang-format 21 already present in .venv/bin` or
  `Linking clang-format from the primary checkout (…)...` followed by
  `Done: clang-format version 21.x`. CMake ends with
  `-- Build files have been written to: …/build/test`.

  On a fresh configure the first `cmake --build build/test -j` can fail several suites while they
  race to build gtest. Rerunning the same command succeeds.

---

## Task 1: `BandInk`, the bitset layout

- [ ] **Step 1.1: Write the failing test.** Create `test/cover_band/BandInkTest.cpp`:

```cpp
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
```

- [ ] **Step 1.2: Register it.** Append to the end of `test/cover_band/CMakeLists.txt`, after the
  existing `gtest_discover_tests(CoverBandGeometryTest)`:

```cmake

# BandInk's layout arithmetic, header-only like the geometry above.
add_executable(BandInkTest
  BandInkTest.cpp
)

target_include_directories(BandInkTest PRIVATE
  ${REPO_ROOT}/src
)

target_link_libraries(BandInkTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(BandInkTest)
```

- [ ] **Step 1.3: Run it and see it fail.**

  ```bash
  cmake -S test -B build/test > /dev/null && cmake --build build/test --target BandInkTest -j 8
  ```

  Expected: a compile error, `'components/BandInk.h' file not found`.

- [ ] **Step 1.4: Implement.** Create `src/components/BandInk.h`:

```cpp
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
```

- [ ] **Step 1.5: Run it and see it pass.**

  ```bash
  cmake --build build/test --target BandInkTest -j 8 && ./build/test/cover_band/BandInkTest
  ```

  Expected: `[  PASSED  ] 6 tests.`

- [ ] **Step 1.6: Commit.**

  ```bash
  git add src/components/BandInk.h test/cover_band/BandInkTest.cpp test/cover_band/CMakeLists.txt
  git commit -m "perf: add the BandInk layout for cover band replay" -m "Claude-Session: https://claude.ai/code/session_01Tku853xEHzdrPyZ8XZm3yW"
  ```

---

## Task 2: `HalFile::seekCur` in the storage fake

`Bitmap::parseHeaders` calls `seekCur` (`lib/GfxRenderer/Bitmap.cpp:88,110,115`). The fake declares
it (`test/stubs/HalStorage.h:82`) but never defines it, so the replay suite in Task 3 would not link.

- [ ] **Step 2.1: Write the failing test.** In `test/storage_io/HalStorageFakeTest.cpp`, replace:

```cpp
TEST_F(HalStorageFake, WrittenHandleBytesLandOnTheCard) {
```

with:

```cpp
TEST_F(HalStorageFake, SeekCurMovesRelativeToThePositionAndRefusesToLeaveTheFile) {
  storage_fake::putFile("/f", "abcdef");
  HalFile file;
  ASSERT_TRUE(Storage.openFileForRead("TEST", std::string("/f"), file));
  EXPECT_EQ(file.read(), 'a');
  EXPECT_TRUE(file.seekCur(2));
  EXPECT_EQ(file.read(), 'd');
  EXPECT_TRUE(file.seekCur(-2));
  EXPECT_EQ(file.read(), 'c');
  EXPECT_FALSE(file.seekCur(-4));
  EXPECT_FALSE(file.seekCur(4));
  EXPECT_EQ(file.position(), 3u);
}

TEST_F(HalStorageFake, WrittenHandleBytesLandOnTheCard) {
```

- [ ] **Step 2.2: Run it and see it fail.**

  ```bash
  cmake --build build/test --target StorageIoTest -j 8
  ```

  Expected: a link error, an undefined symbol `HalFile::seekCur(long long)`.

- [ ] **Step 2.3: Implement.** In `test/stubs/HalStorageFake.cpp`, replace:

```cpp
bool HalFile::seek(size_t pos) {
  if (!impl || pos > size()) return false;
  impl->pos = pos;
  return true;
}
```

with:

```cpp
bool HalFile::seek(size_t pos) {
  if (!impl || pos > size()) return false;
  impl->pos = pos;
  return true;
}

bool HalFile::seekCur(const int64_t offset) {
  if (!impl) return false;
  const int64_t target = static_cast<int64_t>(impl->pos) + offset;
  if (target < 0) return false;
  return seek(static_cast<size_t>(target));
}
```

- [ ] **Step 2.4: Run it and see it pass.**

  ```bash
  cmake --build build/test --target StorageIoTest -j 8 && ./build/test/storage_io/StorageIoTest
  ```

  Expected: every test passes, including `SeekCurMovesRelativeToThePositionAndRefusesToLeaveTheFile`.

- [ ] **Step 2.5: Commit.**

  ```bash
  git add test/stubs/HalStorageFake.cpp test/storage_io/HalStorageFakeTest.cpp
  git commit -m "test: give the storage fake seekCur" -m "Claude-Session: https://claude.ai/code/session_01Tku853xEHzdrPyZ8XZm3yW"
  ```

---

## Task 3: Hold and replay cover bands

One commit. `CoverBand::draw`'s return type changes, so its callers change with it.

- [ ] **Step 3.1: The drift guard at `drawPixel`.** Do this first: the fake in Step 3.2 cites
  `GfxRenderer.cpp` line numbers as they stand after it. In `lib/GfxRenderer/GfxRenderer.cpp`,
  replace:

```cpp
// IMPORTANT: This function is in critical rendering path and is called for every pixel. Please keep it as simple and
// efficient as possible.
void GfxRenderer::drawPixel(const int x, const int y, const bool state) const {
```

with:

```cpp
// IMPORTANT: This function is in critical rendering path and is called for every pixel. Please keep it as simple and
// efficient as possible.
// CoverBand.cpp's replayInk and test/cover_band/GfxRendererFake.cpp copy the Portrait mapping and strip clip below
// to write a band without this call; change all three together.
void GfxRenderer::drawPixel(const int x, const int y, const bool state) const {
```

  Then check that the line numbers the fake cites still hold:

  ```bash
  grep -n "^void GfxRenderer::drawPixel(\|^static inline void rotateCoordinates\|^void GfxRenderer::beginStripTarget\|^void GfxRenderer::endStripTarget" lib/GfxRenderer/GfxRenderer.cpp
  ```

  Expected: `drawPixel` at 561, `rotateCoordinates` at 283, `beginStripTarget` at 1687,
  `endStripTarget` at 1698. If a sibling moved them, update the three `GfxRenderer.cpp:` citations
  in Step 3.2's `GfxRendererFake.cpp` to the new spans (`drawPixel` runs to its closing brace,
  `rotateCoordinates` to its own, and the strip pair to `endStripTarget`'s). Also re-diff each
  copied body against the original.

- [ ] **Step 3.2: The test fakes.** Create four files.

`test/cover_band/esp_heap_caps.h`:

```cpp
#pragma once

// Host stand-in for ESP-IDF's <esp_heap_caps.h>, for CoverBandCache.cpp. PSRAM is
// plain heap here; heap_caps_stub::failAll makes every allocation fail, for the
// out-of-PSRAM case.

#include <cstddef>
#include <cstdint>
#include <cstdlib>

#define MALLOC_CAP_8BIT (1 << 2)
#define MALLOC_CAP_SPIRAM (1 << 10)

namespace heap_caps_stub {
inline bool failAll = false;
}  // namespace heap_caps_stub

inline void* heap_caps_malloc(const size_t size, uint32_t) { return heap_caps_stub::failAll ? nullptr : malloc(size); }

inline void heap_caps_free(void* p) { free(p); }
```

`test/cover_band/CoverThumbStub.cpp`:

```cpp
// CoverBand::thumbPathFor reaches CoverThumb::pathFor, which opens EPUBs; the
// replay suite never calls it, so the link only needs a body.

#include "util/CoverThumb.h"

namespace CoverThumb {

std::string pathFor(const std::string&, int, bool&) { return {}; }

}  // namespace CoverThumb
```

`test/cover_band/GfxRendererFake.h`:

```cpp
#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Test controls for GfxRendererFake.cpp. One device-sized framebuffer, which
// GfxRenderer::begin() points the renderer at, and the finishing calls the band
// made since the last reset.
namespace gfx_fake {

// 800 x 480 panel, 100 bytes a row: the X4 Pro's, not the 480 x 800 the host
// HalDisplay stub declares.
constexpr int PANEL_WIDTH = 800;
constexpr int PANEL_HEIGHT = 480;
constexpr int PANEL_WIDTH_BYTES = PANEL_WIDTH / 8;
constexpr size_t FRAME_BYTES = static_cast<size_t>(PANEL_WIDTH_BYTES) * PANEL_HEIGHT;

std::vector<uint8_t>& frame();

// fillRect, maskRoundedRectOutsideCorners, fillRoundedRect and drawLine, in
// order, with their arguments.
std::vector<std::string>& calls();

}  // namespace gfx_fake
```

`test/cover_band/GfxRendererFake.cpp`:

```cpp
// Link-time host substitute for GfxRenderer, after test/pagination/GfxRendererFake.cpp:
// the real GfxRenderer.h, these bodies. A new renderer call from CoverBand breaks
// this link rather than silently diverging.
//
// The framebuffer is real and device-sized. drawPixel and rotateCoordinates are
// copies of GfxRenderer.cpp:561-600 and :283-313 with the log call dropped, so a
// replay that writes the framebuffer directly is checked against the mapping
// drawPixel uses. Keep them in step with the originals (the comment above the
// real drawPixel says so). The band's finishing calls are recorded rather than
// drawn: a fresh draw and a replay make them identically, and the record shows it.

#include "GfxRendererFake.h"

#include <GfxRenderer.h>

#include <cstdio>

namespace gfx_fake {

std::vector<uint8_t>& frame() {
  static std::vector<uint8_t> bytes(FRAME_BYTES, 0xFF);
  return bytes;
}

std::vector<std::string>& calls() {
  static std::vector<std::string> log;
  return log;
}

}  // namespace gfx_fake

namespace {

void record(const char* format, const int a, const int b, const int c, const int d, const int e) {
  char line[96];
  snprintf(line, sizeof(line), format, a, b, c, d, e);
  gfx_fake::calls().emplace_back(line);
}

// Copy of GfxRenderer.cpp:283-313.
inline void rotateCoordinates(const GfxRenderer::Orientation orientation, const int x, const int y, int* phyX,
                              int* phyY, const uint16_t panelWidth, const uint16_t panelHeight) {
  switch (orientation) {
    case GfxRenderer::Portrait: {
      *phyX = y;
      *phyY = panelHeight - 1 - x;
      break;
    }
    case GfxRenderer::LandscapeClockwise: {
      *phyX = panelWidth - 1 - x;
      *phyY = panelHeight - 1 - y;
      break;
    }
    case GfxRenderer::PortraitInverted: {
      *phyX = panelWidth - 1 - y;
      *phyY = x;
      break;
    }
    case GfxRenderer::LandscapeCounterClockwise: {
      *phyX = x;
      *phyY = y;
      break;
    }
  }
}

}  // namespace

void GfxRenderer::begin() {
  frameBuffer = gfx_fake::frame().data();
  panelWidth = gfx_fake::PANEL_WIDTH;
  panelHeight = gfx_fake::PANEL_HEIGHT;
  panelWidthBytes = gfx_fake::PANEL_WIDTH_BYTES;
  frameBufferSize = gfx_fake::FRAME_BYTES;
}

// Called by the inline ~GfxRenderer().
void GfxRenderer::freeBwBufferChunks() {}

int GfxRenderer::getScreenWidth() const {
  return orientation == Portrait || orientation == PortraitInverted ? panelHeight : panelWidth;
}

int GfxRenderer::getScreenHeight() const {
  return orientation == Portrait || orientation == PortraitInverted ? panelWidth : panelHeight;
}

// Copy of GfxRenderer.cpp:1687-1703, without the debug assert.
void GfxRenderer::beginStripTarget(uint8_t* scratch, const int stripY0, const int stripRows) const {
  _stripBuf = scratch;
  _stripY0 = stripY0;
  _stripRows = stripRows;
  _stripActive = true;
}

void GfxRenderer::endStripTarget() const {
  _stripActive = false;
  _stripBuf = nullptr;
  _stripY0 = 0;
  _stripRows = 0;
}

// Copy of GfxRenderer.cpp:561-600, without the out-of-range log.
void GfxRenderer::drawPixel(const int x, const int y, const bool state) const {
  int phyX = 0;
  int phyY = 0;

  if (orientation == Portrait) {
    phyX = y;
    phyY = panelHeight - 1 - x;
  } else {
    rotateCoordinates(orientation, x, y, &phyX, &phyY, panelWidth, panelHeight);
  }

  if (phyX < 0 || phyX >= panelWidth || phyY < 0 || phyY >= panelHeight) {
    return;
  }

  uint8_t* target = frameBuffer;
  uint32_t rowY = static_cast<uint32_t>(phyY);
  if (_stripActive) {
    if (phyY < _stripY0 || phyY >= _stripY0 + _stripRows) {
      return;
    }
    target = _stripBuf;
    rowY = static_cast<uint32_t>(phyY - _stripY0);
  }

  const uint32_t byteIndex = rowY * panelWidthBytes + (phyX / 8);
  const uint8_t bitPosition = 7 - (phyX % 8);

  if (state) {
    target[byteIndex] &= ~(1 << bitPosition);
  } else {
    target[byteIndex] |= 1 << bitPosition;
  }
}

void GfxRenderer::fillRect(const int x, const int y, const int width, const int height, const bool state) const {
  record("fillRect %d %d %d %d %d", x, y, width, height, state ? 1 : 0);
}

void GfxRenderer::maskRoundedRectOutsideCorners(const int x, const int y, const int width, const int height,
                                                const int radius, const Color color) const {
  record("maskCorners %d %d %d %d r%d", x, y, width, height, radius);
  record("maskCorners color %d %d %d %d %d", static_cast<int>(color), 0, 0, 0, 0);
}

void GfxRenderer::fillRoundedRect(const int x, const int y, const int width, const int height, const int cornerRadius,
                                  const bool roundTopLeft, const bool roundTopRight, const bool roundBottomLeft,
                                  const bool roundBottomRight, const Color color) const {
  record("fillRoundedRect %d %d %d %d r%d", x, y, width, height, cornerRadius);
  record("fillRoundedRect corners %d%d%d%d color %d", roundTopLeft ? 1 : 0, roundTopRight ? 1 : 0,
         roundBottomLeft ? 1 : 0, roundBottomRight ? 1 : 0, static_cast<int>(color));
}

void GfxRenderer::drawLine(const int x1, const int y1, const int x2, const int y2, const bool state) const {
  record("drawLine %d %d %d %d %d", x1, y1, x2, y2, state ? 1 : 0);
}
```

- [ ] **Step 3.3: Write the failing replay suite.** Create `test/cover_band/CoverBandReplayTest.cpp`:

```cpp
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

  EXPECT_EQ(paint(COVER, band(4), MASTHEAD).result, CoverBand::Result::Replayed);
  EXPECT_EQ(paint(COVER, band(0), MASTHEAD).result, CoverBand::Result::Drawn);
  EXPECT_EQ(paint(COVER, band(2), MASTHEAD).result, CoverBand::Result::Replayed);
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
```

  Append to the end of `test/cover_band/CMakeLists.txt`, after `gtest_discover_tests(BandInkTest)`:

```cmake

# A fresh CoverBand draw against its replay, on the real CoverBand,
# CoverBandCache and Bitmap code. GfxRenderer is a link-time fake
# (GfxRendererFake.cpp, after test/pagination). This directory comes first so it
# supplies <esp_heap_caps.h>; test/stubs supplies <HalStorage.h>, <HalDisplay.h>,
# <Logging.h> and <Arduino.h>.
add_executable(CoverBandReplayTest
  CoverBandReplayTest.cpp
  GfxRendererFake.cpp
  CoverThumbStub.cpp
  ${REPO_ROOT}/src/components/CoverBand.cpp
  ${REPO_ROOT}/src/components/CoverBandCache.cpp
  ${REPO_ROOT}/lib/GfxRenderer/Bitmap.cpp
  ${REPO_ROOT}/lib/GfxRenderer/BitmapHelpers.cpp
  ${REPO_ROOT}/test/stubs/HalStorageFake.cpp
)

target_include_directories(CoverBandReplayTest PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${REPO_ROOT}/test/stubs
  ${REPO_ROOT}/src
  ${REPO_ROOT}/lib/EpdFont
  ${REPO_ROOT}/lib/GfxRenderer
  ${REPO_ROOT}/lib/Memory
  ${REPO_ROOT}/lib/MiniBidi
)

target_link_libraries(CoverBandReplayTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(CoverBandReplayTest)
```

- [ ] **Step 3.4: Run it and see it fail.**

  ```bash
  cmake -S test -B build/test > /dev/null && cmake --build build/test --target CoverBandReplayTest -j 8
  ```

  Expected: CMake stops at the generate step, before anything compiles:
  `Cannot find source file: …/src/components/CoverBandCache.cpp`, then
  `CMake Generate step failed`. Step 3.3 lists that file as a source, and it does not exist until
  Step 3.5. That is this task's red.

- [ ] **Step 3.5: The cache.** Create `src/components/CoverBandCache.h`:

```cpp
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include "components/CoverBand.h"

// The ink of the last few cover bands drawn, held in PSRAM for the rest of the
// wake, so a repeat paint replays it instead of streaming the cover from SD.
// Modelled on HomeVerse (RAM state that outlives the screen that filled it) and
// CatalogIndexStore (explicit PSRAM). The slots belong to the render task; only
// the epoch, through noteCoversChanged, is touched from other tasks.
class CoverBandCache {
 public:
  // Home's hero and the Go to, Publications and Meetings mastheads.
  static constexpr uint8_t SLOTS = 4;
  // Home's hero needs at most 16,240 B; a band past this is drawn fresh every time.
  static constexpr size_t MAX_SLOT_BYTES = 24 * 1024;

  static CoverBandCache& getInstance();
  CoverBandCache(const CoverBandCache&) = delete;
  CoverBandCache& operator=(const CoverBandCache&) = delete;

  // The ink held for exactly these arguments under the current epoch, or null.
  const uint8_t* find(const std::string& coverPath, const Rect& band, const CoverBand::Style& style);

  // A zeroed buffer of `bytes` to record a draw into, or null when over the cap or
  // out of PSRAM. The slot holds nothing until commit(); abandon() drops it.
  uint8_t* reserve(const std::string& coverPath, const Rect& band, const CoverBand::Style& style, size_t bytes);
  void commit();
  void abandon();

  // PSRAM held by every slot, held band or not.
  size_t bytesHeld() const;

  // Frees every slot. For a test fixture: the firmware never needs it.
  void clear();

  // Every held band stops matching. Any task. Call after the change that can
  // leave a different thumbnail at a path, never before it: a draw that read the
  // epoch first and the pixels second would otherwise hold the old pixels under
  // the new epoch.
  static void noteCoversChanged();

 private:
  CoverBandCache() = default;

  // heap_caps_malloc is the only way to demand PSRAM; makeUniqueNoThrow's
  // operator new takes no capability argument. The deleter keeps RAII.
  struct PsramFree {
    void operator()(uint8_t* p) const;
  };
  using PsramBuffer = std::unique_ptr<uint8_t[], PsramFree>;

  struct Slot {
    std::string coverPath;
    Rect band;
    CoverBand::Style style;
    uint32_t epoch = 0;
    uint32_t lastUse = 0;
    PsramBuffer bits;
    size_t bytes = 0;
    bool valid = false;
  };

  bool isLive(const Slot& slot) const;
  Slot& victim();

  Slot slots[SLOTS];
  Slot* reserved = nullptr;
  uint32_t useClock = 0;
};
```

  Create `src/components/CoverBandCache.cpp`:

```cpp
#include "CoverBandCache.h"

#include <Logging.h>
#include <esp_heap_caps.h>

#include <atomic>
#include <cstring>

namespace {

constexpr const char* MODULE = "COVER_BAND";

// Namespace scope and constant-initialised, so a bump from another task never
// races the singleton's construction.
std::atomic<uint32_t> coverEpoch{0};

bool sameBand(const Rect& a, const Rect& b) {
  return a.x == b.x && a.y == b.y && a.width == b.width && a.height == b.height;
}

bool sameStyle(const CoverBand::Style& a, const CoverBand::Style& b) {
  return a.focusBand == b.focusBand && a.cornerRadius == b.cornerRadius && a.plateHeight == b.plateHeight;
}

}  // namespace

CoverBandCache& CoverBandCache::getInstance() {
  static CoverBandCache instance;
  return instance;
}

void CoverBandCache::PsramFree::operator()(uint8_t* p) const { heap_caps_free(p); }

void CoverBandCache::noteCoversChanged() { coverEpoch.fetch_add(1); }

bool CoverBandCache::isLive(const Slot& slot) const { return slot.valid && slot.epoch == coverEpoch.load(); }

const uint8_t* CoverBandCache::find(const std::string& coverPath, const Rect& band, const CoverBand::Style& style) {
  for (Slot& slot : slots) {
    if (isLive(slot) && slot.coverPath == coverPath && sameBand(slot.band, band) && sameStyle(slot.style, style)) {
      slot.lastUse = ++useClock;
      return slot.bits.get();
    }
  }
  return nullptr;
}

CoverBandCache::Slot& CoverBandCache::victim() {
  Slot* oldest = &slots[0];
  for (Slot& slot : slots) {
    if (!isLive(slot)) return slot;
    if (slot.lastUse < oldest->lastUse) oldest = &slot;
  }
  return *oldest;
}

uint8_t* CoverBandCache::reserve(const std::string& coverPath, const Rect& band, const CoverBand::Style& style,
                                 const size_t bytes) {
  abandon();
  if (bytes == 0) return nullptr;
  if (bytes > MAX_SLOT_BYTES) {
    LOG_ERR(MODULE, "Band ink %u B over the %u B cap; not held", static_cast<unsigned>(bytes),
            static_cast<unsigned>(MAX_SLOT_BYTES));
    return nullptr;
  }

  // Read before the blit opens the cover: see noteCoversChanged.
  const uint32_t epoch = coverEpoch.load();
  Slot& slot = victim();
  slot.valid = false;
  if (slot.bytes != bytes) {
    slot.bits.reset();
    slot.bytes = 0;
    slot.bits.reset(static_cast<uint8_t*>(heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)));
    if (!slot.bits) {
      LOG_ERR(MODULE, "OOM: %u B band ink in PSRAM", static_cast<unsigned>(bytes));
      return nullptr;
    }
    slot.bytes = bytes;
  }
  memset(slot.bits.get(), 0, bytes);
  slot.coverPath = coverPath;
  slot.band = band;
  slot.style = style;
  slot.epoch = epoch;
  reserved = &slot;
  return slot.bits.get();
}

void CoverBandCache::commit() {
  if (!reserved) return;
  reserved->valid = true;
  reserved->lastUse = ++useClock;
  LOG_DBG(MODULE, "Held band %dx%d in slot %u: %u B, %u B held", reserved->band.width, reserved->band.height,
          static_cast<unsigned>(reserved - slots), static_cast<unsigned>(reserved->bytes),
          static_cast<unsigned>(bytesHeld()));
  reserved = nullptr;
}

void CoverBandCache::abandon() {
  if (!reserved) return;
  reserved->valid = false;
  reserved = nullptr;
}

size_t CoverBandCache::bytesHeld() const {
  size_t total = 0;
  for (const Slot& slot : slots) total += slot.bytes;
  return total;
}

void CoverBandCache::clear() {
  reserved = nullptr;
  for (Slot& slot : slots) slot = Slot();
  useClock = 0;
}
```

- [ ] **Step 3.6: `CoverBand` records and replays.** Replace the whole of
  `src/components/CoverBand.h` with:

```cpp
#pragma once

#include <cstdint>
#include <string>

#include "components/CoverBandGeometry.h"
#include "components/themes/BaseTheme.h"

class GfxRenderer;

// A publication's cover drawn into a rectangle at 1:1: cropped and never scaled,
// because the cached thumbnails are dithered 1-bit and any resampling turns them
// to static. Aimed at a focus band, with an optional opaque plate for a label.
namespace CoverBand {

struct Style {
  float focusBand = CoverBandGeometry::BOOK_TITLE_BAND;
  // 0: square corners, nothing masked.
  int cornerRadius = 0;
  // 0: no plate; the whole band is artwork.
  int plateHeight = 0;
};

enum class Result : uint8_t { Declined, Drawn, Replayed };

// "declined", "drawn" or "replayed", for log lines.
const char* resultName(Result result);

// Draws the cover into band, masks the corners and lays the opaque plate across
// the band's foot. A repeat with the same cover, band and style replays the ink
// held from the first draw instead of reading the cover again. Declined, with the
// band showing nothing of the cover, when it is missing, unreadable, or smaller
// than the band: a read that fails mid-stream clears the band to white first. The
// caller draws the label and any border. Render task only.
Result draw(const GfxRenderer& renderer, const std::string& coverPath, const Rect& band, const Style& style);

// Every held band stops matching. Any task; call it after the change that can
// leave a different thumbnail at a path (CoverBandCache::noteCoversChanged).
void noteCoversChanged();

// Where the caller draws its label. Zero height when style has no plate.
Rect plateRect(const Rect& band, const Style& style);

// The cached thumbnail sized to fill this band, generated when missing.
std::string thumbPathFor(const std::string& bookPath, int bandWidth, int bandHeight, bool& generatedAny);

}  // namespace CoverBand
```

  Replace the whole of `src/components/CoverBand.cpp` with:

```cpp
#include "CoverBand.h"

#include <Bitmap.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>

#include "components/BandInk.h"
#include "components/CoverBandCache.h"
#include "util/CoverThumb.h"

namespace {

constexpr const char* MODULE = "COVER_BAND";

// GfxRenderer has no clip region and drawBitmap only ever scales DOWN, so the
// row walk is done here, and walking only the band's rows and columns is what
// keeps the cover from bleeding into its neighbours. Rows are streamed through
// two row-sized buffers; nothing band-sized is held.
bool blitCropped(const GfxRenderer& renderer, const std::string& coverPath, const Rect& band, const int visibleHeight,
                 const float focusBand, const BandInk::Layout& layout, uint8_t* inkOut) {
  if (coverPath.empty()) return false;
  HalFile file;
  if (!Storage.openFileForRead(MODULE, coverPath, file)) return false;

  Bitmap bitmap(file);
  if (bitmap.parseHeaders() != BmpReaderError::Ok) return false;
  const int width = bitmap.getWidth();
  const int height = bitmap.getHeight();
  const auto crop = CoverBandGeometry::crop(width, height, band.width, band.height, visibleHeight, focusBand);
  if (!crop.fits) return false;

  auto packedRow = makeUniqueNoThrow<uint8_t[]>((width + 3) / 4);
  auto rowScratch = makeUniqueNoThrow<uint8_t[]>(bitmap.getRowBytes());
  if (!packedRow || !rowScratch) {
    LOG_ERR(MODULE, "OOM: cover row buffers");
    return false;
  }

  for (int row = 0; row < height; ++row) {
    if (bitmap.readNextRow(packedRow.get(), rowScratch.get()) != BmpReaderError::Ok) {
      LOG_ERR(MODULE, "Cover read failed at row %d: %s", row, coverPath.c_str());
      renderer.fillRect(band.x, band.y, band.width, band.height, false);
      return false;
    }
    // Rows arrive in file order; a bottom-up BMP delivers the cover's last row
    // first, so the source row has to be resolved before it can be discarded.
    const int sourceRow = bitmap.isTopDown() ? row : height - 1 - row;
    if (sourceRow < crop.yOffset || sourceRow >= crop.yOffset + band.height) continue;

    const int screenY = band.y + sourceRow - crop.yOffset;
    for (int column = 0; column < band.width; ++column) {
      const int sourceColumn = column + crop.xOffset;
      const uint8_t value = packedRow[sourceColumn / 4] >> (6 - ((sourceColumn * 2) % 8)) & 0x3;
      if (value < 3) {
        renderer.drawPixel(band.x + column, screenY, true);
        if (inkOut) BandInk::set(inkOut, layout, column, screenY - band.y);
      }
    }
  }
  return true;
}

void finishBand(const GfxRenderer& renderer, const Rect& band, const CoverBand::Style& style) {
  // The cover was blitted as a rectangle, so its corners sit outside the rounded
  // border the caller is about to draw. Masking them back to paper first is what
  // stops the art poking out past the arc.
  if (style.cornerRadius > 0) {
    renderer.maskRoundedRectOutsideCorners(band.x, band.y, band.width, band.height, style.cornerRadius);
  }

  // Opaque, because a dithered cover underneath would shred the label's glyphs
  // on a 1-bit panel.
  if (style.plateHeight > 0) {
    const Rect plate = CoverBand::plateRect(band, style);
    renderer.fillRoundedRect(plate.x, plate.y, plate.width, plate.height, style.cornerRadius, /*roundTopLeft=*/false,
                             /*roundTopRight=*/false, /*roundBottomLeft=*/true, /*roundBottomRight=*/true,
                             Color::White);
    renderer.drawLine(plate.x, plate.y, plate.x + plate.width - 1, plate.y, true);
  }
}

bool insideScreen(const GfxRenderer& renderer, const Rect& band) {
  return band.x >= 0 && band.y >= 0 && band.x + band.width <= renderer.getScreenWidth() &&
         band.y + band.height <= renderer.getScreenHeight();
}

// Writes exactly what drawPixel(x, y, true) would for every set bit. In Portrait
// a band column is one physical framebuffer row and BandInk lays each column's
// run over that row's bytes, so the replay is one AND per byte through the
// raw-writer target, clipped to an active strip the way drawPixel clips. Any
// other orientation, or a band reaching off screen, goes through drawPixel.
void replayInk(const GfxRenderer& renderer, const Rect& band, const BandInk::Layout& layout, const uint8_t* ink) {
  if (renderer.getOrientation() == GfxRenderer::Portrait && insideScreen(renderer, band)) {
    uint8_t* target = renderer.getWriteTarget();
    const int originY = renderer.getWriteOriginY();
    const int rows = renderer.getWriteRows();
    const size_t rowBytes = renderer.getDisplayWidthBytes();
    const int panelHeight = renderer.getDisplayHeight();
    for (int column = 0; column < band.width; ++column) {
      const int rowY = panelHeight - 1 - (band.x + column) - originY;
      if (static_cast<unsigned>(rowY) >= static_cast<unsigned>(rows)) continue;
      uint8_t* row = target + static_cast<size_t>(rowY) * rowBytes + static_cast<size_t>(band.y >> 3);
      const uint8_t* run = ink + static_cast<size_t>(column) * static_cast<size_t>(layout.stride);
      for (int i = 0; i < layout.stride; ++i) row[i] &= static_cast<uint8_t>(~run[i]);
    }
    return;
  }
  for (int column = 0; column < band.width; ++column) {
    for (int row = 0; row < band.height; ++row) {
      if (BandInk::test(ink, layout, column, row)) renderer.drawPixel(band.x + column, band.y + row, true);
    }
  }
}

}  // namespace

namespace CoverBand {

const char* resultName(const Result result) {
  switch (result) {
    case Result::Drawn:
      return "drawn";
    case Result::Replayed:
      return "replayed";
    case Result::Declined:
      break;
  }
  return "declined";
}

Result draw(const GfxRenderer& renderer, const std::string& coverPath, const Rect& band, const Style& style) {
  if (coverPath.empty()) return Result::Declined;
  CoverBandCache& cache = CoverBandCache::getInstance();
  const BandInk::Layout layout = BandInk::layoutFor(band.y, band.width, band.height);

  if (const uint8_t* ink = cache.find(coverPath, band, style)) {
    replayInk(renderer, band, layout, ink);
    finishBand(renderer, band, style);
    return Result::Replayed;
  }

  uint8_t* inkOut = cache.reserve(coverPath, band, style, BandInk::bytesFor(layout));
  if (!blitCropped(renderer, coverPath, band, band.height - style.plateHeight, style.focusBand, layout, inkOut)) {
    cache.abandon();
    return Result::Declined;
  }
  cache.commit();
  finishBand(renderer, band, style);
  return Result::Drawn;
}

void noteCoversChanged() { CoverBandCache::noteCoversChanged(); }

Rect plateRect(const Rect& band, const Style& style) {
  return Rect{band.x, CoverBandGeometry::plateTop(band.y, band.height, style.plateHeight), band.width,
              style.plateHeight};
}

std::string thumbPathFor(const std::string& bookPath, const int bandWidth, const int bandHeight, bool& generatedAny) {
  return CoverThumb::pathFor(bookPath, CoverBandGeometry::thumbHeightFor(bandWidth, bandHeight), generatedAny);
}

}  // namespace CoverBand
```

  Re-read both files first. The only content this replaces is what the spec §7 describes:
  `blitCropped` gains `layout` and `inkOut`, the finishing code moves into `finishBand`, and
  `replayInk`, `insideScreen`, `resultName` and `noteCoversChanged` are new. If anything else in
  the current files differs from what this replacement keeps, stop and report.

- [ ] **Step 3.7: The two callers.**

  `src/components/Masthead.cpp`: replace

```cpp
  [[maybe_unused]] const unsigned long startMs = millis();
  const bool drawn = CoverBand::draw(renderer, coverPath, toRect(band), style);
  LOG_DBG(MODULE, "Band %s in %lu ms", drawn ? "drawn" : "declined", millis() - startMs);

  if (!drawn) {
```

  with

```cpp
  [[maybe_unused]] const unsigned long startMs = millis();
  const CoverBand::Result result = CoverBand::draw(renderer, coverPath, toRect(band), style);
  LOG_DBG(MODULE, "Band %s in %lu ms", CoverBand::resultName(result), millis() - startMs);

  if (result == CoverBand::Result::Declined) {
```

  `src/activities/launcher/LauncherActivity.cpp`: replace

```cpp
#include "LauncherActivity.h"

#include <GfxRenderer.h>
```

  with

```cpp
#include "LauncherActivity.h"

#include <Arduino.h>
#include <GfxRenderer.h>
```

  and replace (in `drawHero`)

```cpp
  const CoverBand::Style style{CoverBandGeometry::BOOK_TITLE_BAND, RADIUS, layout.plate.height};
  const bool drawn = !bibleCoverPath.empty() && CoverBand::draw(renderer, bibleCoverPath, toRect(layout.hero), style);
```

  with

```cpp
  const CoverBand::Style style{CoverBandGeometry::BOOK_TITLE_BAND, RADIUS, layout.plate.height};
  bool drawn = false;
  if (!bibleCoverPath.empty()) {
    [[maybe_unused]] const unsigned long startMs = millis();
    const CoverBand::Result result = CoverBand::draw(renderer, bibleCoverPath, toRect(layout.hero), style);
    LOG_DBG(MODULE, "Band %s in %lu ms", CoverBand::resultName(result), millis() - startMs);
    drawn = result != CoverBand::Result::Declined;
  }
```

- [ ] **Step 3.8: Run the suites and see them pass.**

  ```bash
  cmake -S test -B build/test > /dev/null && cmake --build build/test --target CoverBandReplayTest BandInkTest CoverBandGeometryTest -j 8
  ./build/test/cover_band/CoverBandReplayTest && ./build/test/cover_band/BandInkTest && ./build/test/cover_band/CoverBandGeometryTest
  ```

  The reconfigure is needed: Step 3.4's failed generate left no `CoverBandReplayTest` target behind.

  Expected: `[  PASSED  ] 14 tests.`, then `6 tests`, then `16 tests`.

  On the host, `CoverBandCache.cpp` warns `unused variable 'MODULE'`. That is expected: the host
  `Logging.h` stub expands to nothing. `src/study/TagPaletteFile.cpp` and `PassageFile.cpp` warn the
  same way.

- [ ] **Step 3.9: Prove the seeded frames catch a whole-byte replay** (spec §11, review 1 MAJOR 1).
  Mutate, run, restore, rebuild:

  ```bash
  cp src/components/CoverBand.cpp /tmp/CoverBand.cpp.keep
  sed -i '' 's/row\[i\] &= static_cast<uint8_t>(~run\[i\]);/row[i] = static_cast<uint8_t>(~run[i]);/' src/components/CoverBand.cpp
  cmake --build build/test --target CoverBandReplayTest -j 8 && ./build/test/cover_band/CoverBandReplayTest | tail -3
  cp /tmp/CoverBand.cpp.keep src/components/CoverBand.cpp && touch src/components/CoverBand.cpp
  cmake --build build/test --target CoverBandReplayTest -j 8 && ./build/test/cover_band/CoverBandReplayTest | tail -1
  ```

  Expected: the mutated run reports `4 FAILED TESTS`. The restored run reports
  `[  PASSED  ] 14 tests.`

  The `touch` is needed: `cp` keeps an older mtime, and without it the restored build is skipped.

- [ ] **Step 3.10: Build the firmware.**

  ```bash
  /Volumes/stein/.platformio/penv/bin/pio run -e x4pro 2>&1 | grep -E "warning|error|SUCCESS|FAILED"
  ```

  Expected: `x4pro SUCCESS`, with no warning that names a file this task touched.

- [ ] **Step 3.11: Commit.**

  ```bash
  git add lib/GfxRenderer/GfxRenderer.cpp src/components/CoverBand.h src/components/CoverBand.cpp \
    src/components/CoverBandCache.h src/components/CoverBandCache.cpp src/components/Masthead.cpp \
    src/activities/launcher/LauncherActivity.cpp test/cover_band/
  git commit -m "perf: replay cover bands from PSRAM instead of the card" \
    -m "The first draw of a band records its ink; a repeat with the same cover, band and style ANDs it back into the framebuffer. Home now logs its band time." \
    -m "Claude-Session: https://claude.ai/code/session_01Tku853xEHzdrPyZ8XZm3yW"
  ```

---

## Task 4: Drop held bands when a thumbnail can change (spec A3, A4)

Each bump goes **after** the change completes, never before.

- [ ] **Step 4.1: `src/util/CoverThumb.cpp`.** Replace

```cpp
#include <SdPaths.h>

namespace {
```

  with

```cpp
#include <SdPaths.h>

#include "components/CoverBand.h"

namespace {
```

  and replace

```cpp
    LOG_DBG(MODULE, "No cover thumbnail for %s", bookPath.c_str());
    return {};
  }
  return Storage.exists(path.c_str()) ? path : std::string{};
```

  with

```cpp
    LOG_DBG(MODULE, "No cover thumbnail for %s", bookPath.c_str());
    return {};
  }
  CoverBand::noteCoversChanged();
  return Storage.exists(path.c_str()) ? path : std::string{};
```

- [ ] **Step 4.2: `src/util/BookCacheUtils.cpp`.** Replace

```cpp
#include "BookProgress.h"
```

  with

```cpp
#include "BookProgress.h"
#include "components/CoverBand.h"
```

  and replace

```cpp
    Epub(path, sdpaths::CROSSPOINT_DIR).clearCache();
  } else {
    return;
  }
```

  with

```cpp
    Epub(path, sdpaths::CROSSPOINT_DIR).clearCache();
  } else {
    return;
  }
  CoverBand::noteCoversChanged();
```

- [ ] **Step 4.3: `src/util/CardBooks.cpp`.** Replace

```cpp
#include "RecentBooksStore.h"
#include "util/BookCacheUtils.h"
```

  with

```cpp
#include "RecentBooksStore.h"
#include "components/CoverBand.h"
#include "util/BookCacheUtils.h"
```

  and replace

```cpp
  Storage.removeDir(bookCachePath(bookPath).c_str());
```

  with

```cpp
  Storage.removeDir(bookCachePath(bookPath).c_str());
  CoverBand::noteCoversChanged();
```

- [ ] **Step 4.4: `src/activities/settings/ClearCacheActivity.cpp`.** Replace

```cpp
#include "MappedInputManager.h"
#include "components/UITheme.h"
```

  with

```cpp
#include "MappedInputManager.h"
#include "components/CoverBand.h"
#include "components/UITheme.h"
```

  and replace (the end of `ClearCacheActivity::clearCache`)

```cpp
  root.close();

  LOG_DBG("CLEAR_CACHE", "Cache cleared: %d removed, %d failed", clearedCount, failedCount);
```

  with

```cpp
  root.close();
  CoverBand::noteCoversChanged();

  LOG_DBG("CLEAR_CACHE", "Cache cleared: %d removed, %d failed", clearedCount, failedCount);
```

- [ ] **Step 4.5: `src/activities/reader/EpubReaderActivity.cpp`.** Replace

```cpp
#include "activities/settings/TextSettingsActivity.h"
#include "components/UITheme.h"
```

  with

```cpp
#include "activities/settings/TextSettingsActivity.h"
#include "components/CoverBand.h"
#include "components/UITheme.h"
```

  and replace (the `DELETE_CACHE` menu action)

```cpp
          epub->clearCache();
          epub->setupCacheDir();
```

  with

```cpp
          epub->clearCache();
          CoverBand::noteCoversChanged();
          epub->setupCacheDir();
```

- [ ] **Step 4.6: Build the firmware.** No host target compiles these five files. The epoch itself
  is tested by `AChangedCoverIsDrawnFromTheCardAfterTheBump` (Task 3).

  ```bash
  /Volumes/stein/.platformio/penv/bin/pio run -e x4pro 2>&1 | grep -E "warning|error|SUCCESS|FAILED"
  ```

  Expected: `x4pro SUCCESS`, with no warning in these files.

- [ ] **Step 4.7: Commit.**

  ```bash
  git add src/util/CoverThumb.cpp src/util/BookCacheUtils.cpp src/util/CardBooks.cpp \
    src/activities/settings/ClearCacheActivity.cpp src/activities/reader/EpubReaderActivity.cpp
  git commit -m "perf: drop held cover bands when a thumbnail can change" -m "Claude-Session: https://claude.ai/code/session_01Tku853xEHzdrPyZ8XZm3yW"
  ```

---

## Task 5: Full verification

- [ ] **Step 5.1: Every host suite.**

  ```bash
  cmake --build build/test -j 8 && ctest --test-dir build/test -j 8 | tail -3
  ```

  Expected: `100% tests passed`. At 1ba1cd34 plus this work that is 1,574 tests. Siblings may have
  added more.

- [ ] **Step 5.2: Format the whole tree, as CI does.**

  ```bash
  git add -A src lib test && ./bin/clang-format-fix && git status --short
  ```

  Expected: no modified files. If it changed anything, commit it:

  ```bash
  git commit -am "style: format" -m "Claude-Session: https://claude.ai/code/session_01Tku853xEHzdrPyZ8XZm3yW"
  ```

- [ ] **Step 5.3: Static analysis.**

  ```bash
  /Volumes/stein/.platformio/penv/bin/pio check -e x4pro --skip-packages 2>&1 | grep -E "CoverBand|BandInk|Masthead|LauncherActivity" | head
  ```

  Expected: no defects in the touched files. Report any it finds. This is optional if `pio check`
  is not set up here.

- [ ] **Step 5.4: Release build.**

  ```bash
  /Volumes/stein/.platformio/penv/bin/pio run -e x4pro-gh_release 2>&1 | grep -E "warning|error|SUCCESS|FAILED"
  ```

  Expected: `SUCCESS`. With `LOG_LEVEL=0` the `startMs` locals are `[[maybe_unused]]`, so there
  must be no unused-variable warning.

---

## Task 6: Push and open the PR (implement phase)

- [ ] **Step 6.1: Push.** `git push`.

- [ ] **Step 6.2: Open the PR** against `main`, with `gh pr create --repo victorstein/berean-os`.
  Title: `perf: replay cover bands from PSRAM instead of re-reading the cover`. Body:

```markdown
## What

Every masthead paint (Go to, Publications, Meetings) and every Home paint streamed the whole 773-row
cover thumbnail from SD and decoded it, to draw a band 120 or 272 rows tall. Now the first draw of a
band records its ink into PSRAM. A repeat with the same cover, band and style replays it: one AND
per byte, in Portrait, straight into the framebuffer. Home now logs its band time.

Design: `docs/superpowers/specs/2026-10-11-berean-o-2-design.md`.

## Before (baseline, `x4pro` dev build of 1ba1cd34, UC8179)

- `[MASTHEAD] Band drawn in 223–229 ms` on every masthead paint, n=11 (brief). The baseline note
  states 225 ms for Go to and Publications. Representative line:
  `[155060] [DBG] [MASTHEAD] Band drawn in 228 ms`.
- Home: no band line. Home's paint was 241–257 ms of CPU.

## After: what to flash and what to read

Flash this branch's `x4pro` dev build and capture serial. With ≥ 10 samples each:

1. **Wake to Home:** `[LAUNCH] Band drawn in N ms` on the first frame, then
   `[LAUNCH] Band replayed in N ms` on the post-verse paint.
2. **Move the Home selection:** `[LAUNCH] Band replayed in N ms` on each move.
3. **Home → Go to, then step the picker:** `[MASTHEAD] Band drawn in ~225 ms` first, then
   `[MASTHEAD] Band replayed in N ms`.
4. **Publications twice, Meetings twice:** `drawn` on the first entry of each, then `replayed`.
5. **`[COVER_BAND] Held band WxH in slot S: B B, T B held`** on each fill. The last `T` is the PSRAM
   figure.
6. **By eye:** each replayed cover matches its first paint, with no shear or mirroring, the plate
   rule present, and Home's rounded corners intact. Check both themes.
7. **Invalidation:** delete or re-download a publication whose cover is on a masthead. The next
   paint of that band logs `drawn` and shows the current cover, or the fallback header.
8. **First paint:** report the first `Band drawn in N ms` against 225 ms. Recording adds to it, by
   an amount that has not been measured.

Generating any thumbnail flushes every held band. So the first paint after a screen that generated
one logs `drawn`, which is expected. **No device result is claimed here.** The replay is estimated
at ≤ 2 ms from the instruction counts (spec §4).

## Memory

- **PSRAM:** up to four slot masks, each exactly sized, in explicit PSRAM
  (`heap_caps_malloc(SPIRAM|8BIT)`, the `PsramFree` idiom from `CatalogIndexStore`).
  - Hard cap: 4 × 24,576 = **98,304 B**.
  - Expected: 3 × 7,424 + 16,240 = **38,512 B**.
  - The masks are read only by the render task. They live in PSRAM so internal SRAM is left to the
    render path. PSRAM free is ~8.2 MB.
- **Internal SRAM:** the singleton (~0.3 KB `.bss`) and one ~55 B path string per slot.
- **On OOM**, the band is drawn as before and nothing is held.

## Invalidation

The cache key is every argument of `CoverBand::draw`, plus an atomic epoch. The epoch is bumped
after:

- `clearBookCache`;
- `CardBooks::remove`;
- thumbnail generation in `CoverThumb::pathFor`;
- Clear Cache;
- the reader's delete-cache action.

Sleep is a chip reset, and a theme change alters the band's size, so neither needs handling.

## Tests

- `BandInkTest`: 6 tests.
- `CoverBandReplayTest`: 14 tests, on the real `CoverBand`, `CoverBandCache` and `Bitmap` code.
  Fresh draw and replay compare all 48,000 framebuffer bytes from the same seeded frame, across all
  8 byte alignments, both BMP row orders, strip mode, the `drawPixel` fallback, the key, the epoch,
  LRU, OOM, a truncated cover, an empty path and the size cap.
- `StorageIoTest` gains `seekCur`.
- With the replay's `&=` mutated to `=`, 4 tests fail.

## For the orchestrator

Neither `test/CMakeLists.txt` nor `src/main.cpp` needs a line (`test/cover_band` is already
registered). The shared stub `test/stubs/HalStorageFake.cpp` gains `HalFile::seekCur`.

Follow-ups filed: `t2/x1` (seek to the crop rows on the first paint) and `t2/x2` (WebDAV COPY keeps
the old book's cache).

https://claude.ai/code/session_01Tku853xEHzdrPyZ8XZm3yW

Refs berean-o-2
```

  The brief requires the body to end with the line `Refs berean-o-2`, so it goes last, below the
  session link.

---

## Changes after plan review 0 (`docs/superpowers/reviews/berean-o-2-plan-review-0.md`, CLEAR)

| Finding | Change |
|---|---|
| MAJOR 1: the replay test never drew a 272-row Home band. `&style == &HOME` compared against an `initializer_list` copy. | Step 3.3: the loop iterates over `&MASTHEAD, &HOME, &MEETINGS`, so 8 of its 24 bands are 272 rows. The reviewer ran it: it passes, and Step 3.9's mutation still fails 4 tests. |
| MAJOR 2: Step 3.4 fails at CMake generate rather than compile, and Step 3.8 then finds no target | Step 3.4's expected output names the generate failure. Step 3.8 reconfigures first. |
| MINOR 3: two expected outputs | Task 0 accepts bootstrap's link-branch output. Step 3.1's grep is anchored with `drawPixel(`. |

