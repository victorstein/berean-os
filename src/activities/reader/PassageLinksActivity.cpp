#include "PassageLinksActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <iterator>

#include "MappedInputManager.h"
#include "ReaderUtils.h"
#include "activities/ActivityResult.h"
#include "activities/PostedMessage.h"
#include "components/UITheme.h"
#include "study/StudyStore.h"

namespace fui = freeink::ui;

PassageLinksActivity::PassageLinksActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                           const size_t passageIndex)
    : UiListActivity("PassageLinks", renderer, mappedInput, /*wantsTouchLongPress=*/true), passageIndex_(passageIndex) {
  refreshLabels();
}

void PassageLinksActivity::refreshLabels() {
  labels_.clear();
  if (passageIndex_ >= STUDY.passages().size()) return;
  const auto& links = STUDY.passages()[passageIndex_].links;
  labels_.reserve(links.size());
  std::transform(links.begin(), links.end(), std::back_inserter(labels_),
                 [](const study::PassageLink& link) { return link.label; });
}

int PassageLinksActivity::listCount() const { return static_cast<int>(labels_.size()); }

const char* PassageLinksActivity::headerTitle() const { return tr(STR_LINKS); }

void PassageLinksActivity::drawFooter() {
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

void PassageLinksActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  screen.setContentMargin(
      fui::Insets{static_cast<int16_t>(safe.y + metrics.topPadding + metrics.headerHeight),
                  static_cast<int16_t>(renderer.getScreenWidth() - (safe.x + safe.width)),
                  static_cast<int16_t>(renderer.getScreenHeight() - (safe.y + safe.height) + metrics.buttonHintsHeight),
                  static_cast<int16_t>(safe.x)});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  rowItems_.clear();
  if (labels_.empty()) {
    screen.centeredText(tr(STR_NO_LINKS), screen.theme().bodyText);
    return;
  }

  rowItems_.reserve(labels_.size());
  for (size_t i = 0; i < labels_.size(); ++i) {
    fui::ListItem item{};
    item.label = labels_[i].empty() ? tr(STR_UNNAMED) : labels_[i].c_str();
    item.actionValue = static_cast<int16_t>(i);
    rowItems_.push_back(item);
  }

  fui::ListProps props;
  props.items = rowItems_.data();
  props.count = static_cast<uint16_t>(rowItems_.size());
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch | fui::InputLongPress;
  syncListViewport(screen, props);
  screen.list(props);
}

void PassageLinksActivity::onBackButton() {
  ActivityResult result;
  result.isCancelled = true;
  setResult(std::move(result));
  finish();
}

bool PassageLinksActivity::handleHomeGesture() {
  onBackButton();
  return true;
}

void PassageLinksActivity::activateIndex(const int index) {
  if (confirmPopup_.isActive()) return;
  if (index < 0 || index >= listCount()) return;
  nav.selected = index;
  app.clearTapFlash();

  const auto found = STUDY.locateLink(passageIndex_, static_cast<size_t>(index));
  if (!found) {
    ReaderUtils::showMessage(renderer, tr(STR_LINK_TARGET_NOT_FOUND));
    requestUpdate();
    return;
  }

  ProgressChangeResult result;
  result.spineIndex = static_cast<int>(found->spineIndex);
  result.hasVisibleTextOffset = true;
  result.visibleTextOffset = found->offset;
  setResult(std::move(result));
  finish();
}

void PassageLinksActivity::onRowLongPress(const int row) {
  if (confirmPopup_.isActive()) return;
  if (row < 0 || row >= listCount()) return;
  app.clearTapFlash();
  nav.selected = row;
  showRemoveConfirmation(static_cast<size_t>(row));
}

void PassageLinksActivity::showRemoveConfirmation(const size_t linkIndex) {
  if (confirmPopup_.isActive()) return;
  if (STUDY.saveDisabled()) {
    ReaderUtils::showMessage(renderer, tr(STR_HIGHLIGHTS_LOAD_FAILED));
    requestUpdate();
    return;
  }

  pendingRemoveIndex_ = linkIndex;
  confirmingRemove_ = true;
  const char* options[] = {tr(STR_CANCEL), tr(STR_REMOVE)};
  confirmPopup_.show(tr(STR_CONFIRM_REMOVE_LINK), options, 2, 0, [this](const int idx) {
    confirmingRemove_ = false;
    if (idx == 1) removeLink(pendingRemoveIndex_);
    requestUpdate();
  });
  requestUpdate();
}

void PassageLinksActivity::removeLink(const size_t linkIndex) {
  // rowItems_ borrows labels_' storage and the render task reads it, so both
  // are replaced under the render lock.
  bool removed = false;
  {
    RenderLock lock(*this);
    removed = STUDY.removeLink(passageIndex_, linkIndex);
    refreshLabels();
    rowItems_.clear();
    if (nav.selected >= listCount()) nav.selected = std::max(0, listCount() - 1);
  }
  if (!removed) ReaderUtils::showMessage(renderer, tr(STR_LINK_SAVE_FAILED));
}

bool PassageLinksActivity::handleCustomInput() {
  if (confirmPopup_.handleInput(mappedInput, [this] { requestUpdate(); })) return true;
  if (confirmingRemove_) {
    confirmingRemove_ = false;
    requestUpdate();
    return true;
  }
  return false;
}

void PassageLinksActivity::render(RenderLock&&) {
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
