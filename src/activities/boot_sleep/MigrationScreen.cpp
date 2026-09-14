#include "MigrationScreen.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include "BereanMark.h"
#include "fontIds.h"

namespace migration_screen {
namespace {

constexpr int MARK_SIZE = 120;
constexpr int BAR_WIDTH = 240;
constexpr int BAR_HEIGHT = 8;
constexpr int BAR_BORDER = 1;
// The travelling segment, as a fraction of the track.
constexpr int SEGMENT_WIDTH = BAR_WIDTH / 4;
// Steps to cross the track once. Slow enough that each step is a visible move
// rather than a flicker, given a full panel refresh between them.
constexpr uint16_t STEPS_PER_SWEEP = 12;

}  // namespace

void draw(const GfxRenderer& renderer, const char* label, const uint16_t step) {
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();

  renderer.clearScreen();
  berean_mark::draw(renderer, (pageWidth - MARK_SIZE) / 2, (pageHeight - MARK_SIZE) / 2 - 40, MARK_SIZE);
  renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 + 30, tr(STR_BEREAN), true, EpdFontFamily::BOLD);
  renderer.drawCenteredText(SMALL_FONT_ID, pageHeight / 2 + 55, label);

  const int barX = (pageWidth - BAR_WIDTH) / 2;
  const int barY = pageHeight / 2 + 80;
  renderer.drawRect(barX, barY, BAR_WIDTH, BAR_HEIGHT, BAR_BORDER, true);

  // Bounces rather than wrapping, so a long phase never looks like it restarted.
  const uint16_t phase = step % (STEPS_PER_SWEEP * 2);
  const uint16_t forward = phase < STEPS_PER_SWEEP ? phase : static_cast<uint16_t>(STEPS_PER_SWEEP * 2 - phase);
  const int travel = BAR_WIDTH - 2 * BAR_BORDER - SEGMENT_WIDTH;
  const int offset = travel > 0 ? (travel * forward) / STEPS_PER_SWEEP : 0;

  renderer.fillRect(barX + BAR_BORDER + offset, barY + BAR_BORDER, SEGMENT_WIDTH, BAR_HEIGHT - 2 * BAR_BORDER, true);

  // A partial refresh: this redraws many times and a full flash on each would
  // take longer than the work it is reporting.
  renderer.displayBuffer();
}

}  // namespace migration_screen
