#include "TagPickerActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <utility>

#include "../util/KeyboardEntryActivity.h"
#include "MappedInputManager.h"
#include "ReaderUtils.h"
#include "TagRowMapping.h"
#include "components/UITheme.h"
#include "study/StudyStore.h"

namespace fui = freeink::ui;

namespace {
// Matches HighlightsActivity's own threshold for "this Confirm release was a
// hold, not a tap" on boards with a physical Confirm button.
constexpr int ENTER_DELETE_MODE_MS = 700;
}  // namespace

TagPickerActivity::TagPickerActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                     std::vector<study::TagId> initialSelection)
    : UiListActivity("TagPicker", renderer, mappedInput, /*wantsTouchLongPress=*/true),
      tags_(STUDY.activeTags()),
      selectedIds_(std::move(initialSelection)) {
  // UNLABELLED is no row here and must not spend one of the per-passage slots.
  selectedIds_.erase(std::remove(selectedIds_.begin(), selectedIds_.end(), study::UNLABELLED), selectedIds_.end());
}

void TagPickerActivity::onEnter() {
  UiListActivity::onEnter();
  tags_ = STUDY.activeTags();
}

bool TagPickerActivity::isSelected(const study::TagId id) const {
  return std::find(selectedIds_.begin(), selectedIds_.end(), id) != selectedIds_.end();
}

void TagPickerActivity::setSelected(const study::TagId id, const bool on) {
  const auto it = std::find(selectedIds_.begin(), selectedIds_.end(), id);
  if (on && it == selectedIds_.end()) {
    selectedIds_.push_back(id);
  } else if (!on && it != selectedIds_.end()) {
    selectedIds_.erase(it);
  }
}

int TagPickerActivity::listCount() const { return static_cast<int>(tags_.size()) + 2; }

int TagPickerActivity::tagIndexForRow(const int row) const {
  return TagRows::tagIndexForRow(row, static_cast<int>(tags_.size()));
}

const char* TagPickerActivity::headerTitle() const { return tr(STR_TAGS); }

void TagPickerActivity::drawFooter() {
  // Matches StatusBarSettingsActivity's toggle-list footer: most rows here
  // toggle in place rather than navigating anywhere.
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

void TagPickerActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  screen.setContentMargin(
      fui::Insets{static_cast<int16_t>(safe.y + metrics.topPadding + metrics.headerHeight),
                  static_cast<int16_t>(renderer.getScreenWidth() - (safe.x + safe.width)),
                  static_cast<int16_t>(renderer.getScreenHeight() - (safe.y + safe.height) + metrics.buttonHintsHeight),
                  static_cast<int16_t>(safe.x)});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  // Refreshed every call (not cached from onEnter): "New tag..." can grow the
  // palette mid-visit, and a reallocation would strand any pointer captured on
  // an earlier visit -- see the rowItems_ comment in the header.
  tags_ = STUDY.activeTags();
  const int tagCount = static_cast<int>(tags_.size());
  // actionValue carries the ROW, not the tag: UiListActivity hands it straight
  // back to activateIndex/onRowLongPress and assigns it to nav.selected, so a
  // tag index here would desync the viewport from the list.
  fui::ListItem doneItem{};
  doneItem.label = tr(STR_DONE);
  doneItem.actionValue = static_cast<int16_t>(DONE_ROW);
  rowItems_[DONE_ROW] = doneItem;

  for (int i = 0; i < tagCount; ++i) {
    const auto& tag = tags_[static_cast<size_t>(i)];
    fui::ListItem item{};
    item.label = tag.name.c_str();
    item.toggle = true;
    item.toggleChecked = isSelected(tag.id);
    item.actionValue = static_cast<int16_t>(i + 1);
    rowItems_[i + 1] = item;
  }

  fui::ListItem newTagItem{};
  newTagItem.label = tr(STR_TAG_NEW);
  newTagItem.actionValue = static_cast<int16_t>(tagCount + 1);
  rowItems_[tagCount + 1] = newTagItem;

  fui::ListProps props;
  props.items = rowItems_;
  props.count = static_cast<uint16_t>(tagCount + 2);
  props.action = ACTION_ROW;
  // Tap toggles/opens; long-press deletes (physical buttons stay in loop()).
  props.inputMask = fui::InputTouch | fui::InputLongPress;
  syncListViewport(screen, props);
  screen.list(props);
}

