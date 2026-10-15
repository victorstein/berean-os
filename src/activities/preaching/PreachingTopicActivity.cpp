#include "PreachingTopicActivity.h"

#include <Arduino.h>
#include <EpdFontFamily.h>
#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>
#include <Preaching/IntroSection.h>

#include <algorithm>
#include <cstdio>
#include <string_view>
#include <utility>
#include <variant>

#include "MappedInputManager.h"
#include "PreachingShowActivity.h"
#include "PreachingSession.h"
#include "activities/PostedMessage.h"
#include "components/UITheme.h"
#include "components/UiAppHelpers.h"
#include "fontIds.h"

namespace fui = freeink::ui;

namespace {

constexpr const char* MODULE = "PREACH";

constexpr fui::ActionId ACTION_QUOTE = 1;
constexpr fui::ActionId ACTION_REFERENCE = 2;
constexpr fui::ActionId ACTION_NEXT_VISIT = 3;

constexpr int CARD_BORDER = 1;
constexpr int CARD_BORDER_SELECTED = 3;
constexpr const char* REFERENCE_MARK = "›";
constexpr const char* REFERENCE_MARK_SPACED = "› ";

struct FaceFont {
  int fontId;
  EpdFontFamily::Style style;
};

// Indexed by preaching::Face.
constexpr FaceFont FACE_FONTS[preaching::FACE_COUNT] = {
    {NOTOSERIF_14_FONT_ID, EpdFontFamily::BOLD},     // Title
    {UI_10_FONT_ID, EpdFontFamily::BOLD},            // Heading
    {NOTOSERIF_12_FONT_ID, EpdFontFamily::ITALIC},   // Quote
    {NOTOSERIF_12_FONT_ID, EpdFontFamily::REGULAR},  // Body
    {NOTOSERIF_12_FONT_ID, EpdFontFamily::BOLD},     // Reference
    {UI_10_FONT_ID, EpdFontFamily::BOLD},            // Label
};

const FaceFont& faceFont(const preaching::Face face) { return FACE_FONTS[static_cast<size_t>(face)]; }

int measureFace(const void* ctx, const preaching::Face face, const char* text) {
  const FaceFont& font = faceFont(face);
  return static_cast<const GfxRenderer*>(ctx)->getTextWidth(font.fontId, text, font.style);
}

}  // namespace

PreachingTopicActivity::PreachingTopicActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                               const PreachingSession& session, std::unique_ptr<TopicCards> cards,
                                               const int startVerse)
    : Activity("PreachingTopic", renderer, mappedInput),
      UiAppHost(renderer),
      session(session),
      topic(std::move(cards)),
      startVerse(startVerse) {}

void PreachingTopicActivity::onEnter() {
  Activity::onEnter();
  computeGeometry();
  layout = makeUniqueNoThrow<preaching::Layout>();
  if (!layout || !layOut(*topic, *layout)) {
    LOG_ERR(MODULE, "Could not lay out the topic");
    layout.reset();
    PostedMessage::post(tr(STR_PREACHING_OPEN_FAILED));
    finishWithResult();
    return;
  }
  cursor = startVerse < 0 ? preaching::atPage(*layout, 0) : preaching::selectVerse(*layout, startVerse);

  resetUi();
  app.on(ACTION_QUOTE, &PreachingTopicActivity::onQuoteTapped, this);
  app.on(ACTION_REFERENCE, &PreachingTopicActivity::onReferenceTapped, this);
  app.on(ACTION_NEXT_VISIT, &PreachingTopicActivity::onNextVisitTapped, this);
  app.setScreen(&PreachingTopicActivity::topicScreen, this);
  requestUpdate();
}

void PreachingTopicActivity::computeGeometry() {
  const auto& metrics = UITheme::getInstance().getMetrics();
  geometry.bodyTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  geometry.pageHeight =
      renderer.getScreenHeight() - metrics.buttonHintsHeight - metrics.verticalSpacing - geometry.bodyTop;
  geometry.cardX = metrics.contentSidePadding;
  geometry.cardWidth = renderer.getScreenWidth() - 2 * metrics.contentSidePadding;
  geometry.textX = geometry.cardX + metrics.listSidePadding;
  geometry.textWidth = geometry.cardWidth - 2 * metrics.listSidePadding;
  geometry.cardPadding = metrics.verticalSpacing;
  geometry.cardGap = metrics.verticalSpacing;
  geometry.cornerRadius = metrics.listRowRadius;
  const FaceFont& reference = faceFont(preaching::Face::Reference);
  geometry.markWidth = renderer.getTextWidth(reference.fontId, REFERENCE_MARK_SPACED, reference.style);
}

