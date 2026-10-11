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