void TagPickerActivity::activateIndex(const int row) {
  const int tagCount = static_cast<int>(tags_.size());
  if (row < 0 || row > tagCount + 1) return;
  nav.selected = row;

  if (row == DONE_ROW) {
    commitAndFinish();
    return;
  }
  if (row == tagCount + 1) {
    startNewTagFlow();
    return;
  }
  toggleTag(static_cast<size_t>(tagIndexForRow(row)));
}

void TagPickerActivity::toggleTag(const size_t tagRow) {
  if (tagRow >= tags_.size()) return;
  const study::TagId id = tags_[tagRow].id;

  if (!isSelected(id) && selectedIds_.size() >= study::PassageDoc::MAX_TAGS_PER_PASSAGE) {
    ReaderUtils::showMessage(renderer, tr(STR_TAG_LIMIT_PER_HIGHLIGHT));
    requestUpdate();
    return;
  }

  setSelected(id, !isSelected(id));
  requestUpdate();
}

void TagPickerActivity::startNewTagFlow() {
  // The row is activated via tap or Confirm and either way we're leaving
  // this screen for the keyboard; a lingering tap flash would gray an
  // unrelated row on return.
  app.clearTapFlash();

  auto handler = [this](const ActivityResult& result) {
    if (result.isCancelled) return;
    const auto& keyboard = std::get<KeyboardResult>(result.data);

    // StudyStore::addTagName persists the palette BEFORE handing back the id and
    // retires it again if that write fails, so an id can never be in use here
    // but absent from disk. It also dedupes by name, so an existing tag comes
    // back as its own id rather than a new row -- and going through
    // "New tag..." reads as intent to apply it either way.
    std::optional<study::TagId> id;
    {
      // buildScreen (render task) latches item.label = tags_[i].name.c_str()
      // every call; refreshing tags_ may reallocate, stranding any pointer a
      // concurrent render already took. Fence the mutation itself.
      RenderLock lock(*this);
      id = STUDY.addTagName(keyboard.text);
      tags_ = STUDY.activeTags();
    }
    if (!id) {
      reportAddTagFailure(keyboard.text);
      return;
    }

    if (selectedIds_.size() < study::PassageDoc::MAX_TAGS_PER_PASSAGE) setSelected(*id, true);
    requestUpdate();
  };

  startActivityForResult(
      std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_TAG_NAME_PROMPT), std::string(),
                                              study::TagPalette::MAX_TAG_NAME_BYTES, InputType::Text),
      handler);
}

void TagPickerActivity::reportAddTagFailure(const std::string& name) {
  // Mirrors HighlightDoc::addTag's own precedence (name validity is checked
  // before the palette-full case) so this needs no failure code out of
  // addTag to say something true. The too-long branch is normally
  // unreachable through this activity -- KeyboardEntryActivity's maxLength
  // already blocks typing past MAX_TAG_NAME_BYTES -- kept for defense in
  // depth against any other caller of this same helper later.
  if (name.empty()) {
    ReaderUtils::showMessage(renderer, tr(STR_TAG_NAME_EMPTY));
  } else if (name.size() > study::TagPalette::MAX_TAG_NAME_BYTES) {
    ReaderUtils::showMessage(renderer, tr(STR_TAG_NAME_TOO_LONG));
  } else {
    ReaderUtils::showMessage(renderer, tr(STR_TAG_PALETTE_FULL));
  }
  requestUpdate();
}

