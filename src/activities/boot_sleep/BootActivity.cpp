#include "BootActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include "BereanMark.h"
#include "fontIds.h"

void BootActivity::onEnter() {
  Activity::onEnter();

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  renderer.clearScreen();
  berean_mark::draw(renderer, (pageWidth - 120) / 2, (pageHeight - 120) / 2, 120);
  renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 + 70, tr(STR_BEREAN), true, EpdFontFamily::BOLD);
  renderer.drawCenteredText(SMALL_FONT_ID, pageHeight / 2 + 95, tr(STR_BOOTING));
  renderer.drawCenteredText(SMALL_FONT_ID, pageHeight - 30, BEREAN_VERSION);
  renderer.displayBuffer();
}
