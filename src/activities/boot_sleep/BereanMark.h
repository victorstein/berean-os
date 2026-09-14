#pragma once

#include <GfxRenderer.h>

// The boot and sleep mark: an open codex, drawn from primitives rather than
// shipped as a bitmap.
//
// Geometry, not artwork, for two reasons. The panel is 1-bit with no
// anti-aliasing, so outlined shapes survive where a dithered bitmap muddies;
// and it costs no flash, where the 120x120 bitmap it replaces cost ~11 KB of
// header.
namespace berean_mark {

// Draws inside a `size` square whose top-left is (x, y).
inline void draw(const GfxRenderer& renderer, int x, int y, int size) {
  constexpr int OUTLINE = 2;
  constexpr int CORNER = 4;

  const int pageW = size * 5 / 12;  // each page panel
  const int pageH = size * 8 / 15;
  const int gap = size / 30;  // spine half-width
  const int top = y + (size - pageH) / 2;
  const int midX = x + size / 2;

  const int leftX = midX - gap - pageW;
  const int rightX = midX + gap;

  renderer.drawRoundedRect(leftX, top, pageW, pageH, OUTLINE, CORNER, true);
  renderer.drawRoundedRect(rightX, top, pageW, pageH, OUTLINE, CORNER, true);

  // Spine: the one solid element, so the two panels read as one object.
  renderer.fillRect(midX - gap, top + pageH / 6, gap * 2, pageH * 2 / 3, true);

  // Suggested text. Inset far enough that the rules never touch the outline,
  // which at 1-bit would read as a smudge rather than a line.
  const int inset = pageW / 5;
  const int ruleW = pageW - inset * 2;
  const int firstRule = top + pageH / 4;
  const int ruleStep = pageH / 5;
  for (int i = 0; i < 3; ++i) {
    const int ry = firstRule + i * ruleStep;
    renderer.drawLine(leftX + inset, ry, leftX + inset + ruleW, ry, true);
    renderer.drawLine(rightX + inset, ry, rightX + inset + ruleW, ry, true);
  }
}

}  // namespace berean_mark
