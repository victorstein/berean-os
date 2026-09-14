#pragma once

#include <cstdint>

class GfxRenderer;

// The screen the one-off study migration draws behind itself.
//
// This runs in setup(), before any Activity exists, so it paints straight to
// the renderer the way BootActivity does rather than going through UiScreen.
//
// It exists because the first migration does real work -- indexing the
// documents a user has marked, and building the Bible's spine-to-book map --
// and without it the panel simply stays white for the whole of it, which reads
// as a dead device.
namespace migration_screen {

// Draws the mark, the title and an indeterminate bar, and pushes the frame.
// `step` advances the bar's travelling segment; the total is unknown up front,
// so this shows MOTION rather than a percentage that would be invented.
void draw(const GfxRenderer& renderer, const char* label, uint16_t step);

}  // namespace migration_screen
