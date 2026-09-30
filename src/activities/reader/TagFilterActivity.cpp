#include "TagFilterActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <utility>

#include "MappedInputManager.h"
#include "ReaderUtils.h"
#include "activities/ActivityResult.h"
#include "activities/PostedMessage.h"
#include "components/UITheme.h"
#include "study/StudyStore.h"

namespace fui = freeink::ui;

TagFilterActivity::TagFilterActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                     std::optional<std::vector<size_t>> scope, const std::optional<study::TagId> filter)
    : UiListActivity("TagFilter", renderer, mappedInput, /*wantsTouchLongPress=*/true),
      scope_(std::move(scope)),
      filter_(filter) {}

void TagFilterActivity::onEnter() {
  UiListActivity::onEnter();
  rebuildChips();
  {
    RenderLock lock(*this);
    nav.selected = selectedChipIndex();
  }
  requestUpdate();
}

int TagFilterActivity::listCount() const { return static_cast<int>(chips_.size()); }

const char* TagFilterActivity::headerTitle() const { return tr(STR_FILTER_BY_TAG); }

void TagFilterActivity::rebuildChips() {
  std::vector<TagChipView::ChipEntry> next;
  TagChipView::buildEntries(scope_, filter_, SIZE_MAX, next);
  RenderLock lock(*this);
  chips_ = std::move(next);
  widths_.assign(chips_.size(), 0);
  const int last = static_cast<int>(chips_.size()) - 1;
  nav.selected = std::clamp(nav.selected, 0, std::max(last, 0));
}

int TagFilterActivity::selectedChipIndex() const {
  for (size_t i = 0; i < chips_.size(); ++i) {
    if (TagChipView::isSelected(chips_[i], filter_)) return static_cast<int>(i);
  }
  return 0;
}

void TagFilterActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  screen.setContentMargin(
      fui::Insets{static_cast<int16_t>(safe.y + metrics.topPadding + metrics.headerHeight),
                  static_cast<int16_t>(renderer.getScreenWidth() - (safe.x + safe.width)),
                  static_cast<int16_t>(renderer.getScreenHeight() - (safe.y + safe.height) + metrics.buttonHintsHeight),
                  static_cast<int16_t>(safe.x)});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  const int count = static_cast<int>(chips_.size());
  if (count == 0 || static_cast<int>(widths_.size()) != count) return;
  const TagChipView::Metrics chip = TagChipView::metricsFor(screen);
  for (int i = 0; i < count; ++i) widths_[i] = TagChipView::measure(screen, chips_[i].label, chip);

  const int inset = screen.theme().listInset;
  const fui::Rect body = screen.body();
  gridLineWidth_ = body.width - 2 * inset;
  gridGap_ = chip.gap;
  gridLinesPerPage_ = TagChips::linesPerPage(body.height, chip.chipHeight, chip.gap);
  pageCount_ = TagChips::pageCountOf(widths_.data(), count, gridLineWidth_, gridGap_, gridLinesPerPage_);
  int stripHeight = 0;
  if (pageCount_ > 1) {
    // Only a paged grid shows the page strip; taking its height out can only keep the grid paged.
    stripHeight = metrics.tabBarHeight;
    gridLinesPerPage_ = TagChips::linesPerPage(body.height - stripHeight, chip.chipHeight, chip.gap);
    pageCount_ = TagChips::pageCountOf(widths_.data(), count, gridLineWidth_, gridGap_, gridLinesPerPage_);
  }

  const TagChips::PageSpan span =
      TagChips::pageHolding(widths_.data(), count, nav.selected, gridLineWidth_, gridGap_, gridLinesPerPage_);
  pageNumber_ = span.page;
  nav.top = span.first;
  TagChips::layoutPage(widths_.data(), count, span.first, gridLineWidth_, gridGap_, gridLinesPerPage_, page_);

  const int gridTop = body.y + stripHeight;
  fui::Rect focused{};
  for (int k = 0; k < page_.placedCount; ++k) {
    const TagChips::Placed& placed = page_.placed[k];
    const TagChipView::ChipEntry& entry = chips_[static_cast<size_t>(placed.chip)];
    const fui::Rect rect{static_cast<int16_t>(body.x + inset + placed.x),
                         static_cast<int16_t>(gridTop + TagChips::lineTop(placed.line, chip.chipHeight, gridGap_)),
                         static_cast<int16_t>(placed.width), static_cast<int16_t>(chip.chipHeight)};
    TagChipView::draw(screen, rect, entry.label, TagChipView::isSelected(entry, filter_), ACTION_ROW,
                      static_cast<int16_t>(placed.chip), fui::InputTouch | fui::InputLongPress,
                      TagChips::hitPadding(page_.placed, page_.placedCount, page_.lines, k, gridGap_));
    if (placed.chip == nav.selected) focused = rect;
  }

  if (buttonFocus_ && focused.width > 0) {
    // Drawn in the gap around the chip, which hitPadding splits but nothing paints.
    // Clamped to the body horizontally: with a zero listInset an edge chip's outline would
    // otherwise fall off the screen, which HighlightsActivity's band outline avoids the same way.
    const int outset = std::max(gridGap_ / 2, 1);
    const int left = std::max(focused.x - outset, static_cast<int>(body.x));
    const int right = std::min(focused.x + focused.width + outset, body.x + body.width);
    const fui::Rect outline{static_cast<int16_t>(left), static_cast<int16_t>(focused.y - outset),
                            static_cast<int16_t>(right - left), static_cast<int16_t>(focused.height + 2 * outset)};
    screen.target().stroke(outline, fui::Paint::solid(fui::Color::Black), 2,
                           static_cast<uint8_t>(std::min(outline.height / 2, 255)));
  }
}

