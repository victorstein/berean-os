#include "TagFilterActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include "MappedInputManager.h"
#include "ReaderUtils.h"
#include "TagRowMapping.h"
#include "activities/ActivityResult.h"
#include "components/UITheme.h"

namespace fui = freeink::ui;

TagFilterActivity::TagFilterActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : UiListActivity("TagFilter", renderer, mappedInput, /*wantsTouchLongPress=*/true), tags_(STUDY.activeTags()) {}

int TagFilterActivity::listCount() const { return FilterRows::rowCount(static_cast<int>(tags_.size())); }

const char* TagFilterActivity::headerTitle() const { return tr(STR_FILTER_BY_TAG); }

void TagFilterActivity::drawFooter() {
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
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

  // Refreshed every call: a retirement between visits changes the list, and
  // rowItems_ borrows label pointers from tags_, so both move together.
  tags_ = STUDY.activeTags();
  rowItems_.clear();
  rowItems_.reserve(static_cast<size_t>(FilterRows::rowCount(static_cast<int>(tags_.size()))));

  fui::ListItem allItem{};
  allItem.label = tr(STR_TAG_FILTER_ALL);
  allItem.actionValue = static_cast<int16_t>(FilterRows::ALL);
  rowItems_.push_back(allItem);

  fui::ListItem unlabelledItem{};
  unlabelledItem.label = tr(STR_TAG_UNLABELLED);
  unlabelledItem.actionValue = static_cast<int16_t>(FilterRows::UNLABELLED);
  rowItems_.push_back(unlabelledItem);

  for (size_t i = 0; i < tags_.size(); ++i) {
    fui::ListItem item{};
    item.label = tags_[i].name.c_str();
    item.actionValue = static_cast<int16_t>(FilterRows::rowForTagIndex(static_cast<int>(i)));
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

void TagFilterActivity::onRowLongPress(const int row) {
  if (confirmPopup_.isActive()) return;
  const int tagIndex = FilterRows::tagIndexForRow(row, static_cast<int>(tags_.size()));
  if (tagIndex < 0) return;
  app.clearTapFlash();
  nav.selected = row;
  showRetireConfirmation(static_cast<size_t>(tagIndex));
}

void TagFilterActivity::showRetireConfirmation(const size_t tagRow) {
  if (confirmPopup_.isActive()) return;
  if (STUDY.saveDisabled()) {
    // The file may still hold the user's data; never let a destructive palette
    // change through in that state, matching the picker's own bail.
    ReaderUtils::showMessage(renderer, tr(STR_HIGHLIGHTS_LOAD_FAILED));
    requestUpdate();
    return;
  }

  pendingRetireRow_ = tagRow;
  confirmingDelete_ = true;
  const char* options[] = {tr(STR_CANCEL), tr(STR_DELETE)};
  confirmPopup_.show(tr(STR_CONFIRM_DELETE_TAG), options, 2, 0, [this](const int idx) {
    confirmingDelete_ = false;
    if (idx == 1) retireTag(pendingRetireRow_);
    requestUpdate();
  });
  requestUpdate();
}

void TagFilterActivity::retireTag(const size_t tagRow) {
  if (tagRow >= tags_.size()) return;  // stale row; nothing to do
  const study::TagId id = tags_[tagRow].id;

  // rowItems_ borrows label pointers from tags_, so the snapshot must be
  // replaced under the render lock. The SD write happens after, outside it.
  bool saved = false;
  {
    RenderLock lock(*this);
    saved = STUDY.retireTag(id);
    tags_ = STUDY.activeTags();
    rowItems_.clear();
  }
  requestUpdate();

  if (!saved) ReaderUtils::showMessage(renderer, tr(STR_HIGHLIGHTS_SAVE_FAILED));
}

bool TagFilterActivity::handleCustomInput() {
  if (confirmPopup_.handleInput(mappedInput, [this] { requestUpdate(); })) return true;
  if (confirmingDelete_) {
    // Popup dismissed without choosing (Back, or a tap outside it): drop the
    // pending delete and stay here.
    confirmingDelete_ = false;
    requestUpdate();
    return true;
  }
  return false;
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
}

void TagFilterActivity::activateIndex(const int index) {
  if (index < 0 || index >= listCount()) return;
  nav.selected = index;

  TagSelectionResult result;
  // "All" returns an empty selection; every other row reports a tag ID, never
  // the row number -- the caller holds that filter across screens where the
  // palette may be edited.
  if (index == FilterRows::UNLABELLED) {
    result.tagIds.push_back(study::UNLABELLED);
  } else if (const int tagIndex = FilterRows::tagIndexForRow(index, static_cast<int>(tags_.size())); tagIndex >= 0) {
    result.tagIds.push_back(tags_[static_cast<size_t>(tagIndex)].id);
  }
  setResult(std::move(result));
  finish();
}
