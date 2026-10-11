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
