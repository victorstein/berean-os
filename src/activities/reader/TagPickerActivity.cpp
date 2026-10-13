#include "TagPickerActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <StudyStore/PassageDoc.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <optional>
#include <utility>

#include "../util/KeyboardEntryActivity.h"
#include "ReaderUtils.h"
#include "activities/ActivityResult.h"
#include "components/UITheme.h"
#include "study/StudyStore.h"

namespace fui = freeink::ui;

TagPickerActivity::TagPickerActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                     std::vector<study::TagId> initialSelection)
    : TagChipGridActivity("TagPicker", renderer, mappedInput),
      selectedIds_(TagChips::selectedInOrder(STUDY.palette().activeIds(), initialSelection)) {}

void TagPickerActivity::onEnter() {
  TagChipGridActivity::onEnter();
  app.on(ACTION_DONE, &TagPickerActivity::onDoneEvent, this);
  rebuildChips();
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

const char* TagPickerActivity::headerTitle() const {
  snprintf(title_, sizeof(title_), "%s %u/%u", tr(STR_TAGS), static_cast<unsigned>(selectedIds_.size()),
           static_cast<unsigned>(study::PassageDoc::MAX_TAGS_PER_PASSAGE));
  return title_;
}

void TagPickerActivity::drawChrome() {
  const auto& metrics = UITheme::getInstance().getMetrics();
  headerBand_ = Rect{0, metrics.topPadding, renderer.getScreenWidth(), metrics.headerHeight};
  doneRect_ = GUI.drawHeaderWithAction(renderer, headerBand_, headerTitle(), tr(STR_DONE));
}

void TagPickerActivity::buildChips(std::vector<TagChipView::ChipEntry>& out) {
  const std::vector<study::TagId> ids = STUDY.palette().activeIdsByName();
  out.reserve(ids.size() + 1);

  TagChipView::ChipEntry newTag;
  newTag.kind = TagChips::Kind::NewTag;
  snprintf(newTag.label, sizeof(newTag.label), "%s", tr(STR_TAG_NEW_CHIP));
  out.push_back(newTag);

  for (const study::TagId id : ids) {
    TagChipView::ChipEntry chip;
    chip.kind = TagChips::Kind::Tag;
    chip.id = id;
    snprintf(chip.label, sizeof(chip.label), "%s", STUDY.palette().name(id).c_str());
    out.push_back(chip);
  }
}

TagChips::ChipState TagPickerActivity::chipState(const TagChipView::ChipEntry& chip) const {
  return TagChips::pickerState(chip.kind, isSelected(chip.id), selectedIds_.size(),
                               study::PassageDoc::MAX_TAGS_PER_PASSAGE);
}

void TagPickerActivity::buildHeaderExtras(UiScreen& screen) {
  if (doneRect_.width <= 0 || doneRect_.height <= 0) return;
  const fui::Rect rect{static_cast<int16_t>(doneRect_.x), static_cast<int16_t>(doneRect_.y),
                       static_cast<int16_t>(doneRect_.width), static_cast<int16_t>(doneRect_.height)};
  // Classic's header corner leaves 25 px and fui::button insets its label 2 px each way, so the 24 px line
  // would hang out of the pill and below the header; centre the label on the whole rect instead.
  TagChipView::draw(screen, rect, "", TagChips::ChipState::Action, ACTION_DONE, 0, fui::InputTouch,
                    TagChips::bandHitPadding(headerBand_.y, headerBand_.height, doneRect_.y, doneRect_.height));
  fui::TextStyle label = screen.theme().smallText;
  label.bold = true;
  label.align = fui::TextAlign::Center;
  screen.target().text(rect, tr(STR_DONE), label);
}

void TagPickerActivity::activateIndex(const int index) {
  if (retireConfirmationOpen()) return;
  if (index < 0 || index >= listCount()) return;
  const TagChips::Kind kind = chips()[static_cast<size_t>(index)].kind;
  const study::TagId id = chips()[static_cast<size_t>(index)].id;

  switch (TagChips::pickerState(kind, isSelected(id), selectedIds_.size(), study::PassageDoc::MAX_TAGS_PER_PASSAGE)) {
    case TagChips::ChipState::Action:
      startNewTagFlow();
      return;
    case TagChips::ChipState::Disabled:
      ReaderUtils::showMessage(renderer, tr(STR_TAG_LIMIT_PER_HIGHLIGHT));
      requestUpdate();
      return;
    case TagChips::ChipState::Active:
    case TagChips::ChipState::Inactive:
      break;
  }
  // A toggle's feedback is its own repaint; a flash must never draw over it.
  app.clearTapFlash();
  {
    RenderLock lock(*this);
    setSelected(id, !isSelected(id));
  }
  requestUpdate();
}

void TagPickerActivity::startNewTagFlow() {
  app.clearTapFlash();
  auto handler = [this](const ActivityResult& result) {
    if (result.isCancelled) return;
    const auto& keyboard = std::get<KeyboardResult>(result.data);
    const std::optional<study::TagId> id = STUDY.addTagName(keyboard.text);
    if (!id) {
      reportAddTagFailure(keyboard.text);
      return;
    }
    switch (TagChips::pickerState(TagChips::Kind::Tag, isSelected(*id), selectedIds_.size(),
                                  study::PassageDoc::MAX_TAGS_PER_PASSAGE)) {
      case TagChips::ChipState::Inactive: {
        RenderLock lock(*this);
        setSelected(*id, true);
        break;
      }
      case TagChips::ChipState::Disabled:
        ReaderUtils::showMessage(renderer, tr(STR_TAG_LIMIT_PER_HIGHLIGHT));
        break;
      case TagChips::ChipState::Active:  // the name is a tag already on this passage
      case TagChips::ChipState::Action:
        break;
    }
    rebuildChips(TagChips::ChipKey{TagChips::Kind::Tag, *id});
  };

  startActivityForResult(
      std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_TAG_NAME_PROMPT), std::string(),
                                              study::TagPalette::MAX_TAG_NAME_BYTES, InputType::Text),
      handler);
}

void TagPickerActivity::reportAddTagFailure(const std::string& name) {
  // Mirrors TagPalette::add's precedence, name checks before the full palette, so it needs no
  // failure code from addTagName. A failed palette write also returns nullopt and reads as full
  // here. The too-long branch is normally unreachable: KeyboardEntryActivity's maxLength already
  // blocks typing past MAX_TAG_NAME_BYTES.
  if (name.empty()) {
    ReaderUtils::showMessage(renderer, tr(STR_TAG_NAME_EMPTY));
  } else if (name.size() > study::TagPalette::MAX_TAG_NAME_BYTES) {
    ReaderUtils::showMessage(renderer, tr(STR_TAG_NAME_TOO_LONG));
  } else {
    ReaderUtils::showMessage(renderer, tr(STR_TAG_PALETTE_FULL));
  }
  requestUpdate();
}

void TagPickerActivity::onTagRetired(const study::TagId id) {
  RenderLock lock(*this);
  setSelected(id, false);
}

void TagPickerActivity::onHome() { commitAndFinish(); }

void TagPickerActivity::onDoneEvent(const fui::ActionEvent&, void* user) {
  auto* self = static_cast<TagPickerActivity*>(user);
  if (self->retireConfirmationOpen()) return;
  self->commitAndFinish();
}

void TagPickerActivity::commitAndFinish() {
  app.clearTapFlash();
  TagSelectionResult result;
  result.tagIds = TagChips::selectedInOrder(STUDY.palette().activeIds(), selectedIds_);
  setResult(std::move(result));
  finish();
}