bool PreachingTopicActivity::layOut(const TopicCards& cards, preaching::Layout& out) const {
  preaching::Metrics metrics;
  metrics.width = geometry.textWidth;
  metrics.pageHeight = geometry.pageHeight;
  metrics.cardPadding = geometry.cardPadding;
  metrics.cardGap = geometry.cardGap;
  metrics.referenceIndent = geometry.markWidth;
  for (size_t face = 0; face < preaching::FACE_COUNT; ++face) {
    metrics.lineHeight[face] = renderer.getLineHeight(FACE_FONTS[face].fontId);
  }
  return preaching::layoutCards(cards.cards.data(), cards.cards.size(), metrics, &measureFace, &renderer, out);
}

void PreachingTopicActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    finishWithResult();
    return;
  }

  const auto touch = routeTouch(mappedInput);
  if (touch.routed && app.invalidated()) requestUpdate();
  if (touch) return;
  if (!layout) return;

  const auto swipe = mappedInput.wasSwipe();
  if (swipe == MappedInputManager::SwipeDir::Up || swipe == MappedInputManager::SwipeDir::Down) {
    preaching::CardCursor next = cursor;
    if (preaching::turnPage(*layout, next, swipe == MappedInputManager::SwipeDir::Up ? 1 : -1)) moveTo(next);
    return;
  }

  buttonNavigator.onNextRelease([this] {
    preaching::CardCursor next = cursor;
    if (preaching::nextCard(*layout, next)) moveTo(next);
  });
  buttonNavigator.onPreviousRelease([this] {
    preaching::CardCursor next = cursor;
    if (preaching::previousCard(*layout, next)) moveTo(next);
  });
}

void PreachingTopicActivity::moveTo(const preaching::CardCursor& next) {
  {
    RenderLock lock(*this);
    cursor = next;
  }
  requestUpdate();
}

PreachingOrigin PreachingTopicActivity::originAt(const int verse) const {
  if (topic->kind == PreachingOrigin::Kind::Question) return PreachingOrigin::ofQuestion(topic->question, verse);
  if (topic->kind == PreachingOrigin::Kind::Tag) return PreachingOrigin::ofTag(study::toRaw(topic->tag), verse);
  return {};
}

void PreachingTopicActivity::finishWithResult() {
  setResult(PreachingResult{originAt(cursor.selected)});
  finish();
}

void PreachingTopicActivity::reportFailure(const char* message) {
  PostedMessage::post(message);
  requestUpdate();
}

void PreachingTopicActivity::openShow(const int verse) {
  if (verse < 0 || verse >= topic->verseCount()) return;
  auto show = makeUniqueNoThrow<PreachingShowActivity>(
      renderer, mappedInput, preaching::show::Deck{topic->cards.data(), topic->cards.size()}, topic->header.c_str(),
      verse);
  if (!show) {
    LOG_ERR(MODULE, "OOM: show mode");
    reportFailure(tr(STR_PREACHING_OPEN_FAILED));
    return;
  }
  app.clearTapFlash();
  startActivityForResult(std::move(show), [this](const ActivityResult& result) { onShowFinished(result); });
}

void PreachingTopicActivity::onShowFinished(const ActivityResult& result) {
  // Coming back from full-screen large type: a fast refresh would leave it ghosted.
  halfRefreshPending = true;
  const auto* returned = std::get_if<PreachingResult>(&result.data);
  if (!returned || !layout) return;
  RenderLock lock(*this);
  cursor = preaching::selectVerse(*layout, returned->at.verse);
}