void TagFilterActivity::drawFooter() {
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  if (pageCount_ <= 1) return;

  char indicator[24];
  snprintf(indicator, sizeof(indicator), "%d/%d", pageNumber_ + 1, pageCount_);
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  GUI.drawSubHeader(renderer,
                    Rect{safe.x, safe.y + metrics.topPadding + metrics.headerHeight, safe.width, metrics.tabBarHeight},
                    "", indicator);
}

void TagFilterActivity::onBackButton() {
  ActivityResult result;
  result.isCancelled = true;
  setResult(std::move(result));
  finish();
}

bool TagFilterActivity::handleHomeGesture() {
  // Consumed rather than left to ActivityManager's "go home", which would
  // abandon the reader entirely from a filter screen. Cancelling matches Back.
  onBackButton();
  return true;
}

void TagFilterActivity::onRowAction(const fui::ActionEvent& event) {
  {
    RenderLock lock(*this);
    buttonFocus_ = false;
  }
  UiListActivity::onRowAction(event);
}

void TagFilterActivity::onRowLongPress(const int index) {
  if (confirmPopup_.isActive()) return;
  if (index < 0 || index >= listCount()) return;
  const TagChipView::ChipEntry& chip = chips_[static_cast<size_t>(index)];
  // All and Unlabelled are not palette entries; a long-press there must never reach the retire path.
  if (chip.kind != TagChips::Kind::Tag) return;
  app.clearTapFlash();
  showRetireConfirmation(chip.id);
}

void TagFilterActivity::showRetireConfirmation(const study::TagId id) {
  if (confirmPopup_.isActive()) return;
  if (STUDY.saveDisabled()) {
    // The file may still hold the user's data; never let a destructive palette
    // change through in that state, matching the picker's own bail.
    ReaderUtils::showMessage(renderer, tr(STR_HIGHLIGHTS_LOAD_FAILED));
    requestUpdate();
    return;
  }

  pendingRetireId_ = id;
  confirmingDelete_ = true;
  const char* options[] = {tr(STR_CANCEL), tr(STR_DELETE)};
  confirmPopup_.show(tr(STR_CONFIRM_DELETE_TAG), options, 2, 0, [this](const int idx) {
    confirmingDelete_ = false;
    if (idx == 1) retireTag(pendingRetireId_);
    requestUpdate();
  });
  requestUpdate();
}