void TagPickerActivity::onBackButton() {
  ActivityResult result;
  result.isCancelled = true;
  setResult(std::move(result));
  finish();
}

bool TagPickerActivity::handleHomeGesture() {
  // A dialog open means the current selection is mid-edit; committing it out
  // from under the confirmation would finish the activity with a result the
  // user never confirmed leaving.
  if (confirmPopup_.isActive()) return true;
  commitAndFinish();
  return true;
}

void TagPickerActivity::commitAndFinish() {
  TagSelectionResult result;
  // Reported in palette order rather than the order they were checked, so a row
  // list reads the same however the user got there.
  for (const auto& tag : tags_) {
    if (isSelected(tag.id)) result.tagIds.push_back(tag.id);
  }
  setResult(std::move(result));
  finish();
}

void TagPickerActivity::onRowLongPress(const int row) {
  if (confirmPopup_.isActive()) return;
  // Delivered for every row within listCount(), including "Done" and
  // "New tag...", neither of which has anything to delete.
  const int tagIndex = tagIndexForRow(row);
  if (tagIndex < 0) return;
  app.clearTapFlash();
  nav.selected = row;
  showRetireConfirmation(static_cast<size_t>(tagIndex));
}

void TagPickerActivity::showRetireConfirmation(const size_t tagRow) {
  if (confirmPopup_.isActive()) return;
  if (STUDY.saveDisabled()) {
    // A store failed to load and may still hold the user's data; never let a
    // palette change through in that state.
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

void TagPickerActivity::retireTag(const size_t tagRow) {
  if (tagRow >= tags_.size()) return;  // stale row; nothing to do
  const study::TagId id = tags_[tagRow].id;

  bool retired = false;
  {
    // The render task reads tags_ mid-buildScreen (item.label =
    // tags_[i].name.c_str()); TagPickerActivity populates rowItems_ inline and
    // never caches it, so the mutation itself must be fenced -- the same tool
    // UiListActivity::moveSelectionTo uses for the identical loop-vs-render
    // race.
    RenderLock lock(*this);
    retired = STUDY.retireTag(id);
    tags_ = STUDY.activeTags();
    // No index shuffle. The bool array this replaced was aligned with the
    // palette and every entry above a deleted tag had to shift down or silently
    // come to mean a different tag; an id needs neither.
    setSelected(id, false);
  }
  requestUpdate();

  if (!retired) {
    ReaderUtils::showMessage(renderer, tr(STR_TAG_SAVE_FAILED));
    requestUpdate();
  }
}

bool TagPickerActivity::handleCustomInput() {
  if (confirmPopup_.handleInput(mappedInput, [this] { requestUpdate(); })) return true;
  if (confirmingDelete_) {
    // Popup dismissed without a selection (Back button/gesture, or a tap
    // outside it): cancel the pending delete, stay on this screen.
    confirmingDelete_ = false;
    requestUpdate();
    return true;
  }
  return false;
}

bool TagPickerActivity::handleButtons() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    onBackButton();
    return true;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    const int selected = nav.selected;
    if (selected < 0 || selected >= listCount()) return true;
    // Matches HighlightsActivity: a held Confirm release on a TAG row opens the
    // delete confirmation instead of toggling. Gated on the row->tag conversion,
    // never on a raw count: comparing a row index against a tag count would put
    // "Done" inside the delete range and leave the last tag unreachable.
    if (tagIndexForRow(selected) >= 0 && mappedInput.getHeldTime() > ENTER_DELETE_MODE_MS) {
      onRowLongPress(selected);
    } else {
      activateIndex(selected);
    }
    return true;
  }

  return false;
}

void TagPickerActivity::render(RenderLock&&) {
  // Duplicates UiListActivity::render()'s body (chrome, app, rebuild-retry
  // loop, footer, display) with the delete-confirmation popup interleaved
  // between the app render and the footer -- exactly where
  // HighlightsActivity's own render() puts the same check, and for the same
  // reason: the base render() has no seam to inject it into.
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