void PreachingTopicActivity::openBible(const int verse) {
  if (verse < 0 || verse >= topic->verseCount()) return;
  const size_t source = topic->verseSource[static_cast<size_t>(verse)];
  if (!session.hasBible()) {
    LOG_ERR(MODULE, "No Bible to open verse %d in", verse);
    reportFailure(tr(STR_PREACHING_VERSE_NOT_FOUND));
    return;
  }

  ReaderEntryIntent intent;
  if (topic->kind == PreachingOrigin::Kind::Question) {
    const std::string& href = topic->parsed.blocks[source].href;
    [[maybe_unused]] const unsigned long started = millis();
    const bool resolves = ReaderEntryIntent::openHref(href, originAt(verse), intent) &&
                          session.book()->resolveHrefToSpineIndex(href) >= 0;
    LOG_DBG(MODULE, "Reference check: %lu ms", millis() - started);
    if (!resolves) {
      LOG_ERR(MODULE, "Reference not opened: \"%s\"", href.c_str());
      reportFailure(tr(STR_PREACHING_VERSE_NOT_FOUND));
      return;
    }
  } else {
    const PreachingPassages::Passage& passage = topic->passages[source];
    intent = ReaderEntryIntent::of(ReaderEntryIntent::Kind::OpenAt);
    intent.unit = passage.start;
    intent.spineHint = passage.spine;
    intent.origin = originAt(verse);
  }
  app.clearTapFlash();
  activityManager.goToReader(session.biblePath(), false, intent);
}

void PreachingTopicActivity::openNextQuestion() {
  if (topic->kind != PreachingOrigin::Kind::Question) return;
  const int next = preaching::IntroSection::nextQuestion(topic->question);
  if (next == 0) return;

  auto cards = makeUniqueNoThrow<TopicCards>();
  auto nextLayout = makeUniqueNoThrow<preaching::Layout>();
  if (!cards || !nextLayout) {
    LOG_ERR(MODULE, "OOM: question %d", next);
    reportFailure(tr(STR_PREACHING_OPEN_FAILED));
    return;
  }
  const auto result = PreachingContent::loadQuestion(session, renderer, next, *cards);
  if (result != PreachingContent::LoadResult::Ok) {
    reportFailure(PreachingContent::failureMessage(result));
    return;
  }
  if (!layOut(*cards, *nextLayout)) {
    LOG_ERR(MODULE, "Could not lay out question %d", next);
    reportFailure(tr(STR_PREACHING_OPEN_FAILED));
    return;
  }
  {
    RenderLock lock(*this);
    std::swap(topic, cards);
    std::swap(layout, nextLayout);
    cursor = preaching::atPage(*layout, 0);
  }
  app.clearTapFlash();
  halfRefreshPending = true;
  requestUpdate();
}

void PreachingTopicActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const auto& metrics = UITheme::getInstance().getMetrics();
  char pageIndicator[24];
  snprintf(pageIndicator, sizeof(pageIndicator), "%d/%d", cursor.page + 1, layout ? layout->pageCount() : 1);
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, renderer.getScreenWidth(), metrics.headerHeight},
                 topic->header.c_str(), pageIndicator);

  if (topic->cards.empty()) {
    renderer.drawCenteredText(UI_10_FONT_ID, geometry.bodyTop + geometry.pageHeight / 2, tr(STR_PREACHING_TOPIC_EMPTY));
  } else if (layout) {
    drawPage();
  }

  renderUi();

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer(halfRefreshPending.exchange(false) ? HalDisplay::HALF_REFRESH : HalDisplay::FAST_REFRESH);
  PostedMessage::drawNext(renderer);
}

void PreachingTopicActivity::drawPage() const {
  const preaching::Layout& page = *layout;
  for (size_t s = page.pageFirstSegment[static_cast<size_t>(cursor.page)]; s < page.segmentsEnd(cursor.page); ++s) {
    const preaching::Segment& segment = page.segments[s];
    const preaching::CardInput& card = topic->cards[segment.card];
    const int top = geometry.bodyTop + segment.y;
    const bool boxed = preaching::isBoxed(card.kind);
    if (boxed) {
      const bool selected = page.cardVerse[segment.card] >= 0 && page.cardVerse[segment.card] == cursor.selected;
      renderer.drawRoundedRect(geometry.cardX, top, geometry.cardWidth, segment.height,
                               selected ? CARD_BORDER_SELECTED : CARD_BORDER, geometry.cornerRadius, true);
    }
    int y = top + (boxed && segment.opensCard ? geometry.cardPadding : 0);
    for (uint16_t i = 0; i < segment.lineCount; ++i) {
      const preaching::Line& line = page.lines[segment.firstLine + i];
      drawLine(card, line, y);
      y += renderer.getLineHeight(faceFont(line.face).fontId);
    }
  }
}