void TagFilterActivity::retireTag(const study::TagId id) {
  const bool saved = STUDY.retireTag(id);
  {
    // A filter on a retired tag has no chip; fall back to All so one chip stays inverted, as
    // HighlightsActivity::dropRetiredFilter does on return. UNLABELLED is in no palette.
    RenderLock lock(*this);
    if (filter_ && *filter_ != study::UNLABELLED && !STUDY.palette().isActive(*filter_)) filter_.reset();
  }
  rebuildChips();
  requestUpdate();

  if (!saved) ReaderUtils::showMessage(renderer, tr(STR_HIGHLIGHTS_SAVE_FAILED));
}

bool TagFilterActivity::handleCustomInput() {
  if (confirmPopup_.handleInput(mappedInput, [this] { requestUpdate(); })) return true;
  if (confirmingDelete_) {
    // Popup dismissed without choosing (Back, or a tap outside it): drop the
    // pending retire and stay here.
    confirmingDelete_ = false;
    requestUpdate();
    return true;
  }

  const auto swipe = mappedInput.wasSwipe();
  if (swipe != MappedInputManager::SwipeDir::Up && swipe != MappedInputManager::SwipeDir::Down) return false;
  // Consumed either way: the base loop would scroll by list rows, which a grid does not have.
  turnPage(swipe == MappedInputManager::SwipeDir::Up ? 1 : -1, /*fromButton=*/false);
  return true;
}

void TagFilterActivity::navigateButtons() {
  // Each handler takes RenderLock only when its button fires. Locking here would
  // block every loop pass for as long as a render (panel refresh included) runs.
  buttonNavigator.onNextRelease([this] { stepSelection(1); });
  buttonNavigator.onPreviousRelease([this] { stepSelection(-1); });
  buttonNavigator.onNextContinuous([this] { turnPage(1, /*fromButton=*/true); });
  buttonNavigator.onPreviousContinuous([this] { turnPage(-1, /*fromButton=*/true); });
}

void TagFilterActivity::stepSelection(const int direction) {
  {
    RenderLock lock(*this);
    const int last = listCount() - 1;
    if (last < 0) return;
    nav.selected = std::clamp(nav.selected + direction, 0, last);
    buttonFocus_ = true;
  }
  requestUpdate();
}

void TagFilterActivity::turnPage(const int direction, const bool fromButton) {
  bool moved = false;
  {
    // One lock around read and write: the render task rewrites the page geometry mid-build.
    RenderLock lock(*this);
    const int count = listCount();
    const TagChips::PageSpan span =
        TagChips::pageHolding(widths_.data(), count, nav.selected, gridLineWidth_, gridGap_, gridLinesPerPage_);
    int target = -1;
    if (direction > 0 && span.next < count) target = span.next;
    if (direction < 0 && span.first > 0) {
      target =
          TagChips::pageHolding(widths_.data(), count, span.first - 1, gridLineWidth_, gridGap_, gridLinesPerPage_).first;
    }
    if (target >= 0) {
      nav.selected = target;
      buttonFocus_ = fromButton;
      moved = true;
    }
  }
  if (moved) requestUpdate();
}

void TagFilterActivity::render(RenderLock&&) {
  // Mirrors UiListActivity::render()'s body with the confirmation interleaved
  // between the app render and the footer; the base render has no seam for it.
  renderer.clearScreen();
  drawChrome();
  renderUi();
  for (int pass = 0; nav.consumeRebuildNeeded() && pass < 8; ++pass) {
    renderer.clearScreen();
    drawChrome();
    renderUi();
  }

  if (confirmPopup_.processRender(renderer, mappedInput)) return;

  drawFooter();
  renderer.displayBuffer();
  PostedMessage::drawNext(renderer);
}

void TagFilterActivity::activateIndex(const int index) {
  if (confirmPopup_.isActive()) return;
  if (index < 0 || index >= listCount()) return;
  app.clearTapFlash();

  TagSelectionResult result;
  // "All" returns an empty selection; every other chip reports a tag ID, never
  // an index -- the caller holds that filter across screens where the palette
  // may be edited. Unlabelled's id is study::UNLABELLED.
  const TagChipView::ChipEntry& chip = chips_[static_cast<size_t>(index)];
  if (chip.kind != TagChips::Kind::All) result.tagIds.push_back(chip.id);
  setResult(std::move(result));
  finish();
}
