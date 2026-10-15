#include "PreachingShowActivity.h"

#include <EpdFontFamily.h>
#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>

#include <algorithm>
#include <cstdio>
#include <string>
#include <string_view>

#include "MappedInputManager.h"
#include "activities/reader/ReaderUtils.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace show = preaching::show;

namespace {

constexpr const char* MODULE = "PREACH";

constexpr int RUNG_FONT_IDS[show::RUNG_COUNT] = {NOTOSERIF_18_FONT_ID, NOTOSERIF_14_FONT_ID, NOTOSERIF_12_FONT_ID};
constexpr int FOOTER_FONT_ID = UI_10_FONT_ID;

constexpr size_t QUOTE_SPANS_RESERVED = 64;
constexpr size_t REFERENCE_SPANS_RESERVED = 4;
constexpr size_t PAGES_RESERVED = 8;

constexpr preaching::CardInput NO_CARD{};

constexpr EpdFontFamily::Style styleOf(const preaching::Face face) {
  return face == preaching::Face::Reference ? EpdFontFamily::BOLD : EpdFontFamily::ITALIC;
}

int measureAtRung(const void* ctx, const uint8_t rung, const preaching::Face face, const char* text) {
  return static_cast<const GfxRenderer*>(ctx)->getTextWidth(RUNG_FONT_IDS[rung], text, styleOf(face));
}

void copyLine(const std::string_view source, const preaching::Span span, char (&out)[preaching::LINE_BUFFER_BYTES]) {
  const size_t length = std::min<size_t>(span.length, sizeof(out) - 1);
  // Tabs and newlines in a passage's text are break points to the layout and spaces on screen.
  for (size_t i = 0; i < length; ++i) {
    const char c = source[span.begin + i];
    out[i] = static_cast<unsigned char>(c) < 0x20 ? ' ' : c;
  }
  out[length] = '\0';
}

}  // namespace

PreachingShowActivity::PreachingShowActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                             const show::Deck deck, const char* title, const int startVerse)
    : Activity("PreachingShow", renderer, mappedInput), deck(deck), title(title), startVerse(startVerse) {}

void PreachingShowActivity::onEnter() {
  Activity::onEnter();
  verses = show::verseCount(deck);
  if (verses == 0) {
    LOG_ERR(MODULE, "Show mode opened on a topic with no verse");
    finishWithResult();
    return;
  }
  computeGeometry();
  layout.quote.reserve(QUOTE_SPANS_RESERVED);
  layout.reference.reserve(REFERENCE_SPANS_RESERVED);
  layout.pageFirstLine.reserve(PAGES_RESERVED);
  {
    RenderLock lock(*this);
    cursor = {std::clamp(startVerse, 0, verses - 1), 0};
    layOutVerse();
  }
  requestUpdate();
}

void PreachingShowActivity::computeGeometry() {
  int viewTop = 0;
  int viewRight = 0;
  int viewBottom = 0;
  int viewLeft = 0;
  renderer.getOrientedViewableTRBL(&viewTop, &viewRight, &viewBottom, &viewLeft);
  const auto& theme = UITheme::getInstance().getMetrics();

  geometry.footerY =
      renderer.getScreenHeight() - viewBottom - theme.verticalSpacing - renderer.getLineHeight(FOOTER_FONT_ID);
  geometry.boxTop = viewTop + theme.verticalSpacing;
  geometry.columnWidth = renderer.getScreenWidth() - viewLeft - viewRight - 2 * theme.contentSidePadding;

  metrics.width = geometry.columnWidth;
  metrics.height = geometry.footerY - theme.verticalSpacing - geometry.boxTop;
  metrics.referenceGap = theme.verticalSpacing;
  for (uint8_t rung = 0; rung < show::RUNG_COUNT; ++rung) {
    metrics.lineHeight[rung] = renderer.getLineHeight(RUNG_FONT_IDS[rung]);
  }
}

void PreachingShowActivity::layOutVerse() {
  const preaching::CardInput* card = show::verseCard(deck, cursor.verse);
  show::layOut(card ? *card : NO_CARD, metrics, &measureAtRung, &renderer, layout);
}

void PreachingShowActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back) ||
      ReaderUtils::isTouchMenuTap(renderer, mappedInput)) {
    finishWithResult();
    return;
  }

  const auto touch = ReaderUtils::detectTouchPageTurn(renderer, mappedInput);
  if (touch.next) {
    stepBy(1);
    return;
  }
  if (touch.prev) {
    stepBy(-1);
    return;
  }

  buttonNavigator.onNextRelease([this] { stepBy(1); });
  buttonNavigator.onPreviousRelease([this] { stepBy(-1); });
}

void PreachingShowActivity::stepBy(const int direction) {
  show::Cursor next = cursor;
  if (!show::step(next, layout.pageCount(), verses, direction)) return;
  {
    RenderLock lock(*this);
    const bool verseChanged = next.verse != cursor.verse;
    cursor = next;
    if (verseChanged) layOutVerse();
  }
  requestUpdate();
}

void PreachingShowActivity::finishWithResult() {
  PreachingOrigin at;
  at.verse = static_cast<uint16_t>(cursor.verse);
  setResult(PreachingResult{at});
  finish();
}

void PreachingShowActivity::render(RenderLock&&) {
  renderer.clearScreen();
  drawPage();
  drawFooter();
  ReaderUtils::displayWithRefreshCycle(renderer, pagesUntilFullRefresh);
}

void PreachingShowActivity::drawPage() const {
  const preaching::CardInput* card = show::verseCard(deck, cursor.verse);
  if (!card || cursor.page >= layout.pageCount()) return;

  const int fontId = RUNG_FONT_IDS[layout.rung];
  const int lineHeight = metrics.lineHeight[layout.rung];
  const int first = layout.pageFirstLine[static_cast<size_t>(cursor.page)];
  const int end = layout.pageEnd(cursor.page);
  const int quoteLines = static_cast<int>(layout.quote.size());
  int y = geometry.boxTop + std::max(0, (metrics.height - layout.pageHeight(cursor.page, metrics)) / 2);

  char text[preaching::LINE_BUFFER_BYTES];
  for (int line = first; line < end; ++line) {
    const bool reference = layout.isReferenceLine(line);
    if (reference && line == quoteLines && line > first) y += metrics.referenceGap;
    if (reference) {
      copyLine(card->reference, layout.reference[static_cast<size_t>(line - quoteLines)], text);
    } else {
      copyLine(card->body, layout.quote[static_cast<size_t>(line)], text);
    }
    renderer.drawCenteredText(fontId, y, text, true,
                              styleOf(reference ? preaching::Face::Reference : preaching::Face::Quote));
    y += lineHeight;
  }
}

void PreachingShowActivity::drawFooter() const {
  char footer[96];
  const int written =
      snprintf(footer, sizeof(footer), tr(STR_PREACHING_SHOW_POSITION), cursor.verse + 1, verses, title ? title : "");
  if (layout.pageCount() > 1 && written > 0 && static_cast<size_t>(written) < sizeof(footer)) {
    snprintf(footer + written, sizeof(footer) - static_cast<size_t>(written), " · %d/%d", cursor.page + 1,
             layout.pageCount());
  }
  if (renderer.getTextWidth(FOOTER_FONT_ID, footer) <= geometry.columnWidth) {
    renderer.drawCenteredText(FOOTER_FONT_ID, geometry.footerY, footer);
    return;
  }
  const std::string shortened = renderer.truncatedText(FOOTER_FONT_ID, footer, geometry.columnWidth);
  renderer.drawCenteredText(FOOTER_FONT_ID, geometry.footerY, shortened.c_str());
}