void PreachingTopicActivity::drawLine(const preaching::CardInput& card, const preaching::Line& line,
                                      const int y) const {
  const std::string_view source = line.reference ? card.reference : card.body;
  char text[preaching::LINE_BUFFER_BYTES];
  const size_t length = std::min<size_t>(line.length, sizeof(text) - 1);
  // Tabs and newlines in a passage's text are break points to the layout and spaces on screen.
  for (size_t i = 0; i < length; ++i) {
    const char c = source[line.begin + i];
    text[i] = static_cast<unsigned char>(c) < 0x20 ? ' ' : c;
  }
  text[length] = '\0';

  const FaceFont& font = faceFont(line.face);
  int x = geometry.textX;
  if (card.kind == preaching::CardKind::Verse && line.reference) {
    x = geometry.textX + geometry.textWidth - renderer.getTextWidth(font.fontId, text, font.style);
    if (line.firstReference)
      renderer.drawText(font.fontId, x - geometry.markWidth, y, REFERENCE_MARK, true, font.style);
  }
  renderer.drawText(font.fontId, x, y, text, true, font.style);
}

void PreachingTopicActivity::topicScreen(UiScreen& screen, void* user) {
  static_cast<const PreachingTopicActivity*>(user)->buildHits(screen);
}

void PreachingTopicActivity::buildHits(UiScreen& screen) const {
  // The cards are drawn in render(); this only registers touch rects over them.
  if (!mappedInput.hasTouch() || !layout) return;
  const preaching::Layout& page = *layout;
  for (size_t s = page.pageFirstSegment[static_cast<size_t>(cursor.page)]; s < page.segmentsEnd(cursor.page); ++s) {
    const preaching::Segment& segment = page.segments[s];
    const preaching::CardKind kind = topic->cards[segment.card].kind;
    const int top = geometry.bodyTop + segment.y;
    const int bottom = top + segment.height;
    if (kind == preaching::CardKind::NextVisit) {
      screen.frame().hit(fui::makeRect(geometry.cardX, top, geometry.cardWidth, segment.height), ACTION_NEXT_VISIT, 0,
                         fui::InputTouch);
      continue;
    }
    if (kind != preaching::CardKind::Verse) continue;

    const int16_t verse = page.cardVerse[segment.card];
    int y = top + (segment.opensCard ? geometry.cardPadding : 0);
    int referenceTop = -1;
    bool hasQuote = false;
    for (uint16_t i = 0; i < segment.lineCount; ++i) {
      const preaching::Line& line = page.lines[segment.firstLine + i];
      if (line.reference && referenceTop < 0) referenceTop = y;
      if (!line.reference) hasQuote = true;
      y += renderer.getLineHeight(faceFont(line.face).fontId);
    }
    const int split = referenceTop < 0 ? bottom : (hasQuote ? referenceTop : top);
    if (split > top) {
      screen.frame().hit(fui::makeRect(geometry.cardX, top, geometry.cardWidth, split - top), ACTION_QUOTE, verse,
                         fui::InputTouch);
    }
    if (split < bottom) {
      screen.frame().hit(fui::makeRect(geometry.cardX, split, geometry.cardWidth, bottom - split), ACTION_REFERENCE,
                         verse, fui::InputTouch);
    }
  }
}

void PreachingTopicActivity::onQuoteTapped(const fui::ActionEvent& event, void* user) {
  static_cast<PreachingTopicActivity*>(user)->openShow(event.value);
}

void PreachingTopicActivity::onReferenceTapped(const fui::ActionEvent& event, void* user) {
  static_cast<PreachingTopicActivity*>(user)->openBible(event.value);
}

void PreachingTopicActivity::onNextVisitTapped(const fui::ActionEvent&, void* user) {
  static_cast<PreachingTopicActivity*>(user)->openNextQuestion();
}
